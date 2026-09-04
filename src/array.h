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
extern int arr_ctor(array_t* arr, size_t size, size_t max_capacity);

// Array destructor.
extern void arr_dtor(array_t* arr);

// Return a pointer to the i'th element in a given array.
extern void* arr_at(array_t* arr, size_t i);

// Append an element to the end of the array.
// Return `0` iff appended successfully, an errno-like error code otherwise.
extern int arr_append(array_t* arr, void const* act);

#endif  // ARRAY_H
