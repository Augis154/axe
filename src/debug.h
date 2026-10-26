#ifndef DEBUG_H
#define DEBUG_H

#include "ast.h"
#include "token.h"
#include "ir.h"

void debug_print_token(Token token);

void debug_print_ast_sexpr(Arena *arena, AstExpr *root);
void debug_print_ast_stmt(Arena *arena, AstStmt *stmt);
void debug_print_ast_decl(Arena *arena, AstDecl *decl);

void debug_print_ir_function(IrFunction *ir_func);
void debug_print_ir_module(IrModule *module);

#endif // DEBUG_H
