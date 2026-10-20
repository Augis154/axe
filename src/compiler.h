#ifndef COMPILER_H
#define COMPILER_H

#include "lib/arena.h"
#include "lib/str_table.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
  const char *filename;

  const char *source;
  size_t source_len;

  Arena ast_arena;
  StrTable symbol_table;

  bool had_error;
} CompilationUnit;

#endif // COMPILER_H