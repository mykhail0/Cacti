#include "cacti.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

/*
#include <stdint.h>
#include <semaphore.h>
*/

#define MUTEX_LOCK(arg) do { \
        if (pthread_mutex_lock(arg) != 0) { \
            printf("mutex lock\n"); \
            syserr(); \
        } \
    } while (false)

// Returns -1;
#define SOFT_MUTEX_LOCK(arg) do { \
        if (pthread_mutex_lock(arg) != 0) { \
            printf("soft mutex lock\n"); \
            return -1; \
        } \
    } while (false)

#define MUTEX_UNLOCK(arg) do { \
        if (pthread_mutex_unlock(arg) != 0) { \
            printf("mutex unlock\n"); \
            syserr(); \
        } \
    } while (false)

#define MUTEX_DESTROY(arg) do { \
        if (pthread_mutex_destroy(arg) != 0) { \
            printf("mutex destroy\n"); \
            syserr(); \
        } \
    } while (false)

#define KEY_DELETE(arg) do { \
        if (pthread_key_delete(arg) != 0) { \
            printf("key delete\n"); \
            syserr(); \
        } \
    } while (false)

#define COND_SIGNAL(arg) do { \
        if (pthread_cond_signal(arg) != 0) { \
            printf("cond signal\n"); \
            syserr(); \
        } \
    } while (false)

extern int sys_nerr;

void syserr() {
    fprintf(stderr, "ERROR: (%d; %s)\n", errno, strerror(errno));
    exit(1);
}

size_t min(size_t a, size_t b) {
    return a < b ? a : b;
}

static const size_t INF = (size_t) -1;

// Factor by which queues and arrays capacity grow.
static const int MULTIPLIER = 2;
// Initial size for queues and arrays.
static const int INI_SIZE = 1;

// GENERIC ARRAY CODE.

// Function type for allocating a resource of some type.
typedef void *(*alloc_t)(void *ptr, size_t nbytes);

/*
Array has a maximal capacity,
and an allocator function used for realocation of the table.
*/
typedef struct {
    void *arr;
    alloc_t allocator;

    size_t MAX_CAPACITY;

    size_t capacity;
    size_t filled;
} array_t;

// Initiates an array with alloc allocator.
// Returns 0 on success, -1 and errno on not succeding with allocation
int arr_ctor(array_t *arr, alloc_t alloc, size_t max_capacity) {
    arr->arr = NULL;

    arr->MAX_CAPACITY = max_capacity;

    arr->capacity = min(INI_SIZE, arr->MAX_CAPACITY);
    arr->filled = 0;

    arr->allocator = alloc;

    void *tmp = arr->allocator(arr->arr, arr->capacity);
    if (tmp == NULL) {
        arr->capacity = 0;
        errno = ENOMEM;
        return -1;
    }

    arr->arr = tmp;
    return 0;
}

// Reallocates the array if needed.
// Returns 0 on success, -1 and errno on not succeding with allocation
int arr_reall(array_t *arr) {
    if (arr->filled == arr->capacity) {
        if (arr->capacity == arr->MAX_CAPACITY) {
            printf("array reached max capacity of %lu\n", arr->MAX_CAPACITY);
            errno = ENOMEM;
            return -1;
        }

        size_t previ = arr->capacity;
        arr->capacity = min(arr->capacity * MULTIPLIER, arr->MAX_CAPACITY);

        void *tmp = arr->allocator(arr->arr, arr->capacity);
        if (tmp == NULL) {
            arr->capacity = previ;
            errno = ENOMEM;
            return -1;
        }

        arr->arr = tmp;
    }
    return 0;
}

// Clears the array.
void arr_dtor(array_t *arr) {
    arr->filled = 0;
    arr->capacity = 0;
    free(arr->arr);
    arr->arr = NULL;
}

// GENERIC QUEUE CODE.

// Function type to swap values at i and j positions in ptr array.
typedef void (*swap_t)(void *ptr, size_t i, size_t j);

/*
Queue imlemented as a cyclic buffor, which is no bigger than max capacity.
Has an allocator function used to reallocate the bufor if it becomes full.
*/
typedef struct {
    void *arr;

    size_t MAX_CAPACITY;

    alloc_t allocator;
    swap_t swapper;

    bool empty;
    size_t capacity;

    size_t head;
    size_t tail;
} queue_t;

// Initiates an empty queue with alloc allocator.
// Returns 0 on success, -1 and errno on not succeding with allocation
int que_ctor(queue_t *q, alloc_t alloc, swap_t swap, size_t max_capacity) {
    q->arr = NULL;

    q->MAX_CAPACITY = max_capacity;

    q->empty = true;
    q->capacity = min(INI_SIZE, q->MAX_CAPACITY);

    q->head = 0;
    q->tail = q->head;

    q->allocator = alloc;
    q->swapper = swap;

    void *tmp = q->allocator(q->arr, q->capacity);
    if (tmp == NULL) {
        q->capacity = 0;
        errno = ENOMEM;
        return -1;
    }

    q->arr = tmp;
    return 0;
}

// Clears the queue.
// Needs to be initiated again
// if the user intends to use it.
void que_dtor(queue_t *q) {
    q->empty = true;
    q->head = 0;
    q->tail = q->head;
    q->capacity = 0;
    free(q->arr);
    q->arr = NULL;
}

// Reverses the [begin, end) subarray of the array using swap swapping function.
void reverse(void *arr, size_t begin, size_t end, swap_t swap) {
    size_t l = begin, r = end - 1;
    while (l < r)
        swap(arr, l++, r--);
}

// Left shifts by `to_shift` the array using swap swapping function.
void left_shift(void *arr, size_t to_shift, size_t size, swap_t swap) {
    reverse(arr, 0, size, swap);
    reverse(arr, 0, size - to_shift, swap);
    reverse(arr, size - to_shift, size, swap);
}

// Reallocates queues's cyclic buffor if needed.
// Returns 0 on success, -1 and errno on not succeding with allocation
// or if it is already max capacity size.
int que_reall(queue_t *q) {
    if (!(q->empty) && q->head == q->tail) {
        if (q->capacity == q->MAX_CAPACITY) {
            printf("my max capacity is %lu\n", q->MAX_CAPACITY);
            errno = ENOMEM;
            return -1;
        }

        size_t previ = q->capacity;
        q->capacity = min(q->capacity * MULTIPLIER, q->MAX_CAPACITY);

        void *tmp = q->allocator(q->arr, q->capacity);
        if (tmp == NULL) {
            q->capacity = previ;
            errno = ENOMEM;
            return -1;
        }

        q->arr = tmp;
        left_shift(q->arr, q->tail, previ, q->swapper);
        q->head = 0;
        q->tail = previ;
    }

    return 0;
}

// ACTOR ID CODE.

// Allocator for the array of actor_id.
void *act_id_alloc(void *ptr, size_t nbytes) {
    return realloc(ptr, nbytes * sizeof (actor_id_t));
}

// Pushes id to the queue of actor ids.
// Returns 0 on success, -1 and errno on not succeding with allocation
int act_id_que_push(queue_t *q, actor_id_t id) {
    if (que_reall(q) == -1) {
        errno = ENOMEM;
        return -1;
    }

    ((actor_id_t*) q->arr)[q->tail] = id;
    q->tail = (q->tail + 1) % q->capacity;
    q->empty = false;
    return 0;
}

// Pops id from the queue of actor ids.
// Returns 0 on success, -1 when the queue is empty.
int act_id_que_pop(queue_t *q, actor_id_t *id) {
    if (q->empty)
        return -1;

    *id = ((actor_id_t*) q->arr)[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->empty = q->head == q->tail;
    return 0;
}

// Swaps two values in the actor id array.
void act_id_swap(void *arr, size_t i, size_t j) {
    actor_id_t tmp = ((actor_id_t*) arr)[i];
    ((actor_id_t*) arr)[i] = ((actor_id_t*) arr)[j];
    ((actor_id_t*) arr)[j] = tmp;
}

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

// MESSAGE_T QUEUE.

// Allocator for the array of message_t.
void *mess_alloc(void *ptr, size_t nbytes) {
    return realloc(ptr, nbytes * sizeof (message_t));
}

// Pushes the message to the queue of messages.
// Returns 0 on success, -1 and errno on not succeding with allocation
int mess_que_push(queue_t *q, message_t message) {
    if (que_reall(q) == -1) {
        errno = ENOMEM;
        return -1;
    }

    ((message_t*) q->arr)[q->tail] = message;
    q->tail = (q->tail + 1) % q->capacity;
    q->empty = false;
    return 0;
}

// Pops a message from the queue of messages.
// Returns 0 on success, -1 when the queue is empty.
int mess_que_pop(queue_t *q, message_t *message) {
    if (q->empty)
        return -1;

    *message = ((message_t*) q->arr)[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->empty = q->head == q->tail;
    return 0;
}

// Swaps two values in the message_t array.
void mess_swap(void *arr, size_t i, size_t j) {
    message_t tmp = ((message_t*) arr)[i];
    ((message_t*) arr)[i] = ((message_t*) arr)[j];
    ((message_t*) arr)[j] = tmp;
}

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
    void *state;
} actor_t;

// Constructs the actor.
// Returns 0 on success.
int actor_ctor(actor_t *actor, actor_id_t id, void *state, role_t role) {
    actor->id = id;
    if (pthread_mutex_init(&(actor->mutex), NULL) != 0)
        return -1;
    actor->working = false;
    actor->dead = false;

    if (que_ctor(&(actor->mailbox), mess_alloc, mess_swap, ACTOR_QUEUE_LIMIT) == -1) {
        MUTEX_DESTROY(&(actor->mutex));
        errno = ENOMEM;
        return -1;
    }

    actor->role = role;
    actor->state = state;
    return 0;
}

// Destructs the actor.
void actor_dtor(actor_t *actor) {
    que_dtor(&(actor->mailbox));
    MUTEX_DESTROY(&(actor->mutex));
}

// Allocator for the array of actor.
void *act_alloc(void *ptr, size_t nbytes) {
    return realloc(ptr, nbytes * sizeof (actor_t*));
}

// Returns pointer to the i'th actor in arr.
actor_t *act_arr_at(array_t *arr, size_t i) {
    if (i >= arr->filled)
        return NULL;
    return ((actor_t**) arr->arr)[i];
}

// Appends an actor to the array.
// Returns 0 on success, -1 and errno on not succeding with allocation
int act_arr_append(array_t *arr, actor_t *act) {
    if (arr_reall(arr) == -1) {
        errno = ENOMEM;
        return -1;
    }

    act->id = arr->filled++;
    ((actor_t**) arr->arr)[arr->filled - 1] = act;
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
int add_actor(system_t *s, actor_id_t *actor, role_t role) {
    actor_t *act = malloc(sizeof *act);
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
int handle_actor_request(system_t *s, actor_id_t act_id) {
    // Getting actor.
    SOFT_MUTEX_LOCK(&(s->act_mutex));
    actor_t *actor = act_arr_at(&(s->actors), act_id);
    MUTEX_UNLOCK(&(s->act_mutex));

    if (actor == NULL)
        return -1;

    // Getting the actor's message.
    message_t message;
    SOFT_MUTEX_LOCK(&(actor->mutex));
    bool msg_succ = (mess_que_pop(&(actor->mailbox), &message) != -1);
    MUTEX_UNLOCK(&(actor->mutex));

    // Can process the actor's message after successful retrieval.
    if (msg_succ) {
        //printf("actor %ld handling %ld message\n", act_id, message.message_type);

        if (message.message_type == MSG_SPAWN) {
            actor_id_t actor_to_add;

            // Adding a new actor.
            SOFT_MUTEX_LOCK(&(s->act_mutex));
            if (add_actor(s, &actor_to_add, *((role_t*) message.data)) != 0) {
                MUTEX_UNLOCK(&(s->act_mutex));
                printf("543\n");
                syserr();
            }
            MUTEX_UNLOCK(&(s->act_mutex));

            // Sending MSG_HELLO to the new actor.
            // (1) Not so safe cast, but says so in the specification.
            if (send_message(actor_to_add, (message_t) {MSG_HELLO, 0, (void*) act_id}) != 0) {
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
            //printf("now dead cnt is %lu\n", s->dead_cnt);
                MUTEX_UNLOCK(&(s->mutex));

            } else {
                MUTEX_UNLOCK(&(actor->mutex));
            }
        } else {
            // Setting actor_id for this thread and calling the function.
            // Not so safe cast to void, but done as in (1)
            if (pthread_setspecific(thread_spec_act, (const void*) act_id) != 0) {
                printf("563\n");
                syserr();
            }
            if (message.message_type < 0 ||
                actor->role.nprompts <= (size_t) message.message_type) {
                return -1;
            }
            actor->role.prompts[message.message_type](&(actor->state), message.nbytes, message.data);
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
        if (act_id_que_push(&(s->actors_q), act_id) != 0) {
            MUTEX_UNLOCK(&(s->mutex));
            printf("590\n");
            syserr();
        }
        COND_SIGNAL(&(s->work_cond));
        MUTEX_UNLOCK(&(s->mutex));
    } else {
        MUTEX_LOCK(&(s->mutex));
        MUTEX_LOCK(&(s->act_mutex));
        if (s->dead_cnt >= s->actors.filled)
            COND_SIGNAL(&(s->work_cond));
        MUTEX_UNLOCK(&(s->act_mutex));
        MUTEX_UNLOCK(&(s->mutex));
    }

    return 0;
}

// Working function of a thread.
void *work_func(void *arg) {
    system_t *s = arg;
    if (s == NULL)
        return NULL;

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
        if (s->actors_q.empty && s->dead_cnt >= s->actors.filled)
            break;

        MUTEX_UNLOCK(&(s->act_mutex));

        // Getting the next actor's id to process.
        actor_id_t act_id;
        bool act_ret_suc = (act_id_que_pop(&(s->actors_q), &act_id) != -1);
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
void clear_act_arr(array_t *arr) {
    for (size_t i = 0; i < arr->filled; ++i) {
        actor_t *actor = act_arr_at(arr, i);
        actor_dtor(actor);
        free(actor);
    }
}

// Constructs the actor system.
int system_ctor(system_t *s) {
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

    if (arr_ctor(&(s->actors), act_alloc, CAST_LIMIT) == -1) {
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

    if (que_ctor(&(s->actors_q), act_id_alloc, act_id_swap, INF) != 0) {
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

int system_dtor(system_t *s) {
    if (s == NULL)
        return 0;

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

    //act_arr_print(&(s->actors));

    clear_act_arr(&(s->actors));
    arr_dtor(&(s->actors));
    MUTEX_DESTROY(&(s->act_mutex));

    s->created = false;

    return 0;
}

actor_id_t actor_id_self() {
    return (actor_id_t) pthread_getspecific(thread_spec_act);
}

int actor_system_create(actor_id_t *actor, role_t *const role) {
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
    ++sys.dead_cnt; // now is rightfully 0 and wont end threads prematurely.
    MUTEX_UNLOCK(&(sys.act_mutex));
    MUTEX_UNLOCK(&(sys.mutex));

    actor_id_t dummy = 0;
    if (send_message(*actor, (message_t) {MSG_HELLO, 0, (void*) dummy}) != 0)
        syserr();

    return 0;
}

void actor_system_join(actor_id_t actor) {
    if (actor < 0 || sys.actors.filled <= (size_t) actor)
        return;
    if (system_dtor(&sys) != 0) {
        printf("767\n");
        syserr();
    }
    KEY_DELETE(thread_spec_act);
}

int send_message(actor_id_t actor, message_t message) {
    //printf("sending message %ld to %ld actor\n", message.message_type, actor);
    if (!sys.created) {
        return -3;
    }

    // Getting the actor.
    if (actor < 0)
        return -2;
    if (pthread_mutex_lock(&(sys.act_mutex)) != 0)
        return -3;
    actor_t *actor_v = act_arr_at(&(sys.actors), actor);
    MUTEX_UNLOCK(&(sys.act_mutex));
    if (actor_v == NULL)
        return -2;

    if (pthread_mutex_lock(&(actor_v->mutex)) != 0)
        return -3;
    if (actor_v->dead) {
        MUTEX_UNLOCK(&(actor_v->mutex));
        return -1;
    }

    // Inserting the message.
    if (mess_que_push(&(actor_v->mailbox), message) != 0) {
        MUTEX_UNLOCK(&(actor_v->mutex));
        return -3;
    }

    // Inserting the actor to the queue if it is not present.
    if (!(actor_v->working)) {
        //printf("actor %ld not working but soon to change\n", actor);
        actor_v->working = true;
        MUTEX_UNLOCK(&(actor_v->mutex));

        MUTEX_LOCK(&(sys.mutex));
        if (act_id_que_push(&(sys.actors_q), actor) != 0) {
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
