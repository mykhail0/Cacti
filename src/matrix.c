#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "array.h"
#include "cacti.h"
#include "error.h"

/*
# Input format

The matrix program loads 2 numbers, `k` and `n` from the standard input, each in
a separate line. These numbers mean the number of rows and columns of a given
matrix respectively. Next, the program takes `k * n` lines of input, each
consisting of 2 numbers separated by a single space `v t`, where `v` is the next
entry in the matrix (traversing rows left to right, going row by row) and `t` is
the time in milliseconds required to calculate `v`. For example for inputs in
test/matrix/matrix.in, the matrix should look the following way:

|  1 | 1 | 12
| 23 | 3 | 7

# Computation

The program uses an actor system with the number of actors equal to the number
of columns in the matrix. Each actor will be responsible for values in a single
column assigned to it. Actors receive messages with a row number and a so far
accumulated sum of that row, then calculate the number in their column in that
row. Next, they add the number to the sum and pass it along to the next agent.

# Output format

The program prints `k` lines, each containing a single number which is the sum
of numbers in that matrix' row. For inputs in test/test_cases/matrix.in the
output is in test/matrix/matrix.out.
*/

#define NPROMPTS 2

static void hello_handler(void** stateptr, size_t nbytes, void* data);
static void pass_handler(void** stateptr, size_t nbytes, void* data);
static void ready_handler(void** stateptr, size_t nbytes, void* data);

static const size_t MSG_PASS = 1;
static const size_t MSG_READY = 1;

typedef struct {
  unsigned t;
  int v;
} cell_t;

static size_t actors;
// Elements are of array_t<cell_t> type.
static array_t matrix;
// Elements are of type int.
static array_t row_sums;
static array_t messages;

typedef struct {
  size_t row;
  int sum;
} message_data_t;

static void suicide() {
  int ret =
      send_message(actor_id_self(), (message_t){.message_type = MSG_GODIE});
  if (ret != 0) fatal("Couldn't suicide: %d.\n", ret);
}

void hello_handler(void** stateptr, size_t nbytes, void* data) {
  assert(nbytes == sizeof(actor_id_t));
  assert(*stateptr == NULL);
  if (NULL == (*stateptr = malloc(sizeof(size_t)))) {
    syserr(errno, "Couldn't malloc actor's state.\n");
  }
  int* state = *(int**)stateptr;
  *state = 0;
  int ret =
      send_message((actor_id_t)data, (message_t){.message_type = MSG_READY});
  if (ret != 0) {
    if (actor_id_self() == 0) {
      assert(ret == UNKNOWN_ACTOR);
      return;
    }
    fatal("Couldn't send MSG_READY: %d.\n", ret);
  }
}

void pass_handler(void** stateptr, size_t nbytes, void* data) {
  assert(nbytes == sizeof(message_data_t));
  message_data_t* msg = data;
  array_t* row = array_at(&matrix, msg->row);
  actor_id_t self_id = actor_id_self();
  cell_t* cell = array_at(row, self_id - 1);
  int ret = sleep(cell->t);
  assert(ret == 0);
  msg->sum += cell->v;
  if ((size_t)self_id == actors) {
    int* ptr = array_at(&row_sums, msg->row);
    *ptr = msg->sum;
  } else {
    ret = send_message(
        self_id + 1,
        (message_t){.message_type = MSG_PASS, .nbytes = nbytes, .data = data});
    if (ret != 0) fatal("Couldn't pass the sum along: %d.\n", ret);
  }
  size_t* processed_rows = *(size_t**)stateptr;
  ++(*processed_rows);
  if (*processed_rows == matrix.filled) {
    free(processed_rows);
    suicide();
  }
}

void ready_handler(void** stateptr, size_t nbytes, void* data) {
  (void)data;
  (void)nbytes;
  int* ready = *(int**)stateptr;
  ++(*ready);
  if ((size_t)*ready == actors) {
    suicide();
    for (size_t i = 0; i < matrix.filled; ++i) {
      int ret = send_message(1, (message_t){.message_type = MSG_PASS,
                                            .nbytes = sizeof(message_data_t),
                                            .data = array_at(&messages, i)});
      if (ret != 0) fatal("Couldn't send initial MSG_PASS: %d.\n", ret);
    }
  }
}

int main(void) {
  size_t k;
  scanf("%lu", &k);
  scanf("%lu", &actors);
  int ret = array_create(&matrix, sizeof(array_t), k);
  if (ret != 0) syserr(ret, "Failed array_create.\n");
  for (size_t i = 0; i < k; ++i) {
    array_t row;
    ret = array_create(&row, sizeof(cell_t), actors);
    if (ret != 0) syserr(ret, "Failed array_create.\n");
    for (size_t j = 0; j < actors; ++j) {
      cell_t cell;
      scanf("%d", &(cell.v));
      scanf("%u", &(cell.t));
      ret = array_append(&row, &cell);
      if (ret != 0) syserr(ret, "Failed array_append.\n");
    }
    ret = array_append(&matrix, &row);
    if (ret != 0) syserr(ret, "Failed array_append.\n");
  }

  ret = array_create(&messages, sizeof(message_data_t), matrix.MAX_CAPACITY);
  if (ret != 0) syserr(ret, "Failed array_create.\n");
  ret = array_create(&row_sums, sizeof(int), matrix.MAX_CAPACITY);
  if (ret != 0) syserr(ret, "Failed array_create.\n");
  for (size_t i = 0; i < matrix.filled; ++i) {
    message_data_t msg = {.row = i, .sum = 0};
    ret = array_append(&messages, &msg);
    if (ret != 0) syserr(ret, "Failed array_append.\n");
    int sum = 0;
    ret = array_append(&row_sums, &sum);
    if (ret != 0) syserr(ret, "Failed array_append.\n");
  }

  actor_id_t spawner;
  act_t spawner_prompts[NPROMPTS] = {hello_handler, ready_handler};
  act_t prompts[NPROMPTS] = {hello_handler, pass_handler};
  role_t spawner_role = {.nprompts = NPROMPTS, .prompts = spawner_prompts},
         role = {.nprompts = NPROMPTS, .prompts = prompts};
  ret = actor_system_create(&spawner, &spawner_role);
  if (ret != 0) fatal("Failed actor_system_create: %d.\n", ret);

  for (size_t i = 0; i < actors; ++i) {
    ret = send_message(spawner, (message_t){.message_type = MSG_SPAWN,
                                            .nbytes = sizeof role,
                                            .data = &role});
    if (ret != 0) fatal("Couldn't send MSG_SPAWN: %d.\n", ret);
  }

  actor_system_join(spawner);
  for (size_t i = 0; i < row_sums.filled; ++i) {
    printf("%d\n", *(int*)array_at(&row_sums, i));
  }
  return 0;
}
