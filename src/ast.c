#include "ast.h"

static uint32_t expr_id = 1;
static uint32_t stmt_id = 1;
static uint32_t decl_id = 1;
static uint32_t type_id = 1;

// Node constructors

AstExpr *ast_expr_new(Arena *arena, AstExprKind kind, SourceLoc loc) {
  AstExpr *expr = arena_calloc(arena, sizeof(AstExpr));

  expr->kind = kind;
  expr->id = expr_id++;
  expr->loc = loc;

  return expr;
}

AstStmt *ast_stmt_new(Arena *arena, AstStmtKind kind, SourceLoc loc) {
  AstStmt *stmt = arena_calloc(arena, sizeof(AstStmt));

  stmt->kind = kind;
  stmt->id = stmt_id++;
  stmt->loc = loc;

  return stmt;
}

AstDecl *ast_decl_new(Arena *arena, AstDeclKind kind, SourceLoc loc) {
  AstDecl *decl = arena_calloc(arena, sizeof(AstDecl));

  decl->kind = kind;
  decl->id = decl_id++;
  decl->loc = loc;

  return decl;
}

AstType *ast_type_new(Arena *arena, AstTypeKind kind, SourceLoc loc) {
  AstType *type = arena_calloc(arena, sizeof(AstType));

  type->kind = kind;
  type->id = type_id++;
  type->loc = loc;

  return type;
}

// Node counts

uint32_t ast_expr_count(void) {
  return expr_id - 1;
}
uint32_t ast_stmt_count(void) {
  return stmt_id - 1;
}
uint32_t ast_decl_count(void) {
  return decl_id - 1;
}
uint32_t ast_type_count(void) {
  return type_id - 1;
}