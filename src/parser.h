#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "compiler.h"
#include "lexer.h"

#include <stdbool.h>

typedef struct {
  CompilationUnit *comp_unit;
  Lexer lexer;

  Token previous;
  Token current;

  bool panic_mode;
} Parser;

void parser_init(Parser *parser, CompilationUnit *comp_unit);

AstExpr *parse_expression(Parser *parser);
AstStmt *parse_statement(Parser *parser);
AstDecl *parse_declaration(Parser *parser);
AstType *parse_type(Parser *parser);

AstProgram *parse_program(Parser *parser);

#endif // PARSER_H
