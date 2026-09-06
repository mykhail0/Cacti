#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "cacti.h"
#include "error.h"

/*
The program receives a single number `n` on standard input and calculates the
factorial of `n`, that is `n!`, using an actor system. Each actor is to receive
`k = n * (n - 1) * (n - 2) * ... * (m + 1)` and `m`. It then spawns another
actor and sends them a message with `k * m` and `m - 1`. When `m == 0` the
program terminates and writes the result to the standard output.

In detail, each actor has an internal state with 2 numbers, so far accumulated
factorial, and how many numbers are left to multiply onto it. An actor spawns
the next actor and waits for a MSG_GREET message from it, which contains the
spawned actor's id. This enables the parent actor to send its state to the
spawned actor, which in turn updates its own state accordingly. Actors receive
destruction messages from their children, except for the base case, where the
final agent self destructs.
*/

#define NPROMPTS 4

static void hello_handler(void** stateptr, size_t nbytes, void* data);
static void factorial_handler(void** stateptr, size_t nbytes, void* data);
static void greet_handler(void** stateptr, size_t nbytes, void* data);
static void prepare_die_handler(void** stateptr, size_t nbytes, void* data);

static act_t PROMPTS[NPROMPTS] = {hello_handler, factorial_handler,
                                  greet_handler, prepare_die_handler};
static role_t ROLE = {.nprompts = NPROMPTS, .prompts = PROMPTS};

static const size_t MSG_FACTORIAL = 1;
static const size_t MSG_GREET = 2;
static const size_t MSG_PREPARE_DIE = 3;

typedef struct {
  actor_id_t parent;
  unsigned long long accumulated_factorial;
  unsigned left;
} state_t;

// Initialize the actor's state and send its ID back to the parent.
void hello_handler(void** stateptr, size_t nbytes, void* data) {
  (void)nbytes;
  if (NULL == (*stateptr = malloc(sizeof(state_t)))) {
    syserr(errno, "Cannot allocate agent's state.\n");
  }
  actor_id_t self_id = actor_id_self();
  state_t* state_ptr = (state_t*)(*stateptr);
  state_ptr->parent = (actor_id_t)data;
  int ret =
      send_message(state_ptr->parent, (message_t){.message_type = MSG_GREET,
                                                  .nbytes = sizeof self_id,
                                                  .data = (void*)self_id});
  if (ret != 0 && ret != UNKNOWN_ACTOR) {
    fatal("Hello handler can't send greet message, error: %d\n", ret);
  }
}

// Receive parent's state, calculate own state and prepare to send it to the
// next agent if needed.
void factorial_handler(void** stateptr, size_t nbytes, void* data) {
  (void)nbytes;
  // Update the factorial state.
  state_t* parent_state = (state_t*)data;
  state_t* state_ptr = (state_t*)(*stateptr);
  state_ptr->accumulated_factorial =
      parent_state->accumulated_factorial * parent_state->left;
  state_ptr->left = parent_state->left - 1;

  // Now parent's state needs no use and can be freed.
  int ret = send_message(
      state_ptr->parent,
      (message_t){.message_type = MSG_PREPARE_DIE, .nbytes = 0, .data = NULL});
  if (ret != 0 && ret != UNKNOWN_ACTOR) {
    fatal("Could not prepare parent to die.\n");
  }
  ret = send_message(
      state_ptr->parent,
      (message_t){.message_type = MSG_GODIE, .nbytes = 0, .data = NULL});
  if (ret != 0 && ret != UNKNOWN_ACTOR) {
    fatal("Could not kill parent.\n");
  }

  if (((state_t*)(*stateptr))->left == 0) {
    // If this is base case, print the result.
    printf("%llu\n", state_ptr->accumulated_factorial);
    // Self clean up and destruct.
    free(state_ptr);
    ret = send_message(
        actor_id_self(),
        (message_t){.message_type = MSG_GODIE, .nbytes = 0, .data = NULL});
    if (ret != 0) {
      fatal("Agent can't send self destruction message, error: %d\n", ret);
    }
  } else {
    // If not the base case, spawn the next agent.
    if (0 != (ret = send_message(actor_id_self(),
                                 (message_t){.message_type = MSG_SPAWN,
                                             .nbytes = sizeof ROLE,
                                             .data = &ROLE}))) {
      fatal("Factorial handler can't send spawn message, error: %d\n", ret);
    }
  }
}

// Propagate own state to the next agent.
void greet_handler(void** stateptr, size_t nbytes, void* data) {
  (void)nbytes;
  int ret =
      send_message((actor_id_t)data, (message_t){.message_type = MSG_FACTORIAL,
                                                 .nbytes = sizeof(state_t),
                                                 .data = *stateptr});
  if (ret != 0) {
    fatal("Greet handler can't send factorial message, error: %d\n", ret);
  }
}

// Clean up the agent's state before `MSG_GODIE`.
void prepare_die_handler(void** stateptr, size_t nbytes, void* data) {
  (void)nbytes;
  (void)data;
  free(*stateptr);
}

int main(void) {
  state_t zero_state;
  zero_state.accumulated_factorial = 1;
  scanf("%u", &(zero_state.left));

  actor_id_t first;
  if (actor_system_create(&first, &ROLE) != 0) return EXIT_FAILURE;

  message_t msg_factorial = {.message_type = MSG_FACTORIAL,
                             .nbytes = sizeof zero_state,
                             .data = &zero_state};
  int ret = send_message(first, msg_factorial);
  if (ret != 0) fatal("Can't send initial factorial message, error: %d\n", ret);

  actor_system_join(first);
  return 0;
}
