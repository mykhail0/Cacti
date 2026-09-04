#include "cacti.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "array.h"
#include "err.h"
#include "queue.h"

/*
#include <stdint.h>
#include <semaphore.h>
*/

#define MUTEX_LOCK(arg)                                                        \
  do {                                                                         \
    int ret = pthread_mutex_lock(arg);                                         \
    if (ret != 0) {                                                            \
      if (ret == EINVAL) {                                                     \
        fprintf(stderr, "The mutex has not been properly initialized.");       \
      } else if (ret == EDEADLK) {                                             \
        fprintf(stderr, "The mutex is already locked by the calling thread."); \
      } else {                                                                 \
        fprintf(stderr, "Unknown pthread_mutex_lock error.");                  \
      }                                                                        \
      exit(EXIT_FAILURE);                                                      \
    }                                                                          \
  } while (false)

// SOFT_MUTEX_LOCK returns -1 on error

#define MUTEX_UNLOCK(arg)                 \
  do {                                    \
    if (pthread_mutex_unlock(arg) != 0) { \
      printf("mutex unlock\n");           \
      syserr();                           \
    }                                     \
  } while (false)

#define MUTEX_DESTROY(arg)                 \
  do {                                     \
    if (pthread_mutex_destroy(arg) != 0) { \
      printf("mutex destroy\n");           \
      syserr();                            \
    }                                      \
  } while (false)

#define KEY_DELETE(arg)                 \
  do {                                  \
    if (pthread_key_delete(arg) != 0) { \
      printf("key delete\n");           \
      syserr();                         \
    }                                   \
  } while (false)

#define COND_SIGNAL(arg)                 \
  do {                                   \
    if (pthread_cond_signal(arg) != 0) { \
      printf("cond signal\n");           \
      syserr();                          \
    }                                    \
  } while (false)

static const size_t INF = (size_t)-1;

const int DEAD_ACTOR = -1;
const int UNKNOWN_ACTOR = -2;

// ACTOR ID CODE.

/*
// FOR TESTING
// Prints the queue of actor ids.
int act_id_que_print(queue_t *q) {
    printf("CAPACITY: %lu\n", q->capacity);
    while (!(q->empty)) {
        actor_id_t id;
        if (act_id_que_pop(q, &id) == -1)
            return -1;
        printf("%ld ", id);
    }
    printf("\n");
    return 0;
}
*/

// ACTOR_T CODE.

typedef struct {
  actor_id_t id;
  pthread_mutex_t mutex;

  // True iff actor's id cannot be added to the queue.
  bool working;
  // True iff processed MSG_GODIE.
  bool dead;
  // Queue of type message_t
  queue_t mailbox;
  role_t role;
  void* state;
} actor_t;

// Constructs the actor.
// Returns 0 on success.
int actor_ctor(actor_t* actor, actor_id_t id, void* state, role_t role) {
  actor->id = id;
  if (pthread_mutex_init(&(actor->mutex), NULL) != 0) return -1;
  actor->working = false;
  actor->dead = false;

  if (!que_ctor(&(actor->mailbox), sizeof(message_t), ACTOR_QUEUE_LIMIT)) {
    MUTEX_DESTROY(&(actor->mutex));
    errno = ENOMEM;
    return -1;
  }

  actor->role = role;
  actor->state = state;
  return 0;
}

// Destructs the actor.
void actor_dtor(actor_t* actor) {
  que_dtor(&(actor->mailbox));
  MUTEX_DESTROY(&(actor->mutex));
}

// Returns pointer to the i'th actor in arr.
actor_t* act_arr_at(array_t* arr, size_t i) {
  if (i >= arr->filled) return NULL;
  return ((actor_t**)arr->arr)[i];
}

// Appends an actor to the array.
// Returns 0 on success, -1 and errno on not succeding with allocation
int act_arr_append(array_t* arr, actor_t* act) {
  if (!arr_reall(arr)) {
    errno = ENOMEM;
    return -1;
  }

  act->id = arr->filled++;
  ((actor_t**)arr->arr)[arr->filled - 1] = act;
  return 0;
}

/*
// FOR TESTING
// Prints the queue of actor ids.
int act_arr_print(array_t *arr) {
    printf("FILLED: %lu CAPACITY: %lu\n", arr->filled, arr->capacity);

    for (size_t i = 0; i < arr->filled; ++i) {
        actor_t *actor = act_arr_at(arr, i);
        printf("%ld ", actor->id);
    }

    printf("\n");
    return 0;
}
*/

// THREAD POOL

typedef struct {
  /*
  Multi purpose mutex.
  Guards work_cond and actors_q.
  */
  pthread_mutex_t mutex;

  // True iff functioning system exists.
  bool created;

  // Threads wait for work on this condition.
  pthread_cond_t work_cond;

  // Guards actors.
  pthread_mutex_t act_mutex;
  size_t dead_cnt;
  array_t actors;

  // Queue of type actor_id_t.
  queue_t actors_q;

  pthread_t threads[POOL_SIZE];
} system_t;

system_t sys;

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
int add_actor(system_t* s, actor_id_t* actor, role_t role) {
  actor_t* act = malloc(sizeof *act);
  if (act == NULL) {
    printf("couldnt allocate\n");
    errno = ENOMEM;
    return -1;
  }

  if (actor_ctor(act, 0, NULL, role) != 0) {
    printf("couldnt construct\n");
    return -1;
  }

  if (act_arr_append(&(s->actors), act) != 0) {
    actor_dtor(act);
    printf("couldnt append\n");
    return -1;
  }

  *actor = act->id;

  return 0;
}

// Handles the next message for the act_id actor.
int handle_actor_request(system_t* s, actor_id_t act_id) {
  // Getting actor.
  // TODO was soft before
  MUTEX_LOCK(&(s->act_mutex));
  actor_t* actor = act_arr_at(&(s->actors), act_id);
  MUTEX_UNLOCK(&(s->act_mutex));

  if (actor == NULL) return -1;

  // Getting the actor's message.
  message_t message;
  // TODO was soft before
  MUTEX_LOCK(&(actor->mutex));
  bool msg_succ = que_pop(&(actor->mailbox), &message);
  MUTEX_UNLOCK(&(actor->mutex));

  // Can process the actor's message after successful retrieval.
  if (msg_succ) {
    // printf("actor %ld handling %ld message\n", act_id, message.message_type);

    if (message.message_type == MSG_SPAWN) {
      actor_id_t actor_to_add;

      // Adding a new actor.
      // TODO was soft
      MUTEX_LOCK(&(s->act_mutex));
      if (add_actor(s, &actor_to_add, *((role_t*)message.data)) != 0) {
        MUTEX_UNLOCK(&(s->act_mutex));
        printf("543\n");
        syserr();
      }
      MUTEX_UNLOCK(&(s->act_mutex));

      // Sending MSG_HELLO to the new actor.
      // (1) Not so safe cast, but says so in the specification.
      if (send_message(actor_to_add,
                       (message_t){MSG_HELLO, 0, (void*)act_id}) != 0) {
        printf("551\n");
        syserr();
      }
    } else if (message.message_type == MSG_GODIE) {
      // Changing the state of the actor.
      MUTEX_LOCK(&(actor->mutex));
      if (!(actor->dead)) {
        actor->dead = true;
        MUTEX_UNLOCK(&(actor->mutex));

        MUTEX_LOCK(&(s->mutex));
        ++(s->dead_cnt);
        // printf("now dead cnt is %lu\n", s->dead_cnt);
        MUTEX_UNLOCK(&(s->mutex));

      } else {
        MUTEX_UNLOCK(&(actor->mutex));
      }
    } else {
      // Setting actor_id for this thread and calling the function.
      // Not so safe cast to void, but done as in (1)
      if (pthread_setspecific(thread_spec_act, (const void*)act_id) != 0) {
        printf("563\n");
        syserr();
      }
      if (message.message_type < 0 ||
          actor->role.nprompts <= (size_t)message.message_type) {
        return -1;
      }
      actor->role.prompts[message.message_type](&(actor->state), message.nbytes,
                                                message.data);
    }
  }

  MUTEX_LOCK(&(actor->mutex));

  // Adding the actor to the queue if there are messages to process.
  bool need_to_add = false;
  if (actor->mailbox.empty)
    actor->working = false;
  else
    need_to_add = true;

  MUTEX_UNLOCK(&(actor->mutex));

  if (need_to_add) {
    MUTEX_LOCK(&(s->mutex));
    if (que_push(&(s->actors_q), &act_id) != 0) {
      MUTEX_UNLOCK(&(s->mutex));
      printf("590\n");
      syserr();
    }
    COND_SIGNAL(&(s->work_cond));
    MUTEX_UNLOCK(&(s->mutex));
  } else {
    MUTEX_LOCK(&(s->mutex));
    MUTEX_LOCK(&(s->act_mutex));
    if (s->dead_cnt >= s->actors.filled) COND_SIGNAL(&(s->work_cond));
    MUTEX_UNLOCK(&(s->act_mutex));
    MUTEX_UNLOCK(&(s->mutex));
  }

  return 0;
}

// Working function of a thread.
void* work_func(void* arg) {
  system_t* s = arg;
  if (s == NULL) return NULL;

  // Work till stop message is not sent.
  while (true) {
    MUTEX_LOCK(&(s->mutex));
    MUTEX_LOCK(&(s->act_mutex));
    // Wait untill there is work or there is stop message.
    // Stop message means that all actors are dead.
    while (s->actors_q.empty && s->dead_cnt < s->actors.filled) {
      MUTEX_UNLOCK(&(s->act_mutex));
      if (pthread_cond_wait(&(s->work_cond), &(s->mutex)) != 0) {
        printf("612\n");
        syserr();
      }
      MUTEX_LOCK(&(s->act_mutex));
    }

    // Can end work because processed all requests after got stop message.
    if (s->actors_q.empty && s->dead_cnt >= s->actors.filled) break;

    MUTEX_UNLOCK(&(s->act_mutex));

    // Getting the next actor's id to process.
    actor_id_t act_id;
    bool act_ret_suc = que_pop(&(s->actors_q), &act_id);
    // Now other threads can access next ids.
    COND_SIGNAL(&(s->work_cond));
    MUTEX_UNLOCK(&(s->mutex));

    // Can process the actor's message after successful retrieval.
    if (act_ret_suc) {
      handle_actor_request(s, act_id);
    }
  }

  MUTEX_UNLOCK(&(s->act_mutex));
  COND_SIGNAL(&(s->work_cond));
  MUTEX_UNLOCK(&(s->mutex));

  return NULL;
}

// Clearing actor's resources before freeing actor's array.
void clear_act_arr(array_t* arr) {
  for (size_t i = 0; i < arr->filled; ++i) {
    actor_t* actor = act_arr_at(arr, i);
    actor_dtor(actor);
    free(actor);
  }
}

// Constructs the actor system.
int system_ctor(system_t* s) {
  if (s == NULL || s->created) {
    printf("was created\n");
    return -1;
  }

  s->created = false;
  // Cant be 0 because work_funcs would stop (because dead_cnt == #actors == 0)
  s->dead_cnt = -1;

  if (pthread_mutex_init(&(s->act_mutex), NULL) != 0) {
    printf("couldnt init act mutex\n");
    return -1;
  }

  if (!arr_ctor(&(s->actors), sizeof(actor_t*), CAST_LIMIT)) {
    printf("couldnt act_ctor\n");
    MUTEX_DESTROY(&(s->act_mutex));
    return -1;
  }

  if (pthread_mutex_init(&(s->mutex), NULL) != 0) {
    printf("couldnt mutex init\n");
    clear_act_arr(&(s->actors));
    arr_dtor(&(s->actors));
    MUTEX_DESTROY(&(s->act_mutex));
    return -1;
  }

  if (!que_ctor(&(s->actors_q), sizeof(actor_id_t), INF)) {
    printf("couldnt que ctor\n");
    MUTEX_DESTROY(&(s->mutex));
    clear_act_arr(&(s->actors));
    arr_dtor(&(s->actors));
    MUTEX_DESTROY(&(s->act_mutex));
    return -1;
  }

  if (pthread_cond_init(&(s->work_cond), NULL) != 0) {
    printf("couldnt cond init\n");
    que_dtor(&(s->actors_q));
    MUTEX_DESTROY(&(s->mutex));
    clear_act_arr(&(s->actors));
    arr_dtor(&(s->actors));
    MUTEX_DESTROY(&(s->act_mutex));
    return -1;
  }

  for (int i = 0; i < POOL_SIZE; ++i) {
    if (pthread_create(&(s->threads[i]), NULL, work_func, s) != 0) {
      if (pthread_cond_destroy(&(s->work_cond)) != 0) {
        printf("692\n");
        syserr();
      }
      que_dtor(&(s->actors_q));
      MUTEX_DESTROY(&(s->mutex));
      clear_act_arr(&(s->actors));
      arr_dtor(&(s->actors));
      MUTEX_DESTROY(&(s->act_mutex));
      printf("700\n");
      syserr();
    }
  }

  s->created = true;
  return 0;
}

int system_dtor(system_t* s) {
  if (s == NULL) return 0;

  for (int i = 0; i < POOL_SIZE; ++i) {
    if (pthread_join(s->threads[i], NULL) != 0) {
      printf("720\n");
      syserr();
    }
  }

  if (pthread_cond_destroy(&(s->work_cond)) != 0) {
    printf("726\n");
    syserr();
  }
  que_dtor(&(s->actors_q));
  MUTEX_DESTROY(&(s->mutex));

  // act_arr_print(&(s->actors));

  clear_act_arr(&(s->actors));
  arr_dtor(&(s->actors));
  MUTEX_DESTROY(&(s->act_mutex));

  s->created = false;

  return 0;
}

actor_id_t actor_id_self() {
  return (actor_id_t)pthread_getspecific(thread_spec_act);
}

int actor_system_create(actor_id_t* actor, role_t* const role) {
  if (pthread_key_create(&thread_spec_act, NULL) != 0) {
    printf("key no success\n");
    return -1;
  }
  if (system_ctor(&sys) != 0) {
    printf("ctor no success\n");
    KEY_DELETE(thread_spec_act);
    return -1;
  }
  MUTEX_LOCK(&(sys.mutex));
  MUTEX_LOCK(&(sys.act_mutex));
  if (add_actor(&sys, actor, *role) != 0) {
    printf("adding no success\n");
    KEY_DELETE(thread_spec_act);
    MUTEX_UNLOCK(&(sys.act_mutex));
    return -1;
  }
  ++sys.dead_cnt;  // now is rightfully 0 and wont end threads prematurely.
  MUTEX_UNLOCK(&(sys.act_mutex));
  MUTEX_UNLOCK(&(sys.mutex));

  actor_id_t dummy = 0;
  if (send_message(*actor, (message_t){MSG_HELLO, 0, (void*)dummy}) != 0)
    syserr();

  return 0;
}

void actor_system_join(actor_id_t actor) {
  if (actor < 0 || sys.actors.filled <= (size_t)actor) return;
  if (system_dtor(&sys) != 0) {
    printf("767\n");
    syserr();
  }
  KEY_DELETE(thread_spec_act);
}

int send_message(actor_id_t actor, message_t message) {
  // printf("sending message %ld to %ld actor\n", message.message_type, actor);
  if (!sys.created) {
    return -3;
  }

  // Getting the actor.
  if (actor < 0) return -2;
  if (pthread_mutex_lock(&(sys.act_mutex)) != 0) return -3;
  actor_t* actor_v = act_arr_at(&(sys.actors), actor);
  MUTEX_UNLOCK(&(sys.act_mutex));
  if (actor_v == NULL) return -2;

  if (pthread_mutex_lock(&(actor_v->mutex)) != 0) return -3;
  if (actor_v->dead) {
    MUTEX_UNLOCK(&(actor_v->mutex));
    return -1;
  }

  // Inserting the message.
  if (que_push(&(actor_v->mailbox), &message) != 0) {
    MUTEX_UNLOCK(&(actor_v->mutex));
    return -3;
  }

  // Inserting the actor to the queue if it is not present.
  if (!(actor_v->working)) {
    // printf("actor %ld not working but soon to change\n", actor);
    actor_v->working = true;
    MUTEX_UNLOCK(&(actor_v->mutex));

    MUTEX_LOCK(&(sys.mutex));
    if (que_push(&(sys.actors_q), &actor) != 0) {
      MUTEX_UNLOCK(&(sys.mutex));
      printf("809\n");
      syserr();
    }
    COND_SIGNAL(&(sys.work_cond));
    MUTEX_UNLOCK(&(sys.mutex));
  } else {
    MUTEX_UNLOCK(&(actor_v->mutex));
  }

  return 0;
}
