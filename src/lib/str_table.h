#ifndef STR_TABLE_H
#define STR_TABLE_H

#include <stddef.h>
#include <stdint.h>

typedef struct STNode {
  char *key;
  void *value;
  struct STNode *next;
} STNode;

typedef struct {
  STNode **buckets;
  size_t size;
} StrTable;

void st_init(StrTable *st, size_t size);
void st_free(StrTable *st);

void st_put(StrTable *st, const char *key, void *value);
void *st_get(StrTable *st, const char *key);

#endif // STR_TABLE_H