#ifndef ARENA_H
#define ARENA_H

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

#define ARENA_BLOCK_SIZE 65536 // 64KB chunks

typedef struct ArenaBlock {
  struct ArenaBlock *next;
  size_t used;
  size_t capacity;

  alignas(max_align_t) unsigned char data[];
} ArenaBlock;

typedef struct {
  ArenaBlock *block;
  size_t used;
} ArenaMark;

typedef struct {
  ArenaBlock *first;
  ArenaBlock *current;
} Arena;

void *arena_alloc(Arena *a, size_t size);
void arena_free(Arena *a);

ArenaMark arena_mark(Arena *a);
void arena_rollback(Arena *a, ArenaMark mark);

char *arena_strdup(Arena *a, const char *str);
char *arena_strndup(Arena *a, const char *str, size_t len);

#endif // ARENA_H