#include "lexer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const char *token_strings[TOKEN_COUNT];

static inline bool reached_end(Lexer *l) {
  return l->current >= l->source_end;
}

static inline char peek(Lexer *l) {
  if (reached_end(l)) {
    return '\0';
  }
  return *l->current;
}

static inline char advance(Lexer *l) {
  char c = *l->current++;
  if (c == '\n') {
    l->line++;
  }
  return c;
}

static inline bool is_alpha(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c == '_');
}

static inline bool is_digit(char c) {
  return c >= '0' && c <= '9';
}

static inline bool is_alnum(char c) {
  return is_alpha(c) || is_digit(c);
}

// Skips whitespace and comments
static void skip_whitespace(Lexer *lex) {
  while (1) {
    char c = peek(lex);

    switch (c) {
    // Skip whitespace
    case ' ':
    case '\n':
    case '\r':
    case '\t':
      advance(lex);
      continue;

    // Skip comments
    case '#':
      while (peek(lex) != '\n' && !reached_end(lex)) {
        advance(lex);
      }
      continue;

    default:
      return;
    }
  }
}

static void begin_token(Lexer *lex) {
  lex->token_start = lex->current;
}

static Token new_token(Lexer *lex, TokenType type) {
  Token token;
  token.type = type;
  token.line = lex->line;
  token.lexeme = lex->token_start;
  token.lexeme_len = (size_t)(lex->current - lex->token_start);
  return token;
}

static Token error_token(Lexer *lex, const char *message) {
  Token token;
  token.line = lex->line;
  token.type = TOKEN_ERROR;
  token.lexeme = message;
  token.lexeme_len = strlen(message);
  return token;
}

static Token number(Lexer *lex) {
  while (is_digit(peek(lex))) {
    advance(lex);
  }
  return new_token(lex, TOKEN_NUMBER);
}

static Token identifier(Lexer *lex) {
  while (is_alnum(peek(lex))) {
    advance(lex);
  }
  return new_token(lex, TOKEN_IDENT);
}

void lexer_init(Lexer *lex, const char *source, size_t length) {
  lex->source_start = source;
  lex->source_end = source + length;

  lex->token_start = source;
  lex->current = source;
  lex->line = 1;
}

Token lexer_get_token(Lexer *lex) {
  skip_whitespace(lex);

  begin_token(lex);

  if (reached_end(lex)) {
    return new_token(lex, TOKEN_EOF);
  }

  char c = advance(lex);

  switch (c) {
  case '(':
    return new_token(lex, TOKEN_OPAREN);
  case ')':
    return new_token(lex, TOKEN_CPAREN);

  case '+':
    return new_token(lex, TOKEN_PLUS);
  case '-':
    return new_token(lex, TOKEN_MINUS);
  case '*':
    return new_token(lex, TOKEN_STAR);
  case '/':
    return new_token(lex, TOKEN_SLASH);

  default:
    if (is_digit(c)) {
      return number(lex);
    }

    if (is_alpha(c)) {
      return identifier(lex);
    }

    break;
  }

  return error_token(lex, "Invalid token");
}

const char *lexer_render_token_type(TokenType type) {
  if (type < 0 || type >= TOKEN_COUNT) {
    return "Unknown";
  }
  return token_strings[type];
}

static const char *token_strings[TOKEN_COUNT] = {
  [TOKEN_ERROR] = "Error",
  [TOKEN_EOF] = "EOF",

  [TOKEN_PLUS] = "+",
  [TOKEN_MINUS] = "-",
  [TOKEN_STAR] = "*",
  [TOKEN_SLASH] = "/",

  [TOKEN_IDENT] = "Identifier",
  [TOKEN_NUMBER] = "Number",
  [TOKEN_CHAR] = "Character",
  [TOKEN_STRING] = "String",
};