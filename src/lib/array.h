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

#define da_reserve(array, expected)                                                      \
  do {                                                                                   \
    size_t da__cap = da_cap(array);                                                      \
    size_t da__want = (expected);                                                        \
    if (da__cap < da__want) {                                                            \
      size_t da__new_cap = da__cap > 0 ? da__cap * 2 : ARRAY_INITIAL_CAPACITY;           \
      while (da__new_cap < da__want)                                                     \
        da__new_cap *= 2;                                                                \
                                                                                         \
      size_t *da__ptr = (array) ? da_head(array) : NULL;                                 \
      da__ptr = realloc(da__ptr, 2 * sizeof(size_t) + da__new_cap * sizeof((array)[0])); \
      assert(da__ptr != NULL && "Memory allocation failed");                             \
                                                                                         \
      da__ptr[0] = da__new_cap;                                                          \
      if (!(array)) da__ptr[1] = 0;                                                      \
      (array) = (void *)(da__ptr + 2);                                                   \
    }                                                                                    \
  } while (0)

#define da_set_len(array, len)            \
  do {                                    \
    da_reserve(array, (len));             \
    if (array) da_head(array)[1] = (len); \
  } while (0)

#define da_append(array, element)             \
  do {                                        \
    da_reserve(array, da_len(array) + 1);     \
    (array)[da_head(array)[1]++] = (element); \
  } while (0)

#define da_append_many(array, elements, n)                                     \
  do {                                                                         \
    size_t da__n = (n);                                                        \
    da_reserve(array, da_len(array) + da__n);                                  \
    if (da__n > 0) {                                                           \
      memcpy((array) + da_len(array), (elements), da__n * sizeof((array)[0])); \
      da_head(array)[1] += da__n;                                              \
    }                                                                          \
  } while (0)

#define da_insert(array, index, element)                                                          \
  do {                                                                                            \
    size_t da__idx = (index);                                                                     \
    assert(da__idx <= da_len(array) && "Index out of bounds");                                    \
    da_append(array, (element));                                                                  \
    size_t da__last = da_len(array) - 1;                                                          \
    unsigned char da__tmp[sizeof((array)[0])];                                                    \
    memcpy(da__tmp, &(array)[da__last], sizeof da__tmp);                                          \
    memmove(&(array)[da__idx + 1], &(array)[da__idx], sizeof((array)[0]) * (da__last - da__idx)); \
    memcpy(&(array)[da__idx], da__tmp, sizeof da__tmp);                                           \
  } while (0)

#define da_remove_at(array, index)                                                                     \
  do {                                                                                                 \
    size_t da__idx = (index);                                                                          \
    assert(da__idx < da_len(array) && "Index out of bounds");                                          \
    da_head(array)[1]--;                                                                               \
    memmove((array) + da__idx, (array) + da__idx + 1, sizeof((array)[0]) * (da_len(array) - da__idx)); \
  } while (0)

#define da_remove(array, element)                            \
  do {                                                       \
    for (size_t da__i = 0; da__i < da_len(array); da__i++) { \
      if ((array)[da__i] == (element)) {                     \
        da_remove_at(array, da__i);                          \
        break;                                               \
      }                                                      \
    }                                                        \
  } while (0)

#define da_foreach(Type, it, array) for (Type *it = (array); it && it < (array) + da_len(array); it++)

#define da_free(array)               \
  do {                               \
    if (array) {                     \
      free(((size_t *)(array)) - 2); \
      (array) = NULL;                \
    }                                \
  } while (0)

#endif // ARRAY_H