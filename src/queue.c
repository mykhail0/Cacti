#include "queue.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

// Factor by which the cyclic buffer grows.
static const size_t MULTIPLIER = 2;

static inline void* get_element_ptr(queue_t* q, size_t i) {
  return ((unsigned char*)q->arr) + i * q->size;
}

int que_ctor(queue_t* q, size_t size, size_t max_capacity) {
  assert(max_capacity > 0);
  assert(size > 0);
  q->MAX_CAPACITY = max_capacity;
  q->empty = true;
  q->capacity = 1;
  q->head = 0;
  q->tail = 0;
  q->size = size;
  q->arr = NULL;

  void* tmp = malloc(q->size);
  if (tmp == NULL) {
    q->capacity = 0;
    return errno;
  }

  q->arr = tmp;
  return 0;
}

void que_dtor(queue_t* q) {
  free(q->arr);
  q->arr = NULL;
  q->empty = true;
  q->head = 0;
  q->tail = 0;
  q->capacity = 0;
}

// Reallocates the queues's cyclic buffer if needed.
// Returns `0` iff the reallocation was successful or was not needed, an
// errno-like error code otherwise.
static bool que_reall(queue_t* q) {
  if (q->empty || q->head != q->tail) return true;
  if (q->capacity == q->MAX_CAPACITY) return false;

  size_t old_capacity = q->capacity;
  q->capacity = min(old_capacity * MULTIPLIER, q->MAX_CAPACITY);

  void* tmp = realloc(q->arr, q->size * q->capacity);
  if (tmp == NULL) {
    q->capacity = old_capacity;
    return errno;
  }

  q->arr = tmp;
  if (q->head == 0) {
    q->tail = old_capacity;
  } else {
    // Make free space not at the end of the buffer but between the tail and the
    // head.
    size_t begin_len = old_capacity - q->head;
    q->head = q->capacity - begin_len;
    // Old head location is at q->tail.
    memmove(get_element_ptr(q, q->head), get_element_ptr(q, q->tail),
            begin_len * q->size);
  }
  return 0;
}

int que_push(queue_t* q, void const* element) {
  int ret = que_reall(q);
  if (ret != 0) return ret;
  memcpy(get_element_ptr(q, q->tail), element, q->size);
  q->tail = (q->tail + 1) % q->capacity;
  q->empty = false;
  return 0;
}

bool que_pop(queue_t* q, void* element) {
  if (q->empty) return false;
  memcpy(element, get_element_ptr(q, q->head), q->size);
  q->head = (q->head + 1) % q->capacity;
  q->empty = q->head == q->tail;
  return true;
}
