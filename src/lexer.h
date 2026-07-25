#ifndef LEXER_H
#define LEXER_H

#include <stddef.h>

typedef enum {
  TOKEN_ERROR = 0,
  TOKEN_EOF,

  TOKEN_PLUS,  // +
  TOKEN_MINUS, // -
  TOKEN_STAR,  // *
  TOKEN_SLASH, // /

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

void lexer_init(Lexer *lex, const char *source, size_t length);

Token lexer_get_token(Lexer *lex);

const char *lexer_render_token_type(TokenType type);

#endif // LEXER_H