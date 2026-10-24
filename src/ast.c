#include "ast.h"

#include <string.h>

static uint32_t expr_id = 1;
static uint32_t stmt_id = 1;
static uint32_t decl_id = 1;
static uint32_t type_id = 1;

void ast_arena_free(Arena *arena) {
  arena_free(arena);
}

AstExpr *ast_expr_new(Arena *arena, AstExprKind type) {
  AstExpr *expr = arena_alloc(arena, sizeof(AstExpr));

  memset(expr, 0, sizeof(AstExpr));
  expr->type = type;
  expr->id = expr_id++;

  return expr;
}

AstStmt *ast_stmt_new(Arena *arena, AstStmtKind type) {
  AstStmt *stmt = arena_alloc(arena, sizeof(AstStmt));

  memset(stmt, 0, sizeof(AstStmt));
  stmt->type = type;
  stmt->id = stmt_id++;

  return stmt;
}

AstDecl *ast_decl_new(Arena *arena, AstDeclKind type) {
  AstDecl *decl = arena_alloc(arena, sizeof(AstDecl));

  memset(decl, 0, sizeof(AstDecl));
  decl->type = type;
  decl->id = decl_id++;

  return decl;
}

AstType *ast_type_new(Arena *arena, AstTypeKind kind) {
  AstType *type = arena_alloc(arena, sizeof(AstType));

  memset(type, 0, sizeof(AstType));
  type->kind = kind;
  type->id = type_id++;

  return type;
}

uint32_t ast_expr_count() {
  return expr_id - 1;
}
uint32_t ast_stmt_count() {
  return stmt_id - 1;
}
uint32_t ast_decl_count() {
  return decl_id - 1;
}
uint32_t ast_type_count() {
  return type_id - 1;
}