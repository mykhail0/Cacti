#include "cacti.h"

#include <assert.h>
#include <bits/pthreadtypes.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "array.h"
#include "error.h"
#include "queue.h"

/*
#include <stdint.h>
#include <semaphore.h>
*/

static pthread_mutexattr_t attr;

void key_delete(pthread_key_t key) {
  int ret = pthread_key_delete(key);
  if (ret != 0) syserr(ret, "Failed key delete.\n");
}

void cond_signal(pthread_cond_t* cond) {
  if (pthread_cond_signal(cond) != 0) {
    fatal("pthread_cond_signal() should never return an error code.\n");
  }
}

static void mutex_lock(pthread_mutex_t* mutex) {
  int ret = pthread_mutex_lock(mutex);
  if (ret != 0) syserr(ret, "Mutex lock failed.\n");
}

static void mutex_unlock(pthread_mutex_t* mutex) {
  int ret = pthread_mutex_unlock(mutex);
  if (ret != 0) syserr(ret, "Mutex unlock failed.\n");
}

static void mutex_destroy(pthread_mutex_t* mutex) {
  int ret = pthread_mutex_destroy(mutex);
  if (ret != 0) syserr(ret, "Mutex destroy failed\n");
}

static const size_t INF = (size_t)-1;

// ACTOR_T CODE.

typedef struct {
  actor_id_t id;
  pthread_mutex_t mutex;

  // True iff actor's id cannot be added to the queue. TODO huh?
  bool working;
  // True iff processed MSG_GODIE.
  bool dead;
  // Queue of type message_t.
  queue_t mailbox;
  role_t role;
  void* state;
} actor_t;

// Construct the actor. Return `0` iff an actor created successfully, an
// errno-like error code otherwise.
static int actor_ctor(actor_t* actor, actor_id_t id, role_t role) {
  actor->id = id;
  actor->working = false;
  actor->dead = false;
  actor->role = role;
  actor->state = NULL;
  int ret = que_ctor(&(actor->mailbox), sizeof(message_t), ACTOR_QUEUE_LIMIT);
  if (ret != 0) {
    syserr(ret, "Message queue creation failed.\n");
  }

  if (0 != (ret = pthread_mutex_init(&(actor->mutex), &attr))) {
    que_dtor(&(actor->mailbox));
    // Because pthread_mutex_init always returns `0`.
    syserr(ret, "Mutex init failed.\n");
  }

  return 0;
}

// Destructs the actor.
static void actor_dtor(actor_t* actor) {
  mutex_destroy(&(actor->mutex));
  que_dtor(&(actor->mailbox));
}

#ifndef NDEBUG
// Print the queue of actor ids.
// static bool act_id_que_print(queue_t* q) {
//   fprintf(stderr, "CAPACITY: %lu\n", q->capacity);
//   while (!(q->empty)) {
//     actor_id_t id;
//     assert(que_pop(q, &id));
//     fprintf(stderr, "%ld ", id);
//   }
//   fprintf(stderr, "\n");
//   return true;
// }

// Prints the array of actor ids.
// static void act_arr_print(array_t* arr) {
//   fprintf(stderr, "FILLED: %lu CAPACITY: %lu\n", arr->filled, arr->capacity);
//   for (size_t i = 0; i < arr->filled; ++i) {
//     actor_t* actor = arr_at(arr, i);
//     fprintf(stderr, "%ld ", actor->id);
//   }
//   fprintf(stderr, "\n");
// }
#endif

// THREAD POOL

typedef struct {
  // True iff functioning system exists.
  bool created;

  // Multi purpose mutex for guarding both work_cond and actors_q.
  pthread_mutex_t mutex;

  // Threads wait for work on this condition.
  pthread_cond_t work_cond;

  // Queue of type actor_id_t.
  queue_t actors_q;

  // Guards actors.
  pthread_mutex_t act_mutex;
  size_t dead_cnt;
  array_t actors;

  pthread_t threads[POOL_SIZE];
} system_t;

static system_t sys;

// Thread specific data, actor_id of current thread,
// set before calling funcs from role.
static pthread_key_t thread_spec_act;

/*
void key_destructor(void *value) {
    actor_id_t *act = value;
    free(act);
}
*/

// Adds an actor to the system.
static int add_actor(system_t* s, actor_id_t* actor, role_t role) {
  actor_t* act;
  if (NULL == (act = malloc(sizeof *act))) {
    syserr(errno, "Memalloc for actor failed.\n");
  }

  int ret = actor_ctor(act, 0, role);
  if (ret != 0) {
    syserr(ret, "Actor construction failed.\n");
  }

  if (0 != (ret = arr_append(&(s->actors), &act))) {
    actor_dtor(act);
    syserr(ret, "Appending actor to array failed.\n");
  }

  act->id = s->actors.filled - 1;
  *actor = act->id;

  return 0;
}

// Handles the next message for the act_id actor.
static int handle_actor_request(system_t* s, actor_id_t act_id) {
  // Getting actor.
  mutex_lock(&(s->act_mutex));
  actor_t* actor = *(actor_t**)arr_at(&(s->actors), act_id);
  mutex_unlock(&(s->act_mutex));

  // TODO return code
  if (actor == NULL) return -1;

  // Getting the actor's message.
  message_t message;
  // TODO was soft before
  mutex_lock(&(actor->mutex));
  bool msg_succ = que_pop(&(actor->mailbox), &message);
  mutex_unlock(&(actor->mutex));

  // Can process the actor's message after successful retrieval.
  if (msg_succ) {
    if (message.message_type == MSG_SPAWN) {
      actor_id_t actor_to_add;

      // Adding a new actor.
      // TODO was soft
      mutex_lock(&(s->act_mutex));
      int ret = add_actor(s, &actor_to_add, *((role_t*)message.data));
      if (ret != 0) {
        mutex_unlock(&(s->act_mutex));
        syserr(ret, "Actor addition due to MSG_SPAWN failed.\n");
      }
      mutex_unlock(&(s->act_mutex));

      // Sending MSG_HELLO to the new actor.
      ret = send_message(actor_to_add, (message_t){.message_type = MSG_HELLO,
                                                   .nbytes = sizeof act_id,
                                                   .data = (void*)act_id});
      if (ret != 0) {
        fatal("Couldn't send a MSG_HELLO message to the spawned actor: %d.\n",
              ret);
      }
    } else if (message.message_type == MSG_GODIE) {
      // Changing the state of the actor.
      mutex_lock(&(actor->mutex));
      if (!(actor->dead)) {
        actor->dead = true;
        mutex_unlock(&(actor->mutex));

        mutex_lock(&(s->mutex));
        ++(s->dead_cnt);
        mutex_unlock(&(s->mutex));

      } else {
        mutex_unlock(&(actor->mutex));
      }
    } else {
      // Setting actor_id for this thread and calling the function.
      // Not so safe cast to void, but done as in (1)
      int ret = pthread_setspecific(thread_spec_act, (void const*)act_id);
      if (ret != 0) {
        syserr(ret, "Failed setting actor id info for the thread.\n");
      }
      if (message.message_type < 0 ||
          actor->role.nprompts <= (size_t)message.message_type) {
        return -1;
      }
      actor->role.prompts[message.message_type](&(actor->state), message.nbytes,
                                                message.data);
    }
  }

  mutex_lock(&(actor->mutex));

  // Adding the actor to the queue if there are messages to process.
  bool need_to_add = false;
  if (actor->mailbox.empty)
    actor->working = false;
  else
    need_to_add = true;

  mutex_unlock(&(actor->mutex));

  if (need_to_add) {
    mutex_lock(&(s->mutex));
    int ret = que_push(&(s->actors_q), &act_id);
    if (ret != 0) {
      mutex_unlock(&(s->mutex));
      syserr(ret, "Failed pushing an actor's id onto an actor's queue.\n");
    }
    cond_signal(&(s->work_cond));
    mutex_unlock(&(s->mutex));
  } else {
    mutex_lock(&(s->mutex));
    mutex_lock(&(s->act_mutex));
    if (s->dead_cnt >= s->actors.filled) cond_signal(&(s->work_cond));
    mutex_unlock(&(s->act_mutex));
    mutex_unlock(&(s->mutex));
  }

  return 0;
}

// Working function of a thread.
static void* work_func(void* arg) {
  system_t* s = arg;
  if (s == NULL) return NULL;

  // Work till stop message is not sent.
  while (true) {
    mutex_lock(&(s->mutex));
    mutex_lock(&(s->act_mutex));
    // Wait untill there is work or there is a stop message.
    // Stop message means that all actors are dead.
    while (s->actors_q.empty && s->dead_cnt < s->actors.filled) {
      mutex_unlock(&(s->act_mutex));
      if (pthread_cond_wait(&(s->work_cond), &(s->mutex)) != 0) {
        fatal("pthread_cond_wait() should never return an error code.\n");
      }
      mutex_lock(&(s->act_mutex));
    }

    // Can end work because processed all requests after got stop message.
    if (s->actors_q.empty && s->dead_cnt >= s->actors.filled) break;

    mutex_unlock(&(s->act_mutex));

    // Getting the next actor's id to process.
    actor_id_t act_id;
    bool act_ret_suc = que_pop(&(s->actors_q), &act_id);
    // Now other threads can access next ids.
    cond_signal(&(s->work_cond));
    mutex_unlock(&(s->mutex));

    // Can process the actor's message after successful retrieval.
    if (act_ret_suc) {
      handle_actor_request(s, act_id);
    }
  }

  mutex_unlock(&(s->act_mutex));
  cond_signal(&(s->work_cond));
  mutex_unlock(&(s->mutex));

  return NULL;
}

// Clearing actor's resources before freeing actor's array.
static void clear_act_arr(array_t* arr) {
  for (size_t i = 0; i < arr->filled; ++i) {
    actor_t* actor = *(actor_t**)arr_at(arr, i);
    actor_dtor(actor);
    free(actor);
  }
}

// Constructs the actor system.
static int system_ctor(system_t* s) {
  if (s == NULL || s->created) {
    fatal("System was already created\n.");
  }

  s->created = false;
  // Cant be 0 because work_funcs would stop (because dead_cnt == #actors == 0)
  s->dead_cnt = -1;

  int ret = pthread_mutex_init(&(s->act_mutex), &attr);
  if (ret != 0) {
    // Because pthread_mutex_init always returns `0`.
    syserr(ret, "Mutex init failed.\n");
  }

  if (0 != (ret = arr_ctor(&(s->actors), sizeof(actor_t*), CAST_LIMIT))) {
    mutex_destroy(&(s->act_mutex));
    syserr(ret, "actors array ctor fail.\n");
  }

  ret = pthread_mutex_init(&(s->mutex), &attr);
  if (ret != 0) {
    clear_act_arr(&(s->actors));
    arr_dtor(&(s->actors));
    mutex_destroy(&(s->act_mutex));
    // Because pthread_mutex_init always returns `0`.
    syserr(ret, "Mutex init failed.\n");
  }

  if (0 != (ret = que_ctor(&(s->actors_q), sizeof(actor_id_t), INF))) {
    mutex_destroy(&(s->mutex));
    clear_act_arr(&(s->actors));
    arr_dtor(&(s->actors));
    mutex_destroy(&(s->act_mutex));
    syserr(ret, "Queue of actors construction fail.\n");
  }

  if (pthread_cond_init(&(s->work_cond), NULL) != 0) {
    que_dtor(&(s->actors_q));
    mutex_destroy(&(s->mutex));
    clear_act_arr(&(s->actors));
    arr_dtor(&(s->actors));
    mutex_destroy(&(s->act_mutex));
    fatal("pthread_cond_init() should never return an error code.\n");
  }

  for (int i = 0; i < POOL_SIZE; ++i) {
    int ret = pthread_create(&(s->threads[i]), NULL, work_func, s);
    if (ret != 0) {
      pthread_cond_destroy(&(s->work_cond));
      que_dtor(&(s->actors_q));
      mutex_destroy(&(s->mutex));
      clear_act_arr(&(s->actors));
      arr_dtor(&(s->actors));
      mutex_destroy(&(s->act_mutex));
      syserr(ret, "pthread_create() fail.\n");
    }
  }

  s->created = true;
  return 0;
}

static int system_dtor(system_t* s) {
  if (s == NULL) return 0;

  for (int i = 0; i < POOL_SIZE; ++i) {
    int ret = pthread_join(s->threads[i], NULL);
    if (ret != 0) {
      syserr(ret, "pthread_join() fail.\n");
    }
  }

  int ret = pthread_cond_destroy(&(s->work_cond));
  if (ret != 0) {
    syserr(ret, "pthread_cond_destroy() fail while destructing the system.\n");
  }
  que_dtor(&(s->actors_q));
  mutex_destroy(&(s->mutex));

  clear_act_arr(&(s->actors));
  arr_dtor(&(s->actors));
  mutex_destroy(&(s->act_mutex));

  s->created = false;

  return 0;
}

actor_id_t actor_id_self() {
  return (actor_id_t)pthread_getspecific(thread_spec_act);
}

int actor_system_create(actor_id_t* actor, role_t* const role) {
  int ret = pthread_key_create(&thread_spec_act, NULL);
  if (ret != 0) {
    syserr(ret, "PTHREAD_KEYS_MAX keys are already allocated.\n");
  }

  ret = pthread_mutexattr_init(&attr);
  if (ret != 0) syserr(ret, "Mutexattr init failed.\n");
#ifndef NDEBUG
  if (0 != (ret = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK))) {
    syserr(ret, "Mutexattr settype failed.\n");
  }
#endif
  if (system_ctor(&sys) != 0) {
    key_delete(thread_spec_act);
    fatal("system_ctor fail.\n");
  }
  mutex_lock(&(sys.mutex));
  mutex_lock(&(sys.act_mutex));
  if (add_actor(&sys, actor, *role) != 0) {
    key_delete(thread_spec_act);
    mutex_unlock(&(sys.act_mutex));
    fatal("Initial actor addition fail.\n");
  }
  ++sys.dead_cnt;  // now is rightfully 0 and wont end threads prematurely.
  mutex_unlock(&(sys.act_mutex));
  mutex_unlock(&(sys.mutex));

  actor_id_t placeholder = 0;
  ret = send_message(*actor, (message_t){.message_type = MSG_HELLO,
                                         .nbytes = sizeof placeholder,
                                         .data = (void*)placeholder});
  if (ret != 0) {
    fatal("A MSG_HELLO message to the initial actor not sent: %d.\n", ret);
  }

  return 0;
}

void actor_system_join(actor_id_t actor) {
  if (actor < 0 || sys.actors.filled <= (size_t)actor) return;
  int ret = system_dtor(&sys);
  if (ret != 0) fatal("System destruction failed: %d.\n", ret);
  if (0 != (ret = pthread_mutexattr_destroy(&attr))) {
    syserr(ret, "Mutexattr destroy failed.\n");
  }
  key_delete(thread_spec_act);
}

const int DEAD_ACTOR = -1;
const int UNKNOWN_ACTOR = -2;
const int SYSTEM_NOT_CREATED = -3;

int send_message(actor_id_t actor, message_t message) {
  if (!sys.created) return SYSTEM_NOT_CREATED;

  // Getting the actor.
  if (actor < 0) return UNKNOWN_ACTOR;
  mutex_lock(&(sys.act_mutex));
  actor_t* actor_v = *(actor_t**)arr_at(&(sys.actors), actor);
  mutex_unlock(&(sys.act_mutex));
  if (actor_v == NULL) return UNKNOWN_ACTOR;

  mutex_lock(&(actor_v->mutex));
  if (actor_v->dead) {
    mutex_unlock(&(actor_v->mutex));
    return DEAD_ACTOR;
  }

  // Inserting the message.
  int ret = que_push(&(actor_v->mailbox), &message);
  if (ret != 0) {
    mutex_unlock(&(actor_v->mutex));
    syserr(ret, "Pushing a message to actor's mailbox fail.\n");
  }

  // Inserting the actor to the queue if it is not present.
  if (!(actor_v->working)) {
    actor_v->working = true;
    mutex_unlock(&(actor_v->mutex));

    mutex_lock(&(sys.mutex));
    if (0 != (ret = que_push(&(sys.actors_q), &actor))) {
      mutex_unlock(&(sys.mutex));
      syserr(ret, "que_push failed.\n");
    }
    cond_signal(&(sys.work_cond));
    mutex_unlock(&(sys.mutex));
  } else {
    mutex_unlock(&(actor_v->mutex));
  }

  return 0;
}
