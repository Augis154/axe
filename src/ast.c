#include "ast.h"

#include <string.h>

static uint32_t expr_id = 1;
static uint32_t stmt_id = 1;
static uint32_t decl_id = 1;
static uint32_t type_id = 1;

void ast_arena_free(Arena *arena) {
  arena_free(arena);
}

AstExpr *ast_expr_new(Arena *arena, AstExprKind kind, SourceLoc loc) {
  AstExpr *expr = arena_alloc(arena, sizeof(AstExpr));

  memset(expr, 0, sizeof(AstExpr));
  expr->kind = kind;
  expr->id = expr_id++;
  expr->loc = loc;

  return expr;
}

AstStmt *ast_stmt_new(Arena *arena, AstStmtKind kind, SourceLoc loc) {
  AstStmt *stmt = arena_alloc(arena, sizeof(AstStmt));

  memset(stmt, 0, sizeof(AstStmt));
  stmt->kind = kind;
  stmt->id = stmt_id++;
  stmt->loc = loc;

  return stmt;
}

AstDecl *ast_decl_new(Arena *arena, AstDeclKind kind, SourceLoc loc) {
  AstDecl *decl = arena_alloc(arena, sizeof(AstDecl));

  memset(decl, 0, sizeof(AstDecl));
  decl->kind = kind;
  decl->id = decl_id++;
  decl->loc = loc;

  return decl;
}

AstType *ast_type_new(Arena *arena, AstTypeKind kind, SourceLoc loc) {
  AstType *type = arena_alloc(arena, sizeof(AstType));

  memset(type, 0, sizeof(AstType));
  type->kind = kind;
  type->id = type_id++;
  type->loc = loc;

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