#ifndef FRONTEND_H
#define FRONTEND_H

#include "ast.h"
#include "compiler.h"

#include <stdbool.h>
#include <stddef.h>

#define KEYWORDS(X) X("var", TOKEN_VAR)

typedef enum {
  TOKEN_ERROR = 0,
  TOKEN_EOF,

  TOKEN_PLUS,  // +
  TOKEN_MINUS, // -
  TOKEN_STAR,  // *
  TOKEN_SLASH, // /
  TOKEN_EQUAL, // =

  TOKEN_OPAREN, // (
  TOKEN_CPAREN, // )

  TOKEN_COLON,     // :
  TOKEN_SEMICOLON, // ;

  // Literals
  TOKEN_IDENT,

  // Types
  TOKEN_INT,
  TOKEN_FLOAT,
  TOKEN_CHAR,
  TOKEN_STRING,

// Keywords
#define KEYWORD_TOKEN(name, token) token
  KEYWORDS(KEYWORD_TOKEN),
#undef KEYWORD_TOKEN

  TOKEN_COUNT,
} TokenType;

typedef struct {
  TokenType type;

  size_t line;

  const char *lexeme;
  size_t lexeme_len;
} Token;

typedef struct {
  const char *name;
  size_t name_len;
  TokenType type;
} Keyword;

typedef struct {
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

const char *lexer_render_token_type(TokenType type);

AstExpr *parse_expression(Parser *parser);
AstStmt *parse_statement(Parser *parser);
AstDecl *parse_declaration(Parser *parser);

#endif // FRONTEND_H