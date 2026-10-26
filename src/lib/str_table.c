#include "str_table.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static char TOMBSTONE_VAL;
#define TOMBSTONE (&TOMBSTONE_VAL)

#define INITIAL_CAPACITY 16
#define MAX_LOAD_FACTOR 0.70

// FNV-1a hash function
static inline hash_t hash_str(const char *str, size_t len) {
  hash_t hash = FNV_OFFSET;
  for (size_t i = 0; i < len; i++) {
    hash ^= (uint8_t)str[i];
    hash *= FNV_PRIME;
  }
  return hash;
}

static char *dup_str(const char *s, size_t len) {
  char *copy = (char *)malloc(len + 1);
  if (copy) {
    memcpy(copy, s, len);
    copy[len] = '\0';
  }
  return copy;
}

static void resize(StrTable *st, size_t new_capacity) {
  if (new_capacity < INITIAL_CAPACITY) {
    new_capacity = INITIAL_CAPACITY;
  }

  STNode *old_nodes = st->nodes;
  size_t old_capacity = st->capacity;

  STNode *new_nodes = (STNode *)calloc(new_capacity, sizeof(STNode));
  assert(new_nodes != NULL && "Memory allocation in resize failed");

  st->nodes = new_nodes;
  st->capacity = new_capacity;
  st->size = 0;
  st->tombstones = 0;

  for (size_t i = 0; i < old_capacity; i++) {
    STNode *node = &old_nodes[i];

    if (node->key != NULL && node->key != TOMBSTONE) {
      size_t idx = (size_t)(node->hash & (new_capacity - 1));

      while (st->nodes[idx].key != NULL) {
        idx = (idx + 1) & (new_capacity - 1);
      }

      st->nodes[idx] = *node;
      st->size++;
    }
  }

  free(old_nodes);
}

static STNode *find_entry(STNode *nodes, size_t capacity, const char *key, size_t len, hash_t hash) {
  size_t idx = (size_t)(hash & (capacity - 1));
  STNode *tombstone = NULL;

  for (size_t i = 0; i < capacity; i++) {
    STNode *node = &nodes[idx];

    if (node->key == NULL) {
      return tombstone != NULL ? tombstone : node;
    } else if (node->key == TOMBSTONE) {
      if (tombstone == NULL) {
        tombstone = node;
      }
    } else if (node->hash == hash && strncmp(node->key, key, len) == 0 && node->key[len] == '\0') {
      return node;
    }

    idx = (idx + 1) & (capacity - 1);
  }

  return tombstone;
}

static void ensure_capacity(StrTable *st) {
  if (st->capacity == 0) {
    st_init(st, INITIAL_CAPACITY);
    return;
  }

  if ((double)(st->size + st->tombstones + 1) / (double)st->capacity >= MAX_LOAD_FACTOR) {
    bool need_growth = (double)(st->size + 1) / (double)st->capacity >= MAX_LOAD_FACTOR / 2;
    resize(st, need_growth ? st->capacity * 2 : st->capacity);
  }
}

void st_init(StrTable *st, size_t initial_cap) {
  assert(st != NULL);

  size_t cap = INITIAL_CAPACITY;
  while (cap < initial_cap) {
    cap <<= 1;
  }

  st->nodes = (STNode *)calloc(cap, sizeof(STNode));
  assert(st->nodes != NULL && "Memory allocation in st_init failed");

  st->capacity = cap;
  st->size = 0;
  st->tombstones = 0;
}

void st_free(StrTable *st) {
  if (!st || !st->nodes) {
    return;
  }

  for (size_t i = 0; i < st->capacity; i++) {
    STNode *node = &st->nodes[i];
    if (node->key != NULL && node->key != TOMBSTONE) {
      free(node->key);
    }
  }

  free(st->nodes);
  st->nodes = NULL;
  st->size = 0;
  st->tombstones = 0;
  st->capacity = 0;
}

void st_set_len(StrTable *st, const char *key, size_t len, void *value) {
  if (!st || !key) {
    return;
  }

  ensure_capacity(st);

  hash_t hash = hash_str(key, len);
  STNode *node = find_entry(st->nodes, st->capacity, key, len, hash);

  if (node->key == NULL || node->key == TOMBSTONE) {
    if (node->key == TOMBSTONE) st->tombstones--;

    char *key_copy = dup_str(key, len);
    assert(key_copy != NULL && "Memory allocation in st_set_len failed");

    node->key = key_copy;
    node->hash = hash;
    st->size++;
  }

  node->value = value;
}

void *st_get_len(StrTable *st, const char *key, size_t len) {
  if (!st || !key || st->capacity == 0) {
    return NULL;
  }

  hash_t hash = hash_str(key, len);
  STNode *node = find_entry(st->nodes, st->capacity, key, len, hash);

  if (!node || node->key == NULL || node->key == TOMBSTONE) {
    return NULL;
  }

  return node->value;
}

void *st_remove_len(StrTable *st, const char *key, size_t len) {
  if (!st || !key || st->capacity == 0) {
    return NULL;
  }

  hash_t hash = hash_str(key, len);
  STNode *node = find_entry(st->nodes, st->capacity, key, len, hash);

  if (!node || node->key == NULL || node->key == TOMBSTONE) {
    return NULL;
  }

  void *value = node->value;

  free(node->key);
  node->key = TOMBSTONE;
  node->value = NULL;
  st->size--;
  st->tombstones++;

  return value;
}

const char *st_intern_len(StrTable *st, const char *key, size_t len) {
  if (!st || !key) {
    return NULL;
  }

  ensure_capacity(st);

  hash_t hash = hash_str(key, len);
  STNode *node = find_entry(st->nodes, st->capacity, key, len, hash);

  if (node->key != NULL && node->key != TOMBSTONE) {
    return node->key;
  }

  if (node->key == TOMBSTONE) st->tombstones--;

  char *interned = dup_str(key, len);
  assert(interned != NULL && "Memory allocation in st_intern_len failed");

  node->key = interned;
  node->hash = hash;
  node->value = interned;
  st->size++;

  return interned;
}