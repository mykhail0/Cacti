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
spawned actor, which in turn updates its own state accordingly.
*/

#define NPROMPTS 3

static void hello_handler(void** stateptr, size_t nbytes, void* data);
static void factorial_handler(void** stateptr, size_t nbytes, void* data);
static void greet_handler(void** stateptr, size_t nbytes, void* data);

static act_t PROMPTS[NPROMPTS] = {hello_handler, factorial_handler,
                                  greet_handler};
static role_t ROLE = {.nprompts = NPROMPTS, .prompts = PROMPTS};

static const size_t MSG_FACTORIAL = 1;
static const size_t MSG_GREET = 2;

typedef struct {
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
  message_t msg_greet = {.message_type = MSG_GREET,
                         .nbytes = sizeof self_id,
                         .data = (void*)self_id};
  int ret = send_message((actor_id_t)data, msg_greet);
  if (ret != 0 && ret != UNKNOWN_ACTOR) {
    fatal("Hello handler can't send greet message, error: %d\n", ret);
  }
}

// Receive parent's state, calculate own state and prepare to send it to the
// next agent if needed.
void factorial_handler(void** stateptr, size_t nbytes, void* data) {
  (void)nbytes;
  // Copy parent's state.
  *(state_t*)(*stateptr) = *(state_t*)data;

  // Update the state to the next iteration of factorial's calculation.
  ((state_t*)(*stateptr))->accumulated_factorial *=
      ((state_t*)(*stateptr))->left;
  --(((state_t*)(*stateptr))->left);

  if (((state_t*)(*stateptr))->left == 0) {
    // If this is base case, print the result.
    printf("%llu\n", ((state_t*)(*stateptr))->accumulated_factorial);
  } else {
    // If not the base case, spawn the next agent.
    message_t msg_spawn = {
        .message_type = MSG_SPAWN, .nbytes = sizeof ROLE, .data = &ROLE};
    int ret = send_message(actor_id_self(), msg_spawn);
    if (ret != 0) {
      fatal("Factorial handler can't send spawn message, error: %d\n", ret);
    }
  }
}

// Propagate own state to the next agent.
void greet_handler(void** stateptr, size_t nbytes, void* data) {
  (void)nbytes;
  message_t msg_factorial = {.message_type = MSG_FACTORIAL,
                             .nbytes = sizeof(state_t),
                             .data = *stateptr};
  int ret = send_message((actor_id_t)data, msg_factorial);
  if (ret != 0) {
    fatal("Greet handler can't send factorial message, error: %d\n", ret);
  }
}

int main() {
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
