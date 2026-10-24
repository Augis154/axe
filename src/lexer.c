#include "frontend.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const Keyword keywords[];
static const size_t KEYWORD_COUNT;

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
  token.lexeme = strdup(message);
  token.lexeme_len = strlen(message);
  return token;
}

static Token number(Lexer *lex) {
  bool is_float = false;

  // TODO: Handle hex and binary literals

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
      return new_token(lex, keyword.type);
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
  case ':':
    return new_token(lex, TOKEN_COLON);
  case ';':
    return new_token(lex, TOKEN_SEMICOLON);

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

  case '=':
    return new_token(lex, TOKEN_EQUAL);

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

const char *lexer_render_token_type(TokenType type) {
  if (type < 0 || type >= TOKEN_COUNT) {
    return "Unknown";
  }
  return token_strings[type];
}

static const Keyword keywords[] = {
#define KEYWORD_TOKEN(name, token) {name, strlen(name), token},
  KEYWORDS(KEYWORD_TOKEN)
#undef KEYWORD_TOKEN
};

static const size_t KEYWORD_COUNT = sizeof(keywords) / sizeof(keywords[0]);

static const char *token_strings[TOKEN_COUNT] = {
  [TOKEN_ERROR] = "Error",
  [TOKEN_EOF] = "EOF",

  [TOKEN_PLUS] = "+",
  [TOKEN_MINUS] = "-",
  [TOKEN_STAR] = "*",
  [TOKEN_SLASH] = "/",
  [TOKEN_EQUAL] = "=",

  [TOKEN_COLON] = ":",
  [TOKEN_SEMICOLON] = ";",

  [TOKEN_OPAREN] = "(",
  [TOKEN_CPAREN] = ")",

  [TOKEN_IDENT] = "Identifier",

  [TOKEN_INT] = "Integer",
  [TOKEN_FLOAT] = "Float",
  [TOKEN_CHAR] = "Character",
  [TOKEN_STRING] = "String",

#define KEYWORD_TOKEN(name, token) [token] = name
  KEYWORDS(KEYWORD_TOKEN),
#undef KEYWORD_TOKEN
};