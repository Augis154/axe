#include "ast.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_ARENA_SIZE 1024

void ast_arena_init(AstArena *arena) {
  arena->capacity = INITIAL_ARENA_SIZE;
  arena->nodes = calloc(arena->capacity, sizeof(AstNode));

  if (arena->nodes == NULL) {
    fprintf(stderr, "ast_arena_init: failed to allocate arena\n");
    exit(EXIT_FAILURE);
  }

  arena->count = 0;

  AstId null_idx = ast_node_new(arena, AST_INVALID);
  assert(null_idx == AST_NULL_ID && "AST_NULL_ID should be 0");
}

void ast_arena_free(AstArena *arena) {
  free(arena->nodes);
  arena->nodes = NULL;
  arena->count = 0;
  arena->capacity = 0;
}

AstId ast_node_new(AstArena *arena, AstNodeType type) {
  if (arena->count >= arena->capacity) {
    arena->capacity *= 2;
    arena->nodes = realloc(arena->nodes, arena->capacity * sizeof(AstNode));

    if (arena->nodes == NULL) {
      fprintf(stderr, "ast_node_new: failed to reallocate arena\n");
      exit(EXIT_FAILURE);
    }
  }

  AstId id = arena->count++;

  AstNode *node = &arena->nodes[id];
  memset(node, 0, sizeof(AstNode));
  node->type = type;

  return id;
}
