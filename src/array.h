#ifndef ARRAY_H
#define ARRAY_H

#include <stdbool.h>

#include "common.h"

// Dynamic arrays library.

typedef struct {
  void* arr;
  // Size of a single element.
  size_t size;

  size_t MAX_CAPACITY;

  size_t capacity;
  // Number of elements filling the array.
  size_t filled;
} array_t;

// Array constructor.
// Return `0` iff initialized successfully, an errno-like error code otherwise.
extern int array_create(array_t* arr, size_t size, size_t max_capacity);

// Array destructor.
extern void array_destroy(array_t* arr);

// Return a pointer to the i'th element in a given array.
extern void* array_at(array_t* arr, size_t i);

// Append an element to the end of the array.
// Return `0` iff appended successfully, an errno-like error code otherwise.
extern int array_append(array_t* arr, void const* element_ptr);

#endif  // ARRAY_H
