#ifndef TOKEN_H
#define TOKEN_H

#include "common.h"

#include <stddef.h>

#define TOKENS_SPECIAL(X) \
  X(TOKEN_ERROR, "ERROR") \
  X(TOKEN_EOF, "EOF")

#define TOKENS_OPERATORS(X) \
  X(TOKEN_PLUS, "+")        \
  X(TOKEN_MINUS, "-")       \
  X(TOKEN_STAR, "*")        \
  X(TOKEN_SLASH, "/")       \
  X(TOKEN_ASSIGN, "=")      \
  X(TOKEN_BANG, "!")        \
                            \
  X(TOKEN_LESS, "<")        \
  X(TOKEN_GREATER, ">")     \
                            \
  X(TOKEN_EQ, "==")         \
  X(TOKEN_NOT_EQ, "!=")     \
  X(TOKEN_LESS_EQ, "<=")    \
  X(TOKEN_GREATER_EQ, ">=")

#define TOKENS_SYMBOLS(X) \
  X(TOKEN_LPAREN, "(")    \
  X(TOKEN_RPAREN, ")")    \
  X(TOKEN_LBRACE, "{")    \
  X(TOKEN_RBRACE, "}")    \
                          \
  X(TOKEN_COMMA, ",")     \
  X(TOKEN_COLON, ":")     \
  X(TOKEN_SEMICOLON, ";")

#define TOKENS_LITERALS(X)     \
  X(TOKEN_IDENT, "Identifier") \
  X(TOKEN_INT, "Int")          \
  X(TOKEN_FLOAT, "Float")      \
  X(TOKEN_CHAR, "Char")        \
  X(TOKEN_STRING, "String")

#define TOKENS_KEYWORDS(X)      \
  X(TOKEN_VAR, "var")           \
  X(TOKEN_FN, "fn")             \
                                \
  X(TOKEN_RETURN, "return")     \
  X(TOKEN_BREAK, "break")       \
  X(TOKEN_CONTINUE, "continue") \
                                \
  X(TOKEN_IF, "if")             \
  X(TOKEN_ELSE, "else")         \
  X(TOKEN_WHILE, "while")       \
                                \
  X(TOKEN_AS, "as")

#define TOKEN_LIST(X) \
  TOKENS_SPECIAL(X)   \
  TOKENS_OPERATORS(X) \
  TOKENS_SYMBOLS(X)   \
  TOKENS_LITERALS(X)  \
  TOKENS_KEYWORDS(X)

typedef enum {
#define TOKEN_ENUM(token, name) token,
  TOKEN_LIST(TOKEN_ENUM)
#undef TOKEN_ENUM

    TOKEN_COUNT,
} TokenKind;

typedef struct {
  TokenKind kind;
  SourceLoc loc;

  const char *lexeme;
  size_t lexeme_len;
} Token;

#endif // TOKEN_H
