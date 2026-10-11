#ifndef FRONTEND_H
#define FRONTEND_H

#include "ast.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum {
  TOKEN_ERROR = 0,
  TOKEN_EOF,

  TOKEN_PLUS,  // +
  TOKEN_MINUS, // -
  TOKEN_STAR,  // *
  TOKEN_SLASH, // /

  TOKEN_OPAREN, // (
  TOKEN_CPAREN, // )

  // Literals
  TOKEN_IDENT,

  // Types
  TOKEN_NUMBER,
  TOKEN_CHAR,
  TOKEN_STRING,

  TOKEN_COUNT,
} TokenType;

typedef struct {
  TokenType type;

  size_t line;

  const char *lexeme;
  size_t lexeme_len;
} Token;

typedef struct {
  const char *source_start;
  const char *source_end;

  const char *token_start;
  const char *current;

  size_t line;
} Lexer;

typedef struct {
  Lexer *lexer;
  Arena *ast_arena;

  Token current;
  Token next;

  bool had_error;
  bool panic_mode;
} Parser;

void lexer_init(Lexer *lex, const char *source, size_t length);
void parser_init(Parser *parser, Lexer *lexer, Arena *arena);

Token lexer_get_token(Lexer *lex);

const char *lexer_render_token_type(TokenType type);

AstExpr *parse_expression(Parser *parser);

#endif // FRONTEND_H