#include "str_table.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint32_t hash_str(StrTable *st, const char *str) {
  uint32_t hash = 5381;
  int c;
  while ((c = *str++)) {
    hash = ((hash << 5) + hash) + c; // hash * 33 + c
  }
  return hash % st->size; // Table size
}

void st_init(StrTable *st, size_t size) {
  st->buckets = (STNode **)calloc(size, sizeof(STNode *));
  st->size = size;
}

void st_free(StrTable *st) {
  for (size_t i = 0; i < st->size; i++) {
    STNode *node = st->buckets[i];
    while (node) {
      STNode *next = node->next;
      free(node->key);
      free(node);
      node = next;
    }
  }
  free(st->buckets);
  st->buckets = NULL;
  st->size = 0;
}

void st_put(StrTable *st, const char *key, void *value) {
  uint32_t index = hash_str(st, key);
  STNode *new_node = (STNode *)malloc(sizeof(STNode));
  new_node->key = strdup(key);
  new_node->value = value;
  new_node->next = st->buckets[index];
  st->buckets[index] = new_node;
}

void *st_get(StrTable *st, const char *key) {
  uint32_t index = hash_str(st, key);
  STNode *node = st->buckets[index];
  while (node) {
    if (strcmp(node->key, key) == 0) {
      return node->value;
    }
    node = node->next;
  }
  return NULL; // Not found
}
