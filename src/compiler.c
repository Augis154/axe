#include "compiler.h"

#include "lib/arena.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void cu_init(CompilationUnit *comp_unit) {
  comp_unit->filename = NULL;
  comp_unit->source = NULL;
  comp_unit->source_len = 0;

  st_init(&comp_unit->intern_table, 1024);
  st_init(&comp_unit->symbol_table, 1024);

  comp_unit->had_error = false;
}

void cu_free(CompilationUnit *comp_unit) {
  if (comp_unit->source) {
    free((void *)comp_unit->source);
    comp_unit->source = NULL;
  }

  st_free(&comp_unit->intern_table);
  st_free(&comp_unit->symbol_table);

  arena_free(&comp_unit->arena);
}

void cu_read_source_file(CompilationUnit *comp_unit, const char *filename) {
  comp_unit->filename = filename;

  FILE *file = fopen(filename, "rb");
  if (!file) {
    fprintf(stderr, "Error: Could not open file '%s'\n", filename);
    comp_unit->had_error = true;
    return;
  }

  fseek(file, 0, SEEK_END);
  size_t file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  char *buffer = (char *)malloc(file_size + 1);
  assert(buffer != NULL && "Memory allocation failed for source buffer");

  fread(buffer, 1, file_size, file);
  buffer[file_size] = '\0';

  fclose(file);

  comp_unit->source = buffer;
  comp_unit->source_len = file_size;
}