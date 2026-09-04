#ifndef QUEUE_H
#define QUEUE_H

#include <stdbool.h>

#include "common.h"

// Queue imlemented as a cyclic buffer, which does not exceed its max capacity.
typedef struct {
  void* arr;

  size_t MAX_CAPACITY;

  // Size of a single element.
  size_t size;

  bool empty;
  size_t capacity;

  size_t head;
  size_t tail;
} queue_t;

// Initialize an empty queue.
// Return `0` iff created successfully, an errno-like error code otherwise.
extern int que_ctor(queue_t* q, size_t size, size_t max_capacity);

// Clear the queue. Needs to be initiated again if the user intends to reuse.
extern void que_dtor(queue_t* q);

// Pushes an element onto a given queue.
// Returns `0` iff pushed successfully, an errno-like error code otherwise.
extern int que_push(queue_t* q, void const* element);

// Pops an element from the queue.
// Return `true` on success, `false` if the queue is empty.
extern bool que_pop(queue_t* q, void* element);

#endif  // QUEUE_H
