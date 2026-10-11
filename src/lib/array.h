#ifndef ARRAY_H
#define ARRAY_H

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_INITIAL_CAPACITY 256

#define da_head(array) ((size_t *)(array) - 2)

#define da_cap(array) ((array) ? da_head(array)[0] : 0)
#define da_len(array) ((array) ? da_head(array)[1] : 0)

#define da_reserve(array, expected)                                          \
  do {                                                                       \
    size_t cap = da_len(array);                                              \
    if (cap < (expected)) {                                                  \
      size_t new_cap = cap > 0 ? cap * 2 : ARRAY_INITIAL_CAPACITY;           \
      while (new_cap < (expected))                                           \
        new_cap *= 2;                                                        \
                                                                             \
      size_t *ptr = (array) ? ((size_t *)(array) - 2) : NULL;                \
      ptr = realloc(ptr, 2 * sizeof(size_t) + new_cap * sizeof((array)[0])); \
      assert(ptr != NULL && "Memory allocation failed");                     \
                                                                             \
      ptr[0] = new_cap;                                                      \
      if (!(array))                                                          \
        ptr[1] = 0;                                                          \
      (array) = (void *)(ptr + 2);                                           \
    }                                                                        \
  } while (0)

#define da_set_len(array, len)   \
  do {                           \
    da_reserve(array, (len));    \
    if (array)                   \
      da_head(array)[1] = (len); \
  } while (0)

#define da_append(array, element)             \
  do {                                        \
    da_reserve(array, da_len(array) + 1);     \
    (array)[da_head(array)[1]++] = (element); \
  } while (0)

#define da_append_many(array, elements, n)                                 \
  do {                                                                     \
    da_reserve(array, da_len(array) + (n));                                \
    memcpy((array) + da_len(array), (elements), (n) * sizeof((array)[0])); \
    da_head(array)[1] += (n);                                              \
  } while (0)

#define da_insert(array, index, element)                                                           \
  do {                                                                                             \
    assert(array != NULL && "Array must not be NULL");                                             \
    size_t idx = (index);                                                                          \
    assert(idx >= 0 && idx <= da_head(array)[1] && "Index out of bounds");                         \
    da_append(array, (element));                                                                   \
    memmove((array) + idx + 1, (array) + idx, sizeof((array)[0]) * (da_head(array)[1] - idx - 1)); \
    (array)[idx] = (element);                                                                      \
  } while (0)

#define da_remove_at(array, index)                                                             \
  do {                                                                                         \
    assert(array != NULL && "Array must not be NULL");                                         \
    size_t idx = (index);                                                                      \
    assert(idx >= 0 && idx <= da_head(array)[1] && "Index out of bounds");                     \
    da_head(array)[1]--;                                                                       \
    memmove((array) + idx, (array) + idx + 1, sizeof((array)[0]) * (da_head(array)[1] - idx)); \
  } while (0)

#define da_remove(array, element)                      \
  do {                                                 \
    assert(array != NULL && "Array must not be NULL"); \
    for (size_t i = 0; i < da_head(array)[1]; i++) {   \
      if ((array)[i] == (element)) {                   \
        da_remove_at(array, i);                        \
        break;                                         \
      }                                                \
    }                                                  \
  } while (0)

#define da_foreach(Type, element, array)                                                 \
  for (Type *it = (array), element = *it; it < (array) + da_len(array); element = *++it)

#define da_free(array)               \
  do {                               \
    if (array) {                     \
      free(((size_t *)(array)) - 2); \
      (array) = NULL;                \
    }                                \
  } while (0)

#endif // ARRAY_H