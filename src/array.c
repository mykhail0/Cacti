#include "array.h"

#include <assert.h>
#include <stdlib.h>

// Factor by which arrays' capacities grow.
static const size_t MULTIPLIER = 2;

bool arr_ctor(array_t* arr, size_t size, size_t max_capacity) {
  assert(max_capacity > 0);
  arr->MAX_CAPACITY = max_capacity;
  arr->arr = NULL;
  arr->capacity = 1;
  arr->filled = 0;
  arr->size = size;

  void* tmp = realloc(arr->arr, arr->size * arr->capacity);
  if (tmp == NULL) {
    arr->capacity = 0;
    return false;
  }

  arr->arr = tmp;
  return true;
}

bool arr_reall(array_t* arr) {
  if (arr->filled < arr->capacity) return true;
  assert(arr->capacity > 0);
  assert(arr->filled == arr->capacity);

  if (arr->capacity == arr->MAX_CAPACITY) return false;
  assert(arr->capacity < arr->MAX_CAPACITY);

  size_t previous = arr->capacity;
  arr->capacity = min(arr->capacity * MULTIPLIER, arr->MAX_CAPACITY);

  void* tmp = realloc(arr->arr, arr->size * arr->capacity);
  if (tmp == NULL) {
    arr->capacity = previous;
    return false;
  }

  arr->arr = tmp;
  return true;
}

// Clears the array.
void arr_dtor(array_t* arr) {
  arr->filled = 0;
  arr->capacity = 0;
  free(arr->arr);
  arr->arr = NULL;
}
