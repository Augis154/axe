#include "sema.h"

#include "lib/arena.h"
#include "lib/str_table.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

#define TYPE_DEFINE(kind, size, ident) static Type TYPE_##ident##_DEF = {kind, size, #ident};
TYPES_BUILTIN(TYPE_DEFINE)
#undef TYPE_DEFINE

#define TYPE_PTRS(kind, size, ident) Type *TYPE_##ident = &TYPE_##ident##_DEF;
TYPES_BUILTIN(TYPE_PTRS)
#undef TYPE_PTRS

static void sema_error(SemaContext *ctx, const char *message, ...) {
  va_list args;
  va_start(args, message);

  fprintf(stderr, "Sema error: ");
  vfprintf(stderr, message, args);
  fprintf(stderr, "\n");

  ctx->comp_unit->had_error = true;

  va_end(args);
}

// Scope management functions

static Scope *scope_enter(SemaContext *ctx, Scope *current) {
  Scope *new_scope = arena_alloc(&ctx->comp_unit->arena, sizeof(Scope));
  new_scope->parent = current;
  new_scope->symbols = NULL;
  new_scope->depth = current ? (current->depth + 1) : 0;
  return new_scope;
}

static Scope *scope_exit(Scope *current) {
  assert(current != NULL);
  return current->parent;
}

static Symbol *scope_insert(SemaContext *ctx, const char *name, SymbolKind kind, Type *type) {
  for (Symbol *sym = ctx->current_scope->symbols; sym != NULL; sym = sym->next) {
    if (sym->name == name) {
      return NULL;
    }
  }

  Symbol *sym = arena_alloc(&ctx->comp_unit->arena, sizeof(Symbol));
  sym->name = st_intern(&ctx->comp_unit->intern_table, name);
  sym->id = ctx->next_symbol_id++;
  sym->next = ctx->current_scope->symbols;
  sym->kind = kind;
  sym->type = type;

  ctx->current_scope->symbols = sym;
  return sym;
}

static Symbol *scope_lookup(Scope *current, const char *name) {
  for (Scope *sc = current; sc != NULL; sc = sc->parent) {
    for (Symbol *sym = sc->symbols; sym != NULL; sym = sym->next) {
      if (sym->name == name) {
        return sym;
      }
    }
  }

  return NULL;
}

static Symbol *scope_lookup_current(Scope *current, const char *name) {
  for (Symbol *sym = current->symbols; sym != NULL; sym = sym->next) {
    if (sym->name == name) {
      return sym;
    }
  }
  return NULL;
}

static Symbol *scope_insert_type(SemaContext *ctx, const char *name, Type *type) {
  Symbol *existing = scope_lookup_current(ctx->current_scope, name);
  if (existing) {
    sema_error(ctx, "Redeclaration of type '%s' in current scope", name);
    return NULL;
  }

  return scope_insert(ctx, name, SYMBOL_TYPE, type);
}

// Expression semantic analysis functions

static Type *sema_expr_literal(SemaContext *ctx, AstExpr *expr) {
  switch (expr->literal.type) {
  case LITERAL_INT:
    return TYPE_i64;
  case LITERAL_FLOAT:
    return TYPE_i64;

  case LITERAL_CHAR:
  case LITERAL_STRING:
    sema_error(ctx, "Literal type not yet supported in semantic analysis");
    return TYPE_void;
  }

  sema_error(ctx, "Unknown literal type");
  return TYPE_void;
}

static Type *sema_expr_identifier(SemaContext *ctx, AstExpr *expr) {
  Symbol *sym = scope_lookup(ctx->current_scope, expr->identifier.name);
  if (!sym) {
    sema_error(ctx, "Undefined identifier '%s'", expr->identifier.name);
    return TYPE_void;
  }

  return sym->type;
}

static Type *sema_expr_assignment(SemaContext *ctx, AstExpr *expr) {
  Type *target_type = sema_expr(ctx, expr->assignment.target);
  Type *value_type = sema_expr(ctx, expr->assignment.value);

  if (target_type != value_type) {
    sema_error(ctx, "Type mismatch in assignment");
    return TYPE_void;
  }

  return target_type;
}

static Type *sema_expr_unary(SemaContext *ctx, AstExpr *expr) {
  Type *operand_type = sema_expr(ctx, expr->expr_unary.operand);

  switch (expr->expr_unary.op) {
  case UNOP_NEGATE:
    if (operand_type != TYPE_i64) {
      sema_error(ctx, "Unary '-' operator requires 'int' type");
      return TYPE_void;
    }
    return operand_type;
  case UNOP_NOT:
    if (operand_type != TYPE_bool) {
      sema_error(ctx, "Unary '!' operator requires 'bool' type");
      return TYPE_void;
    }
    return operand_type;

  case UNOP_COUNT:
    sema_error(ctx, "Invalid unary operator");
    return TYPE_void;
  }

  sema_error(ctx, "Unknown unary operator");
  return TYPE_void;
}

static Type *sema_expr_binary(SemaContext *ctx, AstExpr *expr) {
  Type *left_type = sema_expr(ctx, expr->expr_binary.left);
  Type *right_type = sema_expr(ctx, expr->expr_binary.right);

  if (left_type == TYPE_void || right_type == TYPE_void) {
    return TYPE_void;
  }

  switch (expr->expr_binary.op) {
  case BINOP_ADD:
  case BINOP_SUB:
  case BINOP_MUL:
  case BINOP_DIV:
    if (left_type != TYPE_i64 || right_type != TYPE_i64) {
      sema_error(ctx, "Binary arithmetic operations require 'int' types");
      return TYPE_void;
    }
    return TYPE_i64;

  case BINOP_EQEQ:
  case BINOP_NOT_EQ:
  case BINOP_LESS:
  case BINOP_GREATER:
  case BINOP_LESS_EQ:
  case BINOP_GREATER_EQ:
    if (left_type != right_type) {
      sema_error(ctx, "Binary comparison operations require operands of the same type");
      return TYPE_void;
    }
    return TYPE_bool;

  case BINOP_COUNT:
    sema_error(ctx, "Invalid binary operator");
    return TYPE_void;
  }

  sema_error(ctx, "Unknown binary operator");
  return TYPE_void;
}

// Statement semantic analysis functions

static void sema_stmt_block(SemaContext *ctx, AstStmt *stmt) {
  ctx->current_scope = scope_enter(ctx, ctx->current_scope);

  for (size_t i = 0; i < stmt->stmt_block.count; i++) {
    sema_stmt(ctx, stmt->stmt_block.statements[i]);
  }

  ctx->current_scope = scope_exit(ctx->current_scope);
}

static void sema_stmt_expression(SemaContext *ctx, AstStmt *stmt) {
  sema_expr(ctx, stmt->stmt_expr.expression);
}

static void sema_stmt_var_decl(SemaContext *ctx, AstStmt *stmt) {
  Type *expected_type = NULL;
  if (stmt->stmt_var_decl.type) {
    expected_type = sema_type(ctx, stmt->stmt_var_decl.type);
  }

  Type *init_type = NULL;
  if (stmt->stmt_var_decl.init) {
    init_type = sema_expr(ctx, stmt->stmt_var_decl.init);
  }

  if (expected_type && init_type && expected_type != init_type) {
    sema_error(ctx, "Variable '%s' declared type does not match initializer type", stmt->stmt_var_decl.name);
    return;
  }

  Type *var_type = expected_type ? expected_type : init_type;
  if (!var_type) {
    sema_error(ctx, "Cannot infer type without explicit type or initializer");
    var_type = TYPE_void;
  }

  Symbol *sym = scope_insert(ctx, stmt->stmt_var_decl.name, SYMBOL_LOCAL_VAR, var_type);
  if (!sym) {
    sema_error(ctx, "Redeclaration of '%s' in current scope", stmt->stmt_var_decl.name);
    return;
  }
}

static void sema_stmt_if(SemaContext *ctx, AstStmt *stmt) {
  Type *cond_type = sema_expr(ctx, stmt->stmt_if.condition);
  if (cond_type != TYPE_bool) {
    sema_error(ctx, "Condition in 'if' must be of type 'bool'");
  }

  sema_stmt(ctx, stmt->stmt_if.then_branch);
  if (stmt->stmt_if.else_branch) {
    sema_stmt(ctx, stmt->stmt_if.else_branch);
  }
}

static void sema_stmt_while(SemaContext *ctx, AstStmt *stmt) {
  Type *cond_type = sema_expr(ctx, stmt->stmt_while.condition);
  if (cond_type != TYPE_bool) {
    sema_error(ctx, "Condition in 'while' must be of type 'bool'");
  }

  sema_stmt(ctx, stmt->stmt_while.body);
}

// Type semantic analysis functions

static Type *sema_type_named(SemaContext *ctx, AstType *type) {
  if (!type->named) {
    sema_error(ctx, "Named type must have a name");
    return TYPE_void;
  }

  Symbol *sym = scope_lookup(ctx->current_scope, type->named);
  if (!sym || sym->kind != SYMBOL_TYPE) {
    sema_error(ctx, "Undefined type '%s'", type->named);
    return TYPE_void;
  }

  return sym->type;
}

// Semantic analysis functions

static void sema_init_builtins(SemaContext *ctx) {
#define TYPE_DEFINE(kind, size, ident) scope_insert_type(ctx, #ident, TYPE_##ident);
  TYPES_BUILTIN(TYPE_DEFINE)
#undef TYPE_DEFINE
}

void sema_init(SemaContext *ctx, CompilationUnit *comp_unit) {
  ctx->comp_unit = comp_unit;
  ctx->current_scope = scope_enter(ctx, NULL);
  ctx->next_symbol_id = 1;

  uint32_t expr_count = ast_expr_count();
  ctx->expr_types = arena_alloc(&comp_unit->arena, sizeof(Type *) * expr_count);
  for (size_t i = 0; i < expr_count; i++) {
    ctx->expr_types[i] = TYPE_void;
  }

  sema_init_builtins(ctx);
}

Type *sema_expr(SemaContext *ctx, AstExpr *expr) {
  if (!expr) {
    return TYPE_void;
  }

  Type *result_type = NULL;

  switch (expr->type) {
  case EXPR_LITERAL:
    result_type = sema_expr_literal(ctx, expr);
    break;

  case EXPR_IDENTIFIER:
    result_type = sema_expr_identifier(ctx, expr);
    break;

  case EXPR_ASSIGN:
    result_type = sema_expr_assignment(ctx, expr);
    break;

  case EXPR_UNARY:
    result_type = sema_expr_unary(ctx, expr);
    break;
  case EXPR_BINARY:
    result_type = sema_expr_binary(ctx, expr);
    break;

  case EXPR_INVALID:
    return TYPE_void;
  }

  if (!result_type) {
    sema_error(ctx, "Failed to determine type for expression");
    return TYPE_void;
  }

  ctx->expr_types[expr->id] = result_type;

  return result_type;
}

void sema_stmt(SemaContext *ctx, AstStmt *stmt) {
  switch (stmt->type) {
  case STMT_BLOCK:
    sema_stmt_block(ctx, stmt);
    return;
  case STMT_EXPRESSION:
    sema_stmt_expression(ctx, stmt);
    return;
  case STMT_VAR_DECL:
    sema_stmt_var_decl(ctx, stmt);
    return;

  case STMT_IF:
    sema_stmt_if(ctx, stmt);
    return;
  case STMT_WHILE:
    sema_stmt_while(ctx, stmt);
    return;

  case STMT_INVALID:
    sema_error(ctx, "Invalid statement");
    return;
  }

  sema_error(ctx, "Unknown statement type");
}

Type *sema_type(SemaContext *ctx, AstType *type) {
  if (!type) {
    return TYPE_void;
  }

  switch (type->kind) {
  case TYPE_NAMED:
    return sema_type_named(ctx, type);
  case TYPE_POINTER:
    return TYPE_void;
  }

  sema_error(ctx, "Unknown type kind");
  return TYPE_void;
}