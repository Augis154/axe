#ifndef STR_TABLE_H
#define STR_TABLE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Use 32-bit or 64-bit FNV-1a hash based on the platform
#if UINTPTR_MAX > 0xFFFFFFFF
typedef uint64_t hash_t;
#define FNV_OFFSET 14695981039346656037ULL
#define FNV_PRIME 1099511628211ULL
#else
typedef uint32_t hash_t;
#define FNV_OFFSET 2166136261U
#define FNV_PRIME 16777619U
#endif

typedef struct STNode {
  char *key;
  hash_t hash;
  void *value;
} STNode;

typedef struct {
  STNode *nodes;
  size_t size;
  size_t tombstones;
  size_t capacity;
} StrTable;

void st_init(StrTable *st, size_t size);
void st_free(StrTable *st);

void st_set_len(StrTable *st, const char *key, size_t len, void *value);
void *st_get_len(StrTable *st, const char *key, size_t len);
void *st_remove_len(StrTable *st, const char *key, size_t len);
const char *st_intern_len(StrTable *st, const char *key, size_t len);

static inline void st_set(StrTable *st, const char *key, void *value) {
  key ? st_set_len(st, key, strlen(key), value) : (void)0;
}
static inline void *st_get(StrTable *st, const char *key) {
  return key ? st_get_len(st, key, strlen(key)) : NULL;
}
static inline void *st_remove(StrTable *st, const char *key) {
  return key ? st_remove_len(st, key, strlen(key)) : NULL;
}
static inline const char *st_intern(StrTable *st, const char *key) {
  return key ? st_intern_len(st, key, strlen(key)) : NULL;
}

#endif // STR_TABLE_H