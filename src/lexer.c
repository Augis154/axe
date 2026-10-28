#include "lexer.h"
#include "token.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const char *name;
  size_t name_len;
  TokenKind kind;
} Keyword;

static const Keyword keywords[];
static const size_t KEYWORD_COUNT;

static inline bool reached_end(Lexer *l) {
  return l->current >= l->source_end;
}

static inline char peek(Lexer *l) {
  if (reached_end(l)) {
    return '\0';
  }
  return *l->current;
}

static inline char peek_next(Lexer *l) {
  if (l->current + 1 >= l->source_end) {
    return '\0';
  }
  return *(l->current + 1);
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

static inline bool is_hex_digit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
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

static Token new_token(Lexer *lex, TokenKind kind) {
  Token token;
  token.kind = kind;

  token.loc.filename = lex->filename;
  token.loc.line = lex->line;

  token.lexeme = lex->token_start;
  token.lexeme_len = (size_t)(lex->current - lex->token_start);
  return token;
}

static Token error_token(Lexer *lex, const char *message) {
  Token token;
  token.kind = TOKEN_ERROR;

  token.loc.filename = lex->filename;
  token.loc.line = lex->line;

  token.lexeme = strdup(message);
  token.lexeme_len = strlen(message);
  return token;
}

static Token number(Lexer *lex) {
  // Hex numbers
  if (lex->token_start[0] == '0' && (peek(lex) == 'x' || peek(lex) == 'X')) {
    advance(lex);

    while (is_hex_digit(peek(lex)) || peek(lex) == '_') {
      advance(lex);
    }

    return new_token(lex, TOKEN_INT);
  }

  // Binary numbers
  if (lex->token_start[0] == '0' && (peek(lex) == 'b' || peek(lex) == 'B')) {
    advance(lex);

    while (peek(lex) == '0' || peek(lex) == '1' || peek(lex) == '_') {
      advance(lex);
    }

    return new_token(lex, TOKEN_INT);
  }

  // Decimal and floating-point numbers

  bool is_float = false;

  while (is_digit(peek(lex)) || peek(lex) == '_') {
    advance(lex);
  }

  // Check for fractional part
  if (peek(lex) == '.' && is_digit(peek_next(lex))) {
    is_float = true;
    advance(lex);
    while (is_digit(peek(lex)) || peek(lex) == '_') {
      advance(lex);
    }
  }

  // Check for float suffix
  if (peek(lex) == 'f' || peek(lex) == 'F') {
    is_float = true;
    advance(lex);
  }

  return new_token(lex, is_float ? TOKEN_FLOAT : TOKEN_INT);
}

static Token identifier(Lexer *lex) {
  while (is_alnum(peek(lex))) {
    advance(lex);
  }

  char *token_start = (char *)lex->token_start;
  size_t token_length = (size_t)(lex->current - lex->token_start);

  for (size_t i = 0; i < KEYWORD_COUNT; i++) {
    const Keyword keyword = keywords[i];
    if (token_length == keyword.name_len && strncmp(token_start, keyword.name, token_length) == 0) {
      return new_token(lex, keyword.kind);
    }
  }

  return new_token(lex, TOKEN_IDENT);
}

void lexer_init(Lexer *lex, CompilationUnit *comp_unit) {
  lex->source_start = comp_unit->source;
  lex->source_end = comp_unit->source + comp_unit->source_len;

  lex->token_start = comp_unit->source;
  lex->current = comp_unit->source;
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
  case '.':
    if (peek(lex) == '*') {
      advance(lex);
      return new_token(lex, TOKEN_DEREF);
    }
    return new_token(lex, TOKEN_DOT);
  case ',':
    return new_token(lex, TOKEN_COMMA);
  case ':':
    return new_token(lex, TOKEN_COLON);
  case ';':
    return new_token(lex, TOKEN_SEMICOLON);

  case '&':
    return new_token(lex, TOKEN_AMP);

  case '(':
    return new_token(lex, TOKEN_LPAREN);
  case ')':
    return new_token(lex, TOKEN_RPAREN);
  case '{':
    return new_token(lex, TOKEN_LBRACE);
  case '}':
    return new_token(lex, TOKEN_RBRACE);

  case '+':
    return new_token(lex, TOKEN_PLUS);
  case '-':
    return new_token(lex, TOKEN_MINUS);
  case '*':
    return new_token(lex, TOKEN_STAR);
  case '/':
    return new_token(lex, TOKEN_SLASH);

  case '=':
    if (peek(lex) == '=') {
      advance(lex);
      return new_token(lex, TOKEN_EQ);
    }
    return new_token(lex, TOKEN_ASSIGN);
  case '!':
    if (peek(lex) == '=') {
      advance(lex);
      return new_token(lex, TOKEN_NOT_EQ);
    }
    return new_token(lex, TOKEN_BANG);
  case '<':
    if (peek(lex) == '=') {
      advance(lex);
      return new_token(lex, TOKEN_LESS_EQ);
    }
    return new_token(lex, TOKEN_LESS);
  case '>':
    if (peek(lex) == '=') {
      advance(lex);
      return new_token(lex, TOKEN_GREATER_EQ);
    }
    return new_token(lex, TOKEN_GREATER);

  default:
    if (is_digit(c)) {
      return number(lex);
    }

    if (is_alpha(c)) {
      return identifier(lex);
    }

    break;
  }

  char error_msg[64];
  snprintf(error_msg, sizeof(error_msg), "Unexpected character: '%c'", c);

  return error_token(lex, error_msg);
}

static const Keyword keywords[] = {
#define KEYWORD_DEF(token, name) {name, sizeof(name) - 1, token},
  TOKENS_KEYWORDS(KEYWORD_DEF)
#undef KEYWORD_DEF
};

static const size_t KEYWORD_COUNT = sizeof(keywords) / sizeof(keywords[0]);
