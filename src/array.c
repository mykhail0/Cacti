#include "array.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "errno.h"

// Factor by which arrays' capacities grow.
static const size_t MULTIPLIER = 2;

static inline void* get_element_ptr(array_t* q, size_t i) {
  return ((unsigned char*)q->arr) + i * q->size;
}

int arr_ctor(array_t* arr, size_t size, size_t max_capacity) {
  assert(max_capacity > 0);
  arr->MAX_CAPACITY = max_capacity;
  arr->arr = NULL;
  arr->capacity = 1;
  arr->filled = 0;
  arr->size = size;

  void* tmp = calloc(arr->capacity, arr->size);
  if (tmp == NULL) {
    arr->capacity = 0;
    return errno;
  }

  arr->arr = tmp;
  return 0;
}

void arr_dtor(array_t* arr) {
  arr->filled = 0;
  arr->capacity = 0;
  free(arr->arr);
  arr->arr = NULL;
}

// Reallocate the array if the size approached the not maxxed out capacity.
// Return `0` iff reallocated successfully or no reallocation needed, an
// errno-like error code otherwise.
static int arr_reall(array_t* arr) {
  if (arr->filled < arr->capacity) return 0;
  assert(arr->capacity > 0);
  assert(arr->filled == arr->capacity);

  if (arr->capacity == arr->MAX_CAPACITY) return EAGAIN;
  assert(arr->capacity < arr->MAX_CAPACITY);

  size_t previous = arr->capacity;
  arr->capacity = min(arr->capacity * MULTIPLIER, arr->MAX_CAPACITY);

  void* tmp = realloc(arr->arr, arr->size * arr->capacity);
  if (tmp == NULL) {
    arr->capacity = previous;
    return errno;
  }

  arr->arr = tmp;
  return 0;
}

void* arr_at(array_t* arr, size_t i) {
  return i < arr->filled ? get_element_ptr(arr, i) : NULL;
}

int arr_append(array_t* arr, void const* element_ptr) {
  int ret = arr_reall(arr);
  if (ret != 0) return ret;
  memcpy(get_element_ptr(arr, arr->filled), element_ptr, arr->size);
  ++(arr->filled);
  return 0;
}
