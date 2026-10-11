#include "ast.h"

#include <string.h>

void ast_arena_free(Arena *arena) { arena_free(arena); }

AstExpr *ast_expr_new(Arena *arena, AstNodeType type) {
  AstExpr *expr = arena_alloc(arena, sizeof(AstExpr));

  memset(expr, 0, sizeof(AstExpr));
  expr->type = type;

  return expr;
}
