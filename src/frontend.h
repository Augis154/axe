#ifndef FRONTEND_H
#define FRONTEND_H

#include "ast.h"
#include "compiler.h"

#include <stdbool.h>
#include <stddef.h>

#define TOKENS_SPECIAL(X) \
  X(TOKEN_ERROR, "ERROR") \
  X(TOKEN_EOF, "EOF")

#define TOKENS_OPERATORS(X) \
  X(TOKEN_PLUS, "+")        \
  X(TOKEN_MINUS, "-")       \
  X(TOKEN_STAR, "*")        \
  X(TOKEN_SLASH, "/")       \
  X(TOKEN_EQUAL, "=")       \
  X(TOKEN_BANG, "!")        \
                            \
  X(TOKEN_LESS, "<")        \
  X(TOKEN_GREATER, ">")     \
                            \
  X(TOKEN_EQEQ, "==")       \
  X(TOKEN_NOT_EQ, "!=")     \
  X(TOKEN_LESS_EQ, "<=")    \
  X(TOKEN_GREATER_EQ, ">=")

#define TOKENS_SYMBOLS(X) \
  X(TOKEN_OPAREN, "(")    \
  X(TOKEN_CPAREN, ")")    \
  X(TOKEN_OBRACE, "{")    \
  X(TOKEN_CBRACE, "}")    \
                          \
  X(TOKEN_COLON, ":")     \
  X(TOKEN_SEMICOLON, ";")

#define TOKENS_LITERALS(X)     \
  X(TOKEN_IDENT, "Identifier") \
  X(TOKEN_INT, "Int")          \
  X(TOKEN_FLOAT, "Float")      \
  X(TOKEN_CHAR, "Char")        \
  X(TOKEN_STRING, "String")

#define TOKENS_KEYWORDS(X) \
  X(TOKEN_VAR, "var")      \
  X(TOKEN_IF, "if")        \
  X(TOKEN_ELSE, "else")    \
  X(TOKEN_WHILE, "while")

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

typedef struct {
  const char *name;
  size_t name_len;
  TokenKind kind;
} Keyword;

typedef struct {
  const char *filename;

  const char *source_start;
  const char *source_end;

  const char *token_start;
  const char *current;

  size_t line;
} Lexer;

typedef struct {
  CompilationUnit *comp_unit;
  Lexer lexer;

  Token current;
  Token next;

  bool panic_mode;
} Parser;

void lexer_init(Lexer *lex, CompilationUnit *comp_unit);
void parser_init(Parser *parser, CompilationUnit *comp_unit);

Token lexer_get_token(Lexer *lex);

const char *lexer_render_token_kind(TokenKind kind);

AstExpr *parse_expression(Parser *parser);
AstStmt *parse_statement(Parser *parser);
AstDecl *parse_declaration(Parser *parser);
AstType *parse_type(Parser *parser);

#endif // FRONTEND_H