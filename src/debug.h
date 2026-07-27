#ifndef DEBUG_H
#define DEBUG_H

#include "ast.h"
#include "ir.h"
#include "lexer.h"

void debug_print_token(Token token);

void debug_print_ast_tree(AstArena *arena, AstId root);
void debug_print_ast_sexpr(AstArena *arena, AstId root);

void debug_print_ir_buffer(IrBuffer *buffer);

#endif // DEBUG_H
