#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "lexer.h"

#include <stdbool.h>

typedef struct {
  Lexer *lexer;
  AstArena *ast_arena;

  Token current;
  Token next;

  bool had_error;
  bool panic_mode;
} Parser;

void parser_init(Parser *parser, Lexer *lexer, AstArena *arena);

AstId parse_expression(Parser *parser);

#endif // PARSER_H
