#ifndef LEXER_H
#define LEXER_H

#include "compiler.h"
#include "token.h"

#include <stddef.h>

typedef struct {
  const char *filename;

  const char *source_start;
  const char *source_end;

  const char *token_start;
  const char *current;

  size_t line;
} Lexer;

void lexer_init(Lexer *lex, CompilationUnit *comp_unit);

Token lexer_get_token(Lexer *lex);

#endif // LEXER_H
