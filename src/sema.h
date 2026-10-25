#ifndef SEMA_H
#define SEMA_H

#include "ast.h"
#include "common.h"
#include "compiler.h"
#include "lib/arena.h"

#include <stdint.h>

#define TYPES_BUILTIN(X)  \
  X(TYPE_ERROR, 0, error) \
  X(TYPE_VOID, 0, void)   \
  X(TYPE_I64, 8, i64)     \
  X(TYPE_F64, 8, f64)     \
  X(TYPE_BOOL, 1, bool)

typedef enum {
#define TYPE_ENUM(kind, size, ident) kind,
  TYPES_BUILTIN(TYPE_ENUM)
#undef X
} TypeKind;

typedef struct Type_ {
  TypeKind kind;
  uint32_t size;
  const char *name;
} Type;

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

  Type **type_intern_table;
  Type **expr_types;
} SemaContext;

void sema_init(SemaContext *ctx, CompilationUnit *comp_unit);

Type *sema_expr(SemaContext *ctx, AstExpr *expr);
void sema_stmt(SemaContext *ctx, AstStmt *stmt);
Type *sema_type(SemaContext *ctx, AstType *type);

#endif // SEMA_H