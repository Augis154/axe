#ifndef SEMA_H
#define SEMA_H

#include "ast.h"
#include "common.h"
#include "compiler.h"
#include "lib/arena.h"

#include <stdint.h>

typedef enum {
  SYMBOL_TYPE,
  SYMBOL_LOCAL_VAR,
  SYMBOL_GLOBAL_VAR,
} SymbolKind;

typedef struct Symbol_ {
  SymbolKind kind;
  uint32_t id;

  SourceLoc loc;

  const char *name;
  Type *type;

  struct Symbol_ *next;
} Symbol;

typedef struct Scope_ {
  struct Scope_ *parent;
  Symbol *symbols;
  uint32_t depth;
} Scope;

typedef struct {
  CompilationUnit *comp_unit;

  Scope *current_scope;
  uint32_t next_symbol_id;
  uint32_t loop_depth;

  Type **type_intern_table;
  Type **expr_types;
} SemaContext;

void sema_init(SemaContext *ctx, CompilationUnit *comp_unit);

Type *sema_expr(SemaContext *ctx, AstExpr *expr);
void sema_stmt(SemaContext *ctx, AstStmt *stmt);
Type *sema_type(SemaContext *ctx, AstType *type);

#endif // SEMA_H