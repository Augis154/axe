#include "sema.h"

#include "ast.h"
#include "lib/arena.h"
#include "lib/str_table.h"
#include "types.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

static void sema_error(SemaContext *ctx, SourceLoc loc, const char *message, ...) {
  ctx->comp_unit->had_error = true;

  va_list args;
  va_start(args, message);

  fprintf(stderr, "[line %zu] Error: ", loc.line);
  vfprintf(stderr, message, args);
  fprintf(stderr, "\n");

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

static Symbol *scope_insert(SemaContext *ctx, const char *name, SymbolKind kind, Type *type, SourceLoc loc) {
  const char *interned_name = st_intern(&ctx->comp_unit->intern_table, name);

  for (Symbol *sym = ctx->current_scope->symbols; sym != NULL; sym = sym->next) {
    if (sym->name == interned_name) {
      return NULL;
    }
  }

  Symbol *sym = arena_alloc(&ctx->comp_unit->arena, sizeof(Symbol));
  sym->name = interned_name;
  sym->id = ctx->next_symbol_id++;
  sym->next = ctx->current_scope->symbols;
  sym->kind = kind;
  sym->loc = loc;
  sym->type = type;

  ctx->current_scope->symbols = sym;
  return sym;
}

static Symbol *scope_lookup(SemaContext *ctx, const char *name) {
  const char *interned_name = st_intern(&ctx->comp_unit->intern_table, name);

  for (Scope *sc = ctx->current_scope; sc != NULL; sc = sc->parent) {
    for (Symbol *sym = sc->symbols; sym != NULL; sym = sym->next) {
      if (sym->name == interned_name) {
        return sym;
      }
    }
  }

  return NULL;
}

static Symbol *scope_lookup_current(SemaContext *ctx, const char *name) {
  const char *interned_name = st_intern(&ctx->comp_unit->intern_table, name);

  for (Symbol *sym = ctx->current_scope->symbols; sym != NULL; sym = sym->next) {
    if (sym->name == interned_name) {
      return sym;
    }
  }
  return NULL;
}

static Symbol *scope_insert_type(SemaContext *ctx, const char *name, Type *type, SourceLoc loc) {
  Symbol *existing = scope_lookup_current(ctx, name);
  if (existing) {
    sema_error(ctx,
               loc,
               "Redeclaration of type '%s' in current scope. Previous declaration at line %zu",
               name,
               existing->loc.line);
    return NULL;
  }

  return scope_insert(ctx, name, SYMBOL_TYPE, type, loc);
}

// Expression semantic analysis functions

static Type *sema_expr_literal(SemaContext *ctx, AstExpr *expr) {
  switch (expr->literal.type) {
  case LITERAL_INT:
    return TYPE_untyped_int;
  case LITERAL_FLOAT:
    return TYPE_untyped_float;

  case LITERAL_CHAR:
  case LITERAL_STRING:
    sema_error(ctx, expr->loc, "Literal type not yet supported in semantic analysis");
    return TYPE_error;
  }

  sema_error(ctx, expr->loc, "Unknown literal type");
  return TYPE_error;
}

static Type *sema_expr_identifier(SemaContext *ctx, AstExpr *expr) {
  Symbol *sym = scope_lookup(ctx, expr->identifier.name);
  if (sym) return sym->type;

  sema_error(ctx, expr->loc, "Undefined identifier '%s'", expr->identifier.name);

  scope_insert(ctx, expr->identifier.name, SYMBOL_LOCAL_VAR, TYPE_error, expr->loc);
  return TYPE_error;
}

static Type *sema_expr_assignment(SemaContext *ctx, AstExpr *expr) {
  Type *target_type = sema_expr(ctx, expr->assignment.target);
  Type *value_type = sema_expr(ctx, expr->assignment.value);

  if (target_type == TYPE_error || value_type == TYPE_error) {
    return TYPE_error;
  }

  if (target_type != value_type) {
    sema_error(ctx, expr->loc, "Type mismatch in assignment");
    return TYPE_error;
  }

  return target_type;
}

static Type *sema_expr_call(SemaContext *ctx, AstExpr *expr) {
  AstExpr *callee = expr->expr_call.callee;

  if (callee->kind != EXPR_IDENTIFIER) {
    sema_error(ctx, expr->loc, "Function calls must be made to identifiers");
    return TYPE_error;
  }

  Symbol *sym = scope_lookup(ctx, callee->identifier.name);

  if (!sym) {
    sema_error(ctx, expr->loc, "Undefined function '%s'", callee->identifier.name);
    return TYPE_error;
  }

  if (sym->kind != SYMBOL_FUNCTION) {
    sema_error(ctx, expr->loc, "Expected function, got %s", callee->identifier.name);
    return TYPE_error;
  }

  TypeFunction *func_type = &sym->type->function;

  if (expr->expr_call.argument_count != func_type->param_count) {
    sema_error(ctx,
               expr->loc,
               "Function '%s' expects %zu arguments, but %zu were provided",
               callee->identifier.name,
               sym->type->function.param_count,
               expr->expr_call.argument_count);
    return TYPE_error;
  }

  for (size_t i = 0; i < expr->expr_call.argument_count; i++) {
    AstExpr *arg_expr = expr->expr_call.arguments[i];
    Type *arg_type = sema_expr(ctx, arg_expr);

    if (arg_type == TYPE_error) {
      return TYPE_error;
    }

    Type *expected_type = func_type->param_types[i];

    if (arg_type != expected_type) {
      sema_error(ctx,
                 arg_expr->loc,
                 "Argument %zu of function '%s' expects type '%s', but got '%s'",
                 i + 1,
                 callee->identifier.name,
                 expected_type->name,
                 arg_type->name);
      return TYPE_error;
    }
  }

  return func_type->return_type;
}

static Type *sema_expr_unary(SemaContext *ctx, AstExpr *expr) {
  Type *operand_type = sema_expr(ctx, expr->expr_unary.operand);

  if (operand_type == TYPE_error) {
    return TYPE_error;
  }

  switch (expr->expr_unary.op) {
  case UNOP_NEGATE:
    if (!type_is_numeric(operand_type)) {
      sema_error(ctx, expr->loc, "Unary '-' operator requires numeric type");
      return TYPE_error;
    }

    return operand_type;

  case UNOP_NOT:
    if (!type_is_bool(operand_type)) {
      sema_error(ctx, expr->loc, "Unary '!' operator requires 'bool' type");
      return TYPE_error;
    }

    return operand_type;

  case UNOP_COUNT:
    sema_error(ctx, expr->loc, "Invalid unary operator");
    return TYPE_error;
  }

  sema_error(ctx, expr->loc, "Unknown unary operator");
  return TYPE_error;
}

static Type *sema_expr_binary(SemaContext *ctx, AstExpr *expr) {
  Type *left_type = sema_expr(ctx, expr->expr_binary.left);
  Type *right_type = sema_expr(ctx, expr->expr_binary.right);

  if (left_type == TYPE_error || right_type == TYPE_error) {
    return TYPE_error;
  }

  switch (expr->expr_binary.op) {
  case BINOP_ADD:
  case BINOP_SUB:
  case BINOP_MUL:
  case BINOP_DIV:
    if (!type_is_numeric(left_type) || !type_is_numeric(right_type)) {
      sema_error(ctx, expr->loc, "Arithmetic operators require numeric operands");
      return TYPE_error;
    }

    Type *common_type = get_widened_numeric_type(left_type, right_type);
    if (!common_type) {
      sema_error(
        ctx, expr->loc, "Incompatible types for arithmetic (got '%s' and '%s')", left_type->name, right_type->name);
      return TYPE_error;
    }

    ctx->expr_types[expr->expr_binary.left->id] = common_type;
    ctx->expr_types[expr->expr_binary.right->id] = common_type;

    return common_type;

  case BINOP_EQEQ:
  case BINOP_NOT_EQ: {
    if (!type_is_comparable(left_type) || !type_is_comparable(right_type)) {
      sema_error(ctx, expr->loc, "Type '%s' cannot be compared for equality", left_type->name);
      return TYPE_error;
    }

    Type *common_type = get_widened_numeric_type(left_type, right_type);
    if (!common_type) {
      sema_error(ctx, expr->loc, "Incompatible types: '%s' and '%s'", left_type->name, right_type->name);
      return TYPE_error;
    }

    ctx->expr_types[expr->expr_binary.left->id] = common_type;
    ctx->expr_types[expr->expr_binary.right->id] = common_type;

    return TYPE_bool;
  }

  case BINOP_LESS:
  case BINOP_GREATER:
  case BINOP_LESS_EQ:
  case BINOP_GREATER_EQ: {
    if (!type_is_orderable(left_type) || !type_is_orderable(right_type)) {
      sema_error(ctx, expr->loc, "Comparisons require ordered types");
      return TYPE_error;
    }

    Type *common_type = get_widened_numeric_type(left_type, right_type);
    if (!common_type) {
      sema_error(ctx, expr->loc, "Cannot compare '%s' with '%s'", left_type->name, right_type->name);
      return TYPE_error;
    }

    ctx->expr_types[expr->expr_binary.left->id] = common_type;
    ctx->expr_types[expr->expr_binary.right->id] = common_type;

    return TYPE_bool;
  }

  case BINOP_COUNT:
    sema_error(ctx, expr->loc, "Invalid binary operator");
    return TYPE_error;
  }

  sema_error(ctx, expr->loc, "Unknown binary operator");
  return TYPE_error;
}

Type *sema_expr(SemaContext *ctx, AstExpr *expr) {
  if (!expr) {
    return TYPE_void;
  }

  Type *result_type = NULL;

  switch (expr->kind) {
  case EXPR_LITERAL:
    result_type = sema_expr_literal(ctx, expr);
    break;

  case EXPR_IDENTIFIER:
    result_type = sema_expr_identifier(ctx, expr);
    break;

  case EXPR_ASSIGN:
    result_type = sema_expr_assignment(ctx, expr);
    break;
  case EXPR_CALL:
    result_type = sema_expr_call(ctx, expr);
    break;

  case EXPR_UNARY:
    result_type = sema_expr_unary(ctx, expr);
    break;
  case EXPR_BINARY:
    result_type = sema_expr_binary(ctx, expr);
    break;

  case EXPR_INVALID:
    return TYPE_error;
  }

  if (!result_type) {
    sema_error(ctx, expr->loc, "Failed to determine type for expression");
    return TYPE_error;
  }

  ctx->expr_types[expr->id] = result_type;

  return result_type;
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

  if (expected_type && init_type && !type_can_widen_to(init_type, expected_type)) {
    sema_error(ctx, stmt->loc, "Cannot implicitly convert '%s' to '%s'", init_type->name, expected_type->name);

    scope_insert(ctx, stmt->stmt_var_decl.name, SYMBOL_LOCAL_VAR, TYPE_error, stmt->loc);
    return;
  }

  if (expected_type && stmt->stmt_var_decl.init) {
    ctx->expr_types[stmt->stmt_var_decl.init->id] = expected_type;
  }

  Type *var_type = expected_type ? expected_type : init_type;
  if (!var_type) {
    sema_error(ctx, stmt->loc, "Cannot infer type without explicit type or initializer");
    var_type = TYPE_error;
  }

  Symbol *sym = scope_insert(ctx, stmt->stmt_var_decl.name, SYMBOL_LOCAL_VAR, var_type, stmt->loc);
  if (!sym) {
    sema_error(ctx, stmt->loc, "Redeclaration of '%s' in current scope", stmt->stmt_var_decl.name);
    return;
  }

  stmt->stmt_var_decl.symbol = sym;
}

static void sema_stmt_return(SemaContext *ctx, AstStmt *stmt) {
  if (!ctx->current_function) {
    sema_error(ctx, stmt->loc, "Cannot 'return' outside of a function");
    return;
  }

  Type *expected_type = ctx->current_function->symbol->type->function.return_type;
  Type *actual_type = TYPE_void;

  if (stmt->stmt_return.value) {
    actual_type = sema_expr(ctx, stmt->stmt_return.value);
  }

  if (!type_can_widen_to(actual_type, expected_type)) {
    sema_error(ctx,
               stmt->loc,
               "Function '%s' expects return type '%s', but got incompatible '%s'",
               ctx->current_function->name,
               expected_type->name,
               actual_type->name);
  }
}

static void sema_stmt_break(SemaContext *ctx, AstStmt *stmt) {
  if (ctx->loop_depth == 0) {
    sema_error(ctx, stmt->loc, "'break' statement not within a loop");
  }
}

static void sema_stmt_continue(SemaContext *ctx, AstStmt *stmt) {
  if (ctx->loop_depth == 0) {
    sema_error(ctx, stmt->loc, "'continue' statement not within a loop");
  }
}

static void sema_stmt_if(SemaContext *ctx, AstStmt *stmt) {
  Type *cond_type = sema_expr(ctx, stmt->stmt_if.condition);
  if (cond_type != TYPE_bool) {
    sema_error(ctx, stmt->stmt_if.condition->loc, "Condition in 'if' must be of type 'bool'");
  }

  sema_stmt(ctx, stmt->stmt_if.then_branch);
  if (stmt->stmt_if.else_branch) {
    sema_stmt(ctx, stmt->stmt_if.else_branch);
  }
}

static void sema_stmt_while(SemaContext *ctx, AstStmt *stmt) {
  Type *cond_type = sema_expr(ctx, stmt->stmt_while.condition);
  if (cond_type != TYPE_bool) {
    sema_error(ctx, stmt->stmt_while.condition->loc, "Condition in 'while' must be of type 'bool'");
  }

  ctx->loop_depth++;
  sema_stmt(ctx, stmt->stmt_while.body);
  ctx->loop_depth--;
}

void sema_stmt(SemaContext *ctx, AstStmt *stmt) {
  switch (stmt->kind) {
  case STMT_BLOCK:
    return sema_stmt_block(ctx, stmt);
  case STMT_EXPRESSION:
    return sema_stmt_expression(ctx, stmt);
  case STMT_VAR_DECL:
    return sema_stmt_var_decl(ctx, stmt);

  case STMT_RETURN:
    return sema_stmt_return(ctx, stmt);
  case STMT_BREAK:
    return sema_stmt_break(ctx, stmt);
  case STMT_CONTINUE:
    return sema_stmt_continue(ctx, stmt);

  case STMT_IF:
    return sema_stmt_if(ctx, stmt);
  case STMT_WHILE:
    return sema_stmt_while(ctx, stmt);

  case STMT_INVALID:
    return sema_error(ctx, stmt->loc, "Invalid statement");
  }

  sema_error(ctx, stmt->loc, "Unknown statement type");
}

// Declaration semantic analysis functions

// Pass 1: Symbol discovery

static void sema_pass1_decl_function(SemaContext *ctx, AstDecl *decl) {
  const char *func_name = decl->decl_function.name;

  Type *return_type = sema_type(ctx, decl->decl_function.return_type);
  if (return_type == TYPE_error) {
    sema_error(ctx, decl->loc, "Invalid return type for function '%s'", func_name);
    return;
  }

  uint32_t param_count = decl->decl_function.param_count;
  Type **param_types = arena_alloc(&ctx->comp_unit->arena, sizeof(Type *) * param_count);

  for (uint32_t i = 0; i < param_count; i++) {
    AstFuncParam *param = &decl->decl_function.params[i];
    Type *param_type = sema_type(ctx, param->type);

    if (param_type == TYPE_error) {
      sema_error(ctx, param->loc, "Invalid type for '%s' in function '%s'", param->name, func_name);
      return;
    }

    param_types[i] = param_type;
  }

  Type *func_type = arena_alloc(&ctx->comp_unit->arena, sizeof(Type));
  func_type->kind = TYPE_FUNCTION;

  func_type->function.return_type = return_type;
  func_type->function.param_count = decl->decl_function.param_count;
  func_type->function.param_types = param_types;

  Symbol *sym = scope_insert(ctx, func_name, SYMBOL_FUNCTION, func_type, decl->loc);
  if (!sym) {
    sema_error(ctx, decl->loc, "Redeclaration of function '%s' in current scope", func_name);
    return;
  }

  sym->type = func_type;
  decl->decl_function.symbol = sym;
}

static void sema_pass1_decl(SemaContext *ctx, AstDecl *decl) {
  switch (decl->kind) {
  case DECL_FUNCTION:
    sema_pass1_decl_function(ctx, decl);
    break;

  case DECL_INVALID:
    sema_error(ctx, decl->loc, "Invalid declaration");
    break;
  }
}

// Pass 2: Type checking and validation

static void sema_pass2_decl_function(SemaContext *ctx, AstDecl *decl) {
  if (!decl->decl_function.symbol) {
    sema_error(ctx, decl->loc, "Function '%s' has no symbol associated with it", decl->decl_function.name);
    return;
  }

  AstDeclFunction *func = &decl->decl_function;
  AstDeclFunction *prev_func = ctx->current_function;

  ctx->current_function = func;
  ctx->current_scope = scope_enter(ctx, ctx->current_scope);

  Type *func_type = func->symbol->type;

  for (uint32_t i = 0; i < func->param_count; i++) {
    AstFuncParam *param = &func->params[i];
    Type *type = func_type->function.param_types[i];
    Symbol *sym = scope_insert(ctx, param->name, SYMBOL_LOCAL_VAR, type, param->loc);

    if (!sym) {
      sema_error(ctx, param->loc, "Redeclaration of parameter '%s' in function '%s'", param->name, func->name);
      continue;
    }

    param->symbol = sym;
  }

  if (func->body) {
    sema_stmt(ctx, decl->decl_function.body);
  }

  ctx->current_scope = scope_exit(ctx->current_scope);
  ctx->current_function = prev_func;
}

void sema_pass2_decl(SemaContext *ctx, AstDecl *decl) {
  switch (decl->kind) {
  case DECL_FUNCTION: {
    return sema_pass2_decl_function(ctx, decl);
  }

  case DECL_INVALID:
    return sema_error(ctx, decl->loc, "Invalid declaration");
  }

  sema_error(ctx, decl->loc, "Unknown declaration type");
}

// Type semantic analysis functions

static Type *sema_type_named(SemaContext *ctx, AstType *type) {
  if (!type->named) {
    sema_error(ctx, type->loc, "Named type must have a name");
    return TYPE_error;
  }

  Symbol *sym = scope_lookup(ctx, type->named);
  if (!sym || sym->kind != SYMBOL_TYPE) {
    sema_error(ctx, type->loc, "Undefined type '%s'", type->named);
    return TYPE_error;
  }

  return sym->type;
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

  sema_error(ctx, type->loc, "Unknown type kind");
  return TYPE_error;
}

// Semantic analysis functions

static void sema_init_builtins(SemaContext *ctx) {
#define TYPE_DEFINE(kind, size, ident) scope_insert_type(ctx, #ident, TYPE_##ident, (SourceLoc){0});
  TYPES_BUILTIN(TYPE_DEFINE)
#undef TYPE_DEFINE
}

void sema_init(SemaContext *ctx, CompilationUnit *comp_unit) {
  ctx->comp_unit = comp_unit;
  ctx->current_scope = scope_enter(ctx, NULL);
  ctx->next_symbol_id = 1;
  ctx->loop_depth = 0;

  uint32_t expr_count = ast_expr_count();
  ctx->expr_types = arena_alloc(&comp_unit->arena, sizeof(Type *) * expr_count);
  for (size_t i = 0; i < expr_count; i++) {
    ctx->expr_types[i] = TYPE_void;
  }

  sema_init_builtins(ctx);
}

void sema_program(SemaContext *ctx, AstProgram *program) {
  for (size_t i = 0; i < program->decl_count; i++) {
    sema_pass1_decl(ctx, program->decls[i]);
  }

  if (ctx->comp_unit->had_error) {
    return;
  }

  for (size_t i = 0; i < program->decl_count; i++) {
    sema_pass2_decl(ctx, program->decls[i]);
  }
}