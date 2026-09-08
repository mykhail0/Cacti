#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../cacti.h"
#include "../error.h"

#define INCREMENTS 256
#define NPROMPTS 3

typedef struct {
  int i;
  size_t children;
} state_t;

static size_t children = 2;
static int goal;
static const int MSG_FUN = 1;
static const int MSG_DONE = 2;

static void hello_first(void** stateptr, size_t size, void* data) {
  (void)size;
  (void)data;
  assert(*stateptr == NULL);
  if (NULL == (*stateptr = malloc(sizeof(state_t)))) {
    syserr(errno, "Memory allocation failed.\n");
  }
  state_t* state_ptr = *(state_t**)stateptr;
  state_ptr->i = 0;
  state_ptr->children = children;
}

static void hello(void** stateptr, size_t size, void* data) {
  (void)size;
  (void)stateptr;
  actor_id_t self = actor_id_self(), parent = (actor_id_t)data;
  printf("Hello, I am %ld, and my parent is %ld\n", self, parent);
  message_t msg_fun = {
      .message_type = MSG_FUN, .nbytes = sizeof self, .data = (void*)self};
  for (size_t i = 0; i < INCREMENTS; ++i) {
    int ret = send_message(parent, msg_fun);
    if (ret != 0) fatal("Can't send fun message: %d.\n", ret);
  }
  int ret = send_message(parent, (message_t){.message_type = MSG_DONE});
  if (ret != 0) fatal("Can't send DONE message: %d.\n", ret);
  ret = send_message(actor_id_self(), (message_t){.message_type = MSG_GODIE});
  if (ret != 0) fatal("Can't suicide: %d.\n", ret);
}

static void fun(void** stateptr, size_t size, void* data) {
  (void)size;
  state_t* state_ptr = *(state_t**)stateptr;
  printf("%ld increments %ld: %d\n", (actor_id_t)data, actor_id_self(),
         ++(state_ptr->i));
}

static void done(void** stateptr, size_t size, void* data) {
  (void)size;
  (void)data;
  state_t* state_ptr = *(state_t**)stateptr;
  --(state_ptr->children);
  if (state_ptr->children == 0) {
    assert(goal == state_ptr->i);
    free(state_ptr);
    int ret =
        send_message(actor_id_self(), (message_t){.message_type = MSG_GODIE});
    if (ret != 0) fatal("Can't suicide: %d.\n", ret);
  }
}

int main(void) {
  act_t first_prompts[NPROMPTS] = {hello_first, fun, done};
  act_t prompts[1] = {hello};
  role_t first_role = {.nprompts = NPROMPTS, .prompts = first_prompts},
         role = {.nprompts = 1, .prompts = prompts};

  message_t msg_spawn = {
      .message_type = MSG_SPAWN, .nbytes = sizeof role, .data = &role};

  goal = INCREMENTS * children;

  actor_id_t actor_id;
  if (actor_system_create(&actor_id, &first_role) != 0) {
    fatal("System create fail.\n");
  }

  for (size_t i = 0; i < children; ++i) {
    if (send_message(actor_id, msg_spawn) != 0) {
      fatal("Could not spawn successfully %lu.\n", i);
    }
  }

  actor_system_join(actor_id);

  printf(" ========= ROUND 2 ========\n");
  if (actor_system_create(&actor_id, &first_role) != 0) {
    fatal("System create fail.\n");
  }

  for (size_t i = 0; i < children; ++i) {
    if (send_message(actor_id, msg_spawn) != 0) {
      fatal("Could not spawn successfully %lu.\n", i);
    }
  }

  actor_system_join(actor_id);

  printf(" ========= ROUND 3 ========\n");
  children = 4;
  goal = children * INCREMENTS;
  if (actor_system_create(&actor_id, &first_role) != 0) {
    fatal("System create fail.\n");
  }

  for (size_t i = 0; i < children; ++i) {
    if (send_message(actor_id, msg_spawn) != 0) {
      fatal("Could not spawn successfully %lu.\n", i);
    }
  }

  actor_system_join(actor_id);
  return 0;
}
