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

// Array constructor. Return `true` iff initialized successfully.
extern bool arr_ctor(array_t* arr, size_t size, size_t max_capacity);

// Reallocates the array if the size approached the not maxxed out capacity.
// Return `true` iff reallocated successfully or no reallocation needed.
extern bool arr_reall(array_t* arr);

// Array destructor.
extern void arr_dtor(array_t* arr);

#endif  // ARRAY_H
