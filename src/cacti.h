#ifndef CACTI_H
#define CACTI_H

#include <stddef.h>

typedef long message_type_t;

#define MSG_SPAWN (message_type_t)0x06057a6e
#define MSG_GODIE (message_type_t)0x60bedead
#define MSG_HELLO (message_type_t)0x0

// Max size of the message queue for any actor.
#ifndef ACTOR_QUEUE_LIMIT
#define ACTOR_QUEUE_LIMIT 1024
#endif

// Highest number of actors possible.
#ifndef CAST_LIMIT
#define CAST_LIMIT 1048576
#endif

// Number of threads in the thread pool.
// Note, that one of these threads is exclusively used to handle SIGINT signals.
#ifndef POOL_SIZE
#define POOL_SIZE 3
#endif

typedef struct message {
  message_type_t message_type;
  // Size of `data` as a number of bytes.
  size_t nbytes;
  // Message data in a format understandable by the `act_t` function which would
  // handle a message of this type.
  void* data;
} message_t;

typedef long actor_id_t;

// This function can be used in `act_t` functions to identify a recipient actor
// of a given message.
extern actor_id_t actor_id_self(void);

// `stateptr` is a pointer to the actor's internal state, who is handling the
// message. This state depends on the implementation of a given calculation -
// actors in different systems or with different roles may use different formats
// of internal state. `nbytes` is the size of `data` as a number of bytes.
// `data` may be a pointer to a fragment of a global state that can be read and
// modified by this function or it may be an integer type that can fit in void*.
// Same `data` as in `message_t`, so depends on the user defined protocol what
// it is.
typedef void (*const act_t)(void** stateptr, size_t nbytes, void* data);

typedef struct role {
  // Number of message types that can be handled by this role.
  size_t nprompts;
  // Array of functions for handling each message type, message types are
  // indexes of the array.
  act_t* prompts;
} role_t;

// Create the first actor in the system, responsible for initialization and
// terminating the execution, and initialize the thread pool handling the actor
// system. `actor` is an `in` parameter.
// Return `0` iff success, a negative value otherwise:
// `EBUSY` if the system was already created,
// various memory allocation error codes in case of failure in initializing
// internal fields.
extern int actor_system_create(actor_id_t* actor, role_t* const role);

// Wait until the actor system to which `actor` belongs to terminates.
extern void actor_system_join(actor_id_t actor);

extern const int DEAD_ACTOR;
extern const int UNKNOWN_ACTOR;
extern const int SYSTEM_NOT_CREATED;
extern const int SYSTEM_SHUTDOWN;

// Send a given message to a given actor.
// Return `0` if the operation succeeds,
// `DEAD_ACTOR == -1` if the actor does not accept messages (MSG_GODIE),
// `UNKNOWN_ACTOR == -2` if the actor with the specified identifier is not in
// the system,
// `SYSTEM_NOT_CREATED == -3` if actor_system_create() was not called
// successfully beforehand,
// `SYSTEM_SHUTDOWN == -4` if a SIGINT signal was received,
// `EAGAIN` if the recipient's message queue is full.
extern int send_message(actor_id_t actor, message_t message);

#endif
