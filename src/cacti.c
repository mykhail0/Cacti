#include "cacti.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "array.h"
#include "error.h"
#include "queue.h"
#include "utility.h"

const int DEAD_ACTOR = -1;
const int UNKNOWN_ACTOR = -2;
const int SYSTEM_NOT_CREATED = -3;
const int SYSTEM_SHUTDOWN = -4;

// An object used for initializing all mutexes in the program.
// Controls how mutexes behave, useful for debugging.
static pthread_mutexattr_t attr;

// ACTOR_T

typedef struct {
  actor_id_t id;
  pthread_mutex_t mutex;

  // Actor is not in the queue or doing work.
  bool idle;
  // True iff processed MSG_GODIE.
  bool dead;
  // Queue of type message_t.
  queue_t mailbox;
  role_t role;
  void* state;
} actor_t;

// Construct the actor. Return `0` iff an actor created successfully, an
// errno-like error code otherwise.
static int actor_create(actor_t* actor, actor_id_t id, role_t role) {
  assert(actor != NULL);
  actor->id = id;
  actor->idle = true;
  actor->dead = false;
  actor->role = role;
  actor->state = NULL;
  int ret = q_create(&(actor->mailbox), sizeof(message_t), ACTOR_QUEUE_LIMIT);
  if (ret != 0) return ret;

  if (0 != (ret = pthread_mutex_init(&(actor->mutex), &attr))) {
    q_destroy(&(actor->mailbox));
    // Because pthread_mutex_init always returns `0`.
    syserr(ret, "Mutex init failed.\n");
  }

  return 0;
}

static void actor_destroy(actor_t* actor) {
  mutex_destroy(&(actor->mutex));
  q_destroy(&(actor->mailbox));
}

// Clearing actor's resources before freeing actor's array.
static void clear_actor_array(array_t* arr) {
  for (size_t i = 0; i < arr->filled; ++i) {
    actor_t* actor = *(actor_t**)array_at(arr, i);
    actor_destroy(actor);
    free(actor);
  }
}

// THREAD POOL

typedef struct {
  // True iff functioning system exists.
  bool created;

  // Guards work_cond, actors_q, shutdown and dead_cnt.
  pthread_mutex_t mutex;

  // Threads wait for work on this condition.
  pthread_cond_t work_cond;

  // Queue of type actor_id_t.
  queue_t actors_q;

  bool shutdown;
  size_t dead_cnt;

  // Guards actors.
  pthread_mutex_t act_mutex;
  array_t actors;

  pthread_t threads[POOL_SIZE];
} system_t;

static system_t sys;
static sigset_t SIGINT_set;

// Thread specific data, actor_id of current thread,
// set before calling funcs from role.
static pthread_key_t thread_specific_actor_id;

actor_id_t actor_id_self(void) {
  void* data = pthread_getspecific(thread_specific_actor_id);
  if (data == NULL) fatal("Failure to get actor_id\n.");
  return ((actor_id_t)data) - 1;
}

// Add an actor to the system. Return `0` iff an actor created successfully, an
// errno-like error code otherwise.
static int add_actor(system_t* s, actor_id_t* actor, role_t role) {
  actor_t* a;
  if (NULL == (a = malloc(sizeof *a))) return errno;

  mutex_lock(&(s->act_mutex));
  *actor = s->actors.filled;
  int ret = actor_create(a, *actor, role);
  if (ret != 0) {
    mutex_unlock(&(s->act_mutex));
    free(a);
    return ret;
  }

  if (0 != (ret = array_append(&(s->actors), &a))) {
    actor_destroy(a);
    mutex_unlock(&(s->act_mutex));
    free(a);
    return ret;
  }

  mutex_unlock(&(s->act_mutex));
  return 0;
}

static actor_t* get_actor(system_t* s, actor_id_t actor) {
  mutex_lock(&(s->act_mutex));
  actor_t* actor_v = *(actor_t**)array_at(&(s->actors), actor);
  mutex_unlock(&(s->act_mutex));
  return actor_v;
}

static bool is_shutdown(system_t* s) {
  mutex_lock(&(s->mutex));
  bool shutdown = s->shutdown;
  mutex_unlock(&(s->mutex));
  return shutdown;
}

static int msg_spawn_handler(system_t* s, actor_id_t actor, role_t role) {
  if (is_shutdown(s)) return SYSTEM_SHUTDOWN;
  actor_id_t actor_to_add;
  int ret = add_actor(s, &actor_to_add, role);
  if (ret != 0) syserr(ret, "Actor addition due to MSG_SPAWN failed.\n");

  // Sending MSG_HELLO to the new actor.
  ret = send_message(actor_to_add, (message_t){.message_type = MSG_HELLO,
                                               .nbytes = sizeof actor,
                                               .data = (void*)actor});
  if (ret != 0) {
    fatal("Couldn't send a MSG_HELLO message to the spawned actor: %d.\n", ret);
  }
  return 0;
}

static void msg_godie_handler(system_t* s, actor_t* actor) {
  mutex_lock(&(actor->mutex));
  actor->dead = true;
  mutex_unlock(&(actor->mutex));

  mutex_lock(&(s->mutex));
  ++(s->dead_cnt);
  mutex_unlock(&(s->mutex));
}

static void msg_other_handler(actor_id_t actor_id, actor_t* actor,
                              message_t message) {
  // Setting actor_id for this thread and calling the function.
  int ret = pthread_setspecific(thread_specific_actor_id,
                                (void const*)(actor_id + 1));
  if (ret != 0) {
    syserr(ret, "Failed setting actor id info for the thread.\n");
  }
  if (message.message_type < 0 ||
      actor->role.nprompts <= (size_t)message.message_type) {
    fatal("Unknown message type.\n");
  }
  actor->role.prompts[message.message_type](&(actor->state), message.nbytes,
                                            message.data);
}

// Handle the next message for the act_id actor.
static void make_actor_handle_message(system_t* s, actor_id_t actor_id) {
  actor_t* actor = get_actor(s, actor_id);
  if (actor == NULL) fatal("Invalid actor id in actor's queue found.\n");

  // Get the actor's message.
  message_t message;
  mutex_lock(&(actor->mutex));
  bool msg_exists = q_pop(&(actor->mailbox), &message);
  mutex_unlock(&(actor->mutex));
  if (!msg_exists) {
    fatal("Actor id found in actor's queue with no messages in mailbox.\n");
  }

  if (message.message_type == MSG_SPAWN) {
    msg_spawn_handler(s, actor_id, *((role_t*)message.data));
  } else if (message.message_type == MSG_GODIE) {
    msg_godie_handler(s, actor);
  } else {
    msg_other_handler(actor_id, actor, message);
  }

  // Signal other threads if there's more work or all actors died.
  mutex_lock(&(actor->mutex));
  mutex_lock(&(s->mutex));
  assert(!(actor->idle));
  if (actor->mailbox.empty) {
    actor->idle = true;
    // All actors may have died.
    mutex_lock(&(s->act_mutex));
    if (s->dead_cnt >= s->actors.filled && s->dead_cnt != 0) {
      cond_signal(&(s->work_cond));
    }
    mutex_unlock(&(s->act_mutex));
  } else {
    // There's more work to do for this actor.
    int ret = q_push(&(s->actors_q), &actor_id);
    if (ret != 0) {
      pthread_mutex_unlock(&(s->mutex));
      pthread_mutex_unlock(&(actor->mutex));
      syserr(ret, "Failed pushing an actor's id onto an actor's queue.\n");
    }
    cond_signal(&(s->work_cond));
  }
  mutex_unlock(&(s->mutex));
  mutex_unlock(&(actor->mutex));
}

// Dedicated thread handling SIGINT.
static void* signal_handler(void* arg) {
  sigset_t* blocked = arg;
  assert(blocked != NULL);
  int sig;
  int ret = sigwait(blocked, &sig);
  if (ret != 0) syserr(ret, "sigwait fail.\n");
  assert(sig == SIGINT);
  mutex_lock(&(sys.mutex));
  sys.shutdown = true;
  cond_signal(&(sys.work_cond));
  mutex_unlock(&(sys.mutex));
  return NULL;
}

// Working function of a thread.
static void* work_func(void* arg) {
  system_t* s = arg;
  assert(s != NULL);

  while (true) {
    mutex_lock(&(s->mutex));
    mutex_lock(&(s->act_mutex));
    // Wait untill there is work or it is known that work won't come.
    while (s->actors_q.empty &&
           (s->dead_cnt < s->actors.filled || s->dead_cnt == 0) &&
           !s->shutdown) {
      mutex_unlock(&(s->act_mutex));
      if (pthread_cond_wait(&(s->work_cond), &(s->mutex)) != 0) {
        fatal("pthread_cond_wait() should never return an error code.\n");
      }
      mutex_lock(&(s->act_mutex));
    }

    // Can end work because processed all requests, and either all actors are
    // dead or the system shutdown was requested.
    if (s->actors_q.empty && (s->dead_cnt >= s->actors.filled || s->shutdown)) {
      break;
    }

    mutex_unlock(&(s->act_mutex));

    // Getting the next actor's id to process.
    actor_id_t act_id;
    bool actor_exists = q_pop(&(s->actors_q), &act_id);
    // Now other threads can access next ids.
    cond_signal(&(s->work_cond));
    mutex_unlock(&(s->mutex));

    // Can process the actor's message after successful retrieval.
    if (actor_exists) make_actor_handle_message(s, act_id);
  }

  mutex_unlock(&(s->act_mutex));
  cond_signal(&(s->work_cond));
  mutex_unlock(&(s->mutex));

  return NULL;
}

// Clean up the actor system after failing to spawn a thread.
static void fail_pthread_create_cleanup(system_t* s, int return_code) {
  clear_actor_array(&(s->actors));
  array_destroy(&(s->actors));
  q_destroy(&(s->actors_q));
  pthread_cond_destroy(&(s->work_cond));
  pthread_mutex_destroy(&(s->mutex));
  pthread_mutex_destroy(&(s->act_mutex));
  syserr(return_code, "pthread_create() fail.\n");
}

static void block_on_sigint(sigset_t* blocked) {
  sigset_t old;
  int ret = sigemptyset(blocked);
  if (ret != 0) syserr(errno, "sigemptyset fail.\n");
  if (0 != (ret = sigaddset(blocked, SIGINT))) {
    syserr(errno, "sigaddset fail.\n");
  }
  if (0 != (ret = pthread_sigmask(SIG_BLOCK, blocked, &old))) {
    syserr(ret, "sigprocmask fail.\n");
  }
}

static int system_create(system_t* s) {
  assert(s != NULL);
  if (s->created) return EBUSY;

  s->shutdown = false;
  s->created = false;
  s->dead_cnt = 0;

  block_on_sigint(&SIGINT_set);

  int ret = pthread_mutex_init(&(s->act_mutex), &attr);
  if (ret != 0) {
    // Because pthread_mutex_init always returns `0`.
    syserr(ret, "Mutex init failed.\n");
  }
  if (0 != (ret = pthread_mutex_init(&(s->mutex), &attr))) {
    pthread_mutex_destroy(&(s->act_mutex));
    // Because pthread_mutex_init always returns `0`.
    syserr(ret, "Mutex init failed.\n");
  }
  if (pthread_cond_init(&(s->work_cond), NULL) != 0) {
    pthread_mutex_destroy(&(s->mutex));
    pthread_mutex_destroy(&(s->act_mutex));
    fatal("pthread_cond_init() should never return an error code.\n");
  }
  if (0 != (ret = q_create(&(s->actors_q), sizeof(actor_id_t), CAST_LIMIT))) {
    cond_destroy(&(s->work_cond));
    mutex_destroy(&(s->mutex));
    mutex_destroy(&(s->act_mutex));
    return ret;
  }
  if (0 != (ret = array_create(&(s->actors), sizeof(actor_t*), CAST_LIMIT))) {
    q_destroy(&(s->actors_q));
    cond_destroy(&(s->work_cond));
    mutex_destroy(&(s->mutex));
    mutex_destroy(&(s->act_mutex));
    return ret;
  }

  s->created = true;
  ret = pthread_create(&(s->threads[0]), NULL, signal_handler, &SIGINT_set);
  if (ret != 0) fail_pthread_create_cleanup(s, ret);
  for (size_t i = 1; i < POOL_SIZE; ++i) {
    if (0 != (ret = pthread_create(&(s->threads[i]), NULL, work_func, s))) {
      kill(getpid(), SIGINT);
      for (size_t j = 1; j < i; ++j) {
        pthread_join(s->threads[j], NULL);
      }
      fail_pthread_create_cleanup(s, ret);
    }
  }

  return 0;
}

// Clean up globals other than system_t.
static void cleanup_globals() {
  bool success = pthread_mutexattr_destroy(&attr) == 0;
  success = pthread_key_delete(thread_specific_actor_id) == 0 && success;
  if (!success) fatal("Some global cleanup failed.\n");
}

int actor_system_create(actor_id_t* actor, role_t* const role) {
  if (POOL_SIZE < 2) fatal("Minimum number of threads should be 2.");
  int ret = pthread_key_create(&thread_specific_actor_id, NULL);
  if (ret != 0) {
    syserr(ret, "PTHREAD_KEYS_MAX keys are already allocated.\n");
  }

  ret = pthread_mutexattr_init(&attr);
  if (ret != 0) {
    pthread_key_delete(thread_specific_actor_id);
    syserr(ret, "Mutexattr init failed.\n");
  }
#ifndef NDEBUG
  if (0 != (ret = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK))) {
    cleanup_globals();
    syserr(ret, "Mutexattr settype failed.\n");
  }
#endif
  if (system_create(&sys) != 0) {
    cleanup_globals();
    fatal("System creation failed.\n");
  }
  if (0 != (ret = add_actor(&sys, actor, *role))) {
    cleanup_globals();
    fatal("Failed to add initial actor.\n");
  }

  actor_id_t placeholder = -1;
  ret = send_message(*actor, (message_t){.message_type = MSG_HELLO,
                                         .nbytes = sizeof placeholder,
                                         .data = (void*)placeholder});
  if (ret != 0) {
    fatal("A MSG_HELLO message to the initial actor not sent: %d.\n", ret);
  }

  return 0;
}

static void system_destroy(system_t* s) {
  if (s == NULL) return;

  bool success = true;
  for (size_t i = 1; i < POOL_SIZE; ++i) {
    success = pthread_join(s->threads[i], NULL) == 0 && success;
  }
  success = kill(getpid(), SIGINT) == 0 && success;
  success = pthread_join(s->threads[0], NULL) == 0 && success;

  success = pthread_cond_destroy(&(s->work_cond)) == 0 && success;
  q_destroy(&(s->actors_q));
  success = pthread_mutex_destroy(&(s->mutex)) == 0 && success;

  clear_actor_array(&(s->actors));
  array_destroy(&(s->actors));
  success = pthread_mutex_destroy(&(s->act_mutex)) == 0 && success;

  s->created = false;

  if (!success) fatal("Some destructor failed while destroying the system.\n");
}

void actor_system_join(actor_id_t actor) {
  if (actor < 0 || sys.actors.filled <= (size_t)actor) return;
  system_destroy(&sys);
  cleanup_globals();
}

static void add_actor_to_queue(system_t* s, actor_id_t* actor) {
  mutex_lock(&(s->mutex));
  int ret = q_push(&(s->actors_q), actor);
  if (ret != 0) {
    pthread_mutex_unlock(&(s->mutex));
    syserr(ret, "Actor q_push failed.\n");
  }
  cond_signal(&(s->work_cond));
  mutex_unlock(&(s->mutex));
}

int send_message(actor_id_t actor, message_t message) {
  if (!sys.created) return SYSTEM_NOT_CREATED;
  if (actor < 0) return UNKNOWN_ACTOR;
  actor_t* actor_v = get_actor(&sys, actor);
  if (actor_v == NULL) return UNKNOWN_ACTOR;
  if (is_shutdown(&sys)) return SYSTEM_SHUTDOWN;

  mutex_lock(&(actor_v->mutex));
  if (actor_v->dead) {
    mutex_unlock(&(actor_v->mutex));
    return DEAD_ACTOR;
  }

  // Inserting the message. This implementation is nonblocking.
  int ret = q_push(&(actor_v->mailbox), &message);
  if (ret != 0) {
    mutex_unlock(&(actor_v->mutex));
    if (ret == EAGAIN) return EAGAIN;
    syserr(ret, "Pushing a message to actor's mailbox fail.\n");
  }

  if (actor_v->idle) {
    add_actor_to_queue(&sys, &actor);
    actor_v->idle = false;
  }
  mutex_unlock(&(actor_v->mutex));

  return 0;
}
