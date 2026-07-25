#ifndef DEBUG_H
#define DEBUG_H

#include "ast.h"
#include "lexer.h"

void debug_print_token(Token token);
void debug_print_ast(AstNode *root);
void debug_print_ast_sexpr(AstNode *root);

#endif // DEBUG_H