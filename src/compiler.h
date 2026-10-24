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
  StrTable intern_table;
  StrTable symbol_table;

  bool had_error;
} CompilationUnit;

void cu_init(CompilationUnit *comp_unit);
void cu_free(CompilationUnit *comp_unit);

void cu_read_source_file(CompilationUnit *comp_unit, const char *filename);

#endif // COMPILER_H