#include "sema.h"

#include "ast.h"
#include "lib/arena.h"
#include "lib/array.h"
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

// Allocates a symbol; `name` must be interned. It is not added to any scope.
static Symbol *symbol_new(SemaContext *ctx, const char *name, SymbolKind kind, Type *type, SourceLoc loc) {
  Symbol *sym = arena_alloc(&ctx->comp_unit->arena, sizeof(Symbol));
  *sym = (Symbol){
    .kind = kind,
    .id = ctx->next_symbol_id++,
    .loc = loc,
    .name = name,
    .type = type,
  };
  return sym;
}

// Declares `name` in the current scope; returns NULL if it is already declared there.
static Symbol *scope_insert(SemaContext *ctx, const char *name, SymbolKind kind, Type *type, SourceLoc loc) {
  const char *interned_name = st_intern(&ctx->comp_unit->intern_table, name);

  for (Symbol *sym = ctx->current_scope->symbols; sym != NULL; sym = sym->next) {
    if (sym->name == interned_name) {
      return NULL;
    }
  }

  Symbol *sym = symbol_new(ctx, interned_name, kind, type, loc);
  sym->next = ctx->current_scope->symbols;
  ctx->current_scope->symbols = sym;

  return sym;
}

// Finds `name` in the current scope or any enclosing one (names are interned, so pointers are compared).
static Symbol *scope_lookup(SemaContext *ctx, const char *name) {
  for (Scope *sc = ctx->current_scope; sc != NULL; sc = sc->parent) {
    for (Symbol *sym = sc->symbols; sym != NULL; sym = sym->next) {
      if (sym->name == name) {
        return sym;
      }
    }
  }

  return NULL;
}

// Finds the symbol a type name refers to.
static Symbol *scope_lookup_type_name(SemaContext *ctx, const char *name) {
  for (Scope *sc = ctx->current_scope; sc != NULL; sc = sc->parent) {
    for (Symbol *sym = sc->symbols; sym != NULL; sym = sym->next) {
      if (sym->name == name && (sym->kind == SYMBOL_TYPE || sym->type != TYPE_error)) {
        return sym;
      }
    }
  }

  return NULL;
}

static Symbol *scope_lookup_current(SemaContext *ctx, const char *name) {
  for (Symbol *sym = ctx->current_scope->symbols; sym != NULL; sym = sym->next) {
    if (sym->name == name) {
      return sym;
    }
  }
  return NULL;
}

static Symbol *scope_insert_type(SemaContext *ctx, const char *name, Type *type, SourceLoc loc) {
  name = st_intern(&ctx->comp_unit->intern_table, name);

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

// Expression types

Type *sema_expr_type(const SemaContext *ctx, const AstExpr *expr) {
  return expr->id < da_len(ctx->expr_types) ? ctx->expr_types[expr->id] : NULL;
}

// Nodes created during sema (implicit casts) have ids past the table, so it grows on demand.
static void sema_set_expr_type(SemaContext *ctx, const AstExpr *expr, Type *type) {
  size_t old_len = da_len(ctx->expr_types);

  if (expr->id >= old_len) {
    da_set_len(ctx->expr_types, expr->id + 1);
    for (size_t i = old_len; i < da_len(ctx->expr_types); i++) {
      ctx->expr_types[i] = NULL;
    }
  }

  ctx->expr_types[expr->id] = type;
}

// Implicit conversions

// Gives an untyped expression (and the untyped operands it is built from) a concrete type.
static void sema_concretize(SemaContext *ctx, AstExpr *expr, Type *target) {
  if (!type_is_untyped(sema_expr_type(ctx, expr))) {
    return;
  }

  sema_set_expr_type(ctx, expr, target);

  switch (expr->kind) {
  case AST_EXPR_UNARY:
    sema_concretize(ctx, expr->expr_unary.operand, target);
    break;

  case AST_EXPR_BINARY:
    sema_concretize(ctx, expr->expr_binary.left, target);
    sema_concretize(ctx, expr->expr_binary.right, target);
    break;

  default:
    break;
  }
}

// Makes `*slot` usable as `to`: untyped expressions take the concrete type, others get an implicit cast node.
static void sema_coerce(SemaContext *ctx, AstExpr **slot, Type *to) {
  AstExpr *expr = *slot;
  Type *from = sema_expr_type(ctx, expr);

  if (from == TYPE_error || to == TYPE_error) {
    return;
  }

  if (type_is_untyped(from)) {
    sema_concretize(ctx, expr, type_default(to));
    return;
  }

  if (from != to) {
    AstExpr *cast = ast_expr_new(&ctx->comp_unit->arena, AST_EXPR_CAST, expr->loc);
    cast->expr_cast.operand = expr;
    cast->expr_cast.implicit = true;
    sema_set_expr_type(ctx, cast, to);

    *slot = cast;
  }
}

// Expression semantic analysis functions

// Reports an error if `name` is a type name.
static bool sema_reject_type_name(SemaContext *ctx, const char *name, SourceLoc loc) {
  Symbol *existing = scope_lookup(ctx, name);
  if (!existing || existing->kind != SYMBOL_TYPE) {
    return false;
  }

  sema_error(ctx, loc, "'%s' is a type name and cannot be used as a variable name", name);
  return true;
}

// Types a literal: ints are untyped_int and floats untyped_float; char and string are not supported yet.
static Type *sema_expr_literal(SemaContext *ctx, AstExpr *expr) {
  switch (expr->expr_literal.kind) {
  case AST_LITERAL_INT:
    return TYPE_untyped_int;
  case AST_LITERAL_FLOAT:
    return TYPE_untyped_float;

  case AST_LITERAL_CHAR:
  case AST_LITERAL_STRING:
    sema_error(ctx, expr->loc, "Literal type not yet supported in semantic analysis");
    return TYPE_error;
  }

  sema_error(ctx, expr->loc, "Unknown literal type");
  return TYPE_error;
}

static Type *sema_expr_identifier(SemaContext *ctx, AstExpr *expr) {
  Symbol *sym = scope_lookup(ctx, expr->expr_identifier.name);
  if (!sym) {
    sema_error(ctx, expr->loc, "Undefined identifier '%s'", expr->expr_identifier.name);

    scope_insert(ctx, expr->expr_identifier.name, SYMBOL_LOCAL_VAR, TYPE_error, expr->loc);
    return TYPE_error;
  }

  if (sym->kind == SYMBOL_TYPE) {
    sema_error(ctx, expr->loc, "'%s' is a type, not a value", expr->expr_identifier.name);
    return TYPE_error;
  }

  if (sym->kind == SYMBOL_FUNCTION) {
    sema_error(
      ctx, expr->loc, "'%s' is a function, not a value (did you mean to call it?)", expr->expr_identifier.name);
    return TYPE_error;
  }

  expr->expr_identifier.symbol = sym;
  return sym->type;
}

static Type *sema_expr_assignment(SemaContext *ctx, AstExpr *expr) {
  AstExpr *target = expr->expr_assign.target;

  if (target->kind == AST_EXPR_IDENTIFIER) {
    Symbol *sym = scope_lookup(ctx, target->expr_identifier.name);

    if (sym && sym->kind != SYMBOL_LOCAL_VAR && sym->kind != SYMBOL_GLOBAL_VAR) {
      sema_expr(ctx, expr->expr_assign.value);
      sema_error(ctx,
                 expr->loc,
                 "Invalid assignment target: '%s' is %s, not a variable",
                 target->expr_identifier.name,
                 sym->kind == SYMBOL_TYPE ? "a type" : "a function");
      return TYPE_error;
    }
  }

  Type *target_type = sema_expr(ctx, target);
  Type *value_type = sema_expr(ctx, expr->expr_assign.value);

  if (target_type == TYPE_error || value_type == TYPE_error) {
    return TYPE_error;
  }

  Symbol *target_sym = expr->expr_assign.target->expr_identifier.symbol;
  if (!target_sym || (target_sym->kind != SYMBOL_LOCAL_VAR && target_sym->kind != SYMBOL_GLOBAL_VAR)) {
    sema_error(ctx, expr->loc, "Invalid assignment target");
    return TYPE_error;
  }

  if (!type_can_widen_to(value_type, target_type)) {
    sema_error(ctx, expr->loc, "Cannot assign '%s' to '%s'", value_type->name, target_type->name);
    return TYPE_error;
  }

  sema_coerce(ctx, &expr->expr_assign.value, target_type);

  return target_type;
}

// Analyses the arguments of a call that could not be resolved, so that errors inside them are still reported.
static void sema_call_arguments(SemaContext *ctx, AstExpr *expr) {
  for (size_t i = 0; i < expr->expr_call.argument_count; i++) {
    sema_expr(ctx, expr->expr_call.arguments[i]);
  }
}

static Type *sema_expr_call(SemaContext *ctx, AstExpr *expr) {
  AstExpr *callee = expr->expr_call.callee;

  if (callee->kind != AST_EXPR_IDENTIFIER) {
    sema_error(ctx, expr->loc, "Function calls must be made to identifiers");
    sema_call_arguments(ctx, expr);
    return TYPE_error;
  }

  Symbol *sym = scope_lookup(ctx, callee->expr_identifier.name);

  if (!sym) {
    sema_error(ctx, expr->loc, "Undefined function '%s'", callee->expr_identifier.name);
    sema_call_arguments(ctx, expr);
    return TYPE_error;
  }

  if (sym->kind != SYMBOL_FUNCTION) {
    sema_error(ctx, expr->loc, "Expected function, got %s", callee->expr_identifier.name);
    sema_call_arguments(ctx, expr);
    return TYPE_error;
  }

  TypeFunction *func_type = &sym->type->function;

  if (expr->expr_call.argument_count != func_type->param_count) {
    sema_error(ctx,
               expr->loc,
               "Function '%s' expects %zu arguments, but %zu were provided",
               callee->expr_identifier.name,
               sym->type->function.param_count,
               expr->expr_call.argument_count);
    sema_call_arguments(ctx, expr);
    return TYPE_error;
  }

  bool arguments_ok = true;

  for (size_t i = 0; i < expr->expr_call.argument_count; i++) {
    AstExpr *arg_expr = expr->expr_call.arguments[i];
    Type *arg_type = sema_expr(ctx, arg_expr);

    if (arg_type == TYPE_error) {
      arguments_ok = false;
      continue;
    }

    Type *expected_type = func_type->param_types[i];

    if (!type_can_widen_to(arg_type, expected_type)) {
      sema_error(ctx,
                 arg_expr->loc,
                 "Argument %zu of function '%s' expects type '%s', but got '%s'",
                 i + 1,
                 callee->expr_identifier.name,
                 expected_type->name,
                 arg_type->name);
      arguments_ok = false;
      continue;
    }

    sema_coerce(ctx, &expr->expr_call.arguments[i], expected_type);
  }

  return arguments_ok ? func_type->return_type : TYPE_error;
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

// Checks the operands, converts both to a common type; arithmetic yields that type, comparisons bool.
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

    // Untyped operands stay untyped so a later consumer can pick the concrete type.
    if (!type_is_untyped(common_type)) {
      sema_coerce(ctx, &expr->expr_binary.left, common_type);
      sema_coerce(ctx, &expr->expr_binary.right, common_type);
    }

    return common_type;

  case BINOP_EQ:
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

    sema_coerce(ctx, &expr->expr_binary.left, type_default(common_type));
    sema_coerce(ctx, &expr->expr_binary.right, type_default(common_type));

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

    sema_coerce(ctx, &expr->expr_binary.left, type_default(common_type));
    sema_coerce(ctx, &expr->expr_binary.right, type_default(common_type));

    return TYPE_bool;
  }

  case BINOP_COUNT:
    sema_error(ctx, expr->loc, "Invalid binary operator");
    return TYPE_error;
  }

  sema_error(ctx, expr->loc, "Unknown binary operator");
  return TYPE_error;
}

// Types a cast node. Implicit casts are created by sema_coerce() with their type already set.
static Type *sema_expr_cast(SemaContext *ctx, AstExpr *expr) {
  if (expr->expr_cast.implicit) {
    return sema_expr_type(ctx, expr);
  }

  assert(expr->expr_cast.target && "Missing cast target");

  AstExpr *operand = expr->expr_cast.operand;
  Type *from = sema_expr(ctx, operand);
  Type *to = sema_type(ctx, expr->expr_cast.target);

  if (from == TYPE_error || to == TYPE_error) {
    return to;
  }

  bool from_is_value = type_is_numeric(from) || from == TYPE_bool;

  if (from_is_value && type_is_numeric(to)) {
    if (type_is_untyped(from)) {
      sema_concretize(ctx, operand, type_can_widen_to(from, to) ? to : type_default(from));
    }
    return to;
  }

  if (from == to && from != TYPE_void) {
    return to;
  }

  if (to == TYPE_bool && type_is_numeric(from)) {
    sema_error(ctx, expr->loc, "Cannot cast from '%s' to 'bool'. Use x != 0", from->name);
    return TYPE_error;
  }

  sema_error(ctx, expr->loc, "Cannot cast from '%s' to '%s'", from->name, to->name);
  return TYPE_error;
}

Type *sema_expr(SemaContext *ctx, AstExpr *expr) {
  assert(expr && "sema_expr called without an expression");

  Type *result_type = NULL;

  switch (expr->kind) {
  case AST_EXPR_LITERAL:
    result_type = sema_expr_literal(ctx, expr);
    break;

  case AST_EXPR_IDENTIFIER:
    result_type = sema_expr_identifier(ctx, expr);
    break;

  case AST_EXPR_ASSIGN:
    result_type = sema_expr_assignment(ctx, expr);
    break;
  case AST_EXPR_CALL:
    result_type = sema_expr_call(ctx, expr);
    break;

  case AST_EXPR_UNARY:
    result_type = sema_expr_unary(ctx, expr);
    break;
  case AST_EXPR_BINARY:
    result_type = sema_expr_binary(ctx, expr);
    break;

  case AST_EXPR_CAST:
    result_type = sema_expr_cast(ctx, expr);
    break;

  case AST_EXPR_INVALID:
    sema_set_expr_type(ctx, expr, TYPE_error);
    return TYPE_error;
  }

  assert(result_type && "every expression kind produces a type");

  sema_set_expr_type(ctx, expr, result_type);

  return result_type;
}

// Statement semantic analysis functions

static void sema_stmt_block(SemaContext *ctx, AstStmt *stmt) {
  ctx->current_scope = scope_enter(ctx, ctx->current_scope);

  for (size_t i = 0; i < stmt->stmt_block.statement_count; i++) {
    sema_stmt(ctx, stmt->stmt_block.statements[i]);
  }

  ctx->current_scope = scope_exit(ctx->current_scope);
}

static void sema_stmt_expression(SemaContext *ctx, AstStmt *stmt) {
  sema_expr(ctx, stmt->stmt_expression.expression);
}

// Declares a variable; the type comes from the annotation or the initializer, which is converted to it.
static void sema_stmt_local_var(SemaContext *ctx, AstStmt *stmt) {
  bool reuses_type_name = sema_reject_type_name(ctx, stmt->stmt_local_var.name, stmt->loc);

  Type *expected_type = NULL;
  if (stmt->stmt_local_var.declared_type) {
    expected_type = sema_type(ctx, stmt->stmt_local_var.declared_type);
  }

  Type *init_type = NULL;
  if (stmt->stmt_local_var.init) {
    init_type = sema_expr(ctx, stmt->stmt_local_var.init);
  }

  Type *var_type = expected_type ? expected_type : type_default(init_type);
  assert(var_type && "the parser requires a type annotation or an initializer");

  if (!type_has_storage(var_type)) {
    sema_error(ctx, stmt->loc, "Cannot declare variable '%s' of type '%s'", stmt->stmt_local_var.name, var_type->name);
    var_type = TYPE_error;
  }

  if (expected_type && init_type && !type_can_widen_to(init_type, var_type)) {
    sema_error(ctx, stmt->loc, "Cannot implicitly convert '%s' to '%s'", init_type->name, var_type->name);

    scope_insert(ctx, stmt->stmt_local_var.name, SYMBOL_LOCAL_VAR, TYPE_error, stmt->loc);
    return;
  }

  if (stmt->stmt_local_var.init) {
    sema_coerce(ctx, &stmt->stmt_local_var.init, var_type);
  }

  if (reuses_type_name) {
    var_type = TYPE_error;
  }

  Symbol *sym = scope_insert(ctx, stmt->stmt_local_var.name, SYMBOL_LOCAL_VAR, var_type, stmt->loc);
  if (!sym) {
    if (!reuses_type_name) {
      sema_error(ctx, stmt->loc, "Redeclaration of '%s' in current scope", stmt->stmt_local_var.name);
    }
    return;
  }

  stmt->stmt_local_var.symbol = sym;
}

static void sema_stmt_return(SemaContext *ctx, AstStmt *stmt) {
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
    return;
  }

  if (stmt->stmt_return.value) {
    sema_coerce(ctx, &stmt->stmt_return.value, expected_type);
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
  if (cond_type != TYPE_bool && cond_type != TYPE_error) {
    sema_error(ctx, stmt->stmt_if.condition->loc, "Condition in 'if' must be of type 'bool'");
  }

  sema_stmt(ctx, stmt->stmt_if.then_branch);
  if (stmt->stmt_if.else_branch) {
    sema_stmt(ctx, stmt->stmt_if.else_branch);
  }
}

static void sema_stmt_while(SemaContext *ctx, AstStmt *stmt) {
  Type *cond_type = sema_expr(ctx, stmt->stmt_while.condition);
  if (cond_type != TYPE_bool && cond_type != TYPE_error) {
    sema_error(ctx, stmt->stmt_while.condition->loc, "Condition in 'while' must be of type 'bool'");
  }

  ctx->loop_depth++;
  sema_stmt(ctx, stmt->stmt_while.body);
  ctx->loop_depth--;
}

void sema_stmt(SemaContext *ctx, AstStmt *stmt) {
  assert(ctx->current_function && "statements only appear inside functions");

  switch (stmt->kind) {
  case AST_STMT_BLOCK:
    return sema_stmt_block(ctx, stmt);
  case AST_STMT_EXPRESSION:
    return sema_stmt_expression(ctx, stmt);
  case AST_STMT_LOCAL_VAR:
    return sema_stmt_local_var(ctx, stmt);

  case AST_STMT_RETURN:
    return sema_stmt_return(ctx, stmt);
  case AST_STMT_BREAK:
    return sema_stmt_break(ctx, stmt);
  case AST_STMT_CONTINUE:
    return sema_stmt_continue(ctx, stmt);

  case AST_STMT_IF:
    return sema_stmt_if(ctx, stmt);
  case AST_STMT_WHILE:
    return sema_stmt_while(ctx, stmt);

  case AST_STMT_INVALID:
    return sema_error(ctx, stmt->loc, "Invalid statement");
  }

  sema_error(ctx, stmt->loc, "Unknown statement type");
}

// Declaration semantic analysis functions

// Pass 1: Symbol discovery

// Pass 1: resolves the signature and declares the function globally so it can be called before its definition.
static void sema_pass1_decl_function(SemaContext *ctx, AstDecl *decl) {
  AstDeclFunction *func = &decl->decl_function;

  Type *return_type = sema_type(ctx, func->declared_return_type);

  Type **param_types = arena_alloc(&ctx->comp_unit->arena, sizeof(Type *) * func->param_count);
  for (size_t i = 0; i < func->param_count; i++) {
    param_types[i] = sema_type(ctx, func->params[i].declared_type);

    if (!type_has_storage(param_types[i])) {
      sema_error(ctx,
                 func->params[i].loc,
                 "Cannot declare parameter '%s' of type '%s'",
                 func->params[i].name,
                 param_types[i]->name);
      param_types[i] = TYPE_error;
    }
  }

  Type *func_type = arena_alloc(&ctx->comp_unit->arena, sizeof(Type));
  *func_type = (Type){
    .kind = TYPE_KIND_FUNCTION,
    .name = "Function",
    .function =
      {
        .return_type = return_type,
        .param_types = param_types,
        .param_count = func->param_count,
      },
  };

  Symbol *sym = scope_insert(ctx, func->name, SYMBOL_FUNCTION, func_type, decl->loc);
  if (!sym) {
    sema_error(ctx, decl->loc, "Redeclaration of function '%s' in current scope", func->name);

    const char *sym_name = st_intern(&ctx->comp_unit->intern_table, func->name);
    sym = symbol_new(ctx, sym_name, SYMBOL_FUNCTION, func_type, decl->loc);
  }

  sym->type = func_type;
  func->symbol = sym;
}

static void sema_pass1_decl(SemaContext *ctx, AstDecl *decl) {
  switch (decl->kind) {
  case AST_DECL_FUNCTION:
    sema_pass1_decl_function(ctx, decl);
    break;

  case AST_DECL_INVALID:
    sema_error(ctx, decl->loc, "Invalid declaration");
    break;
  }
}

// Pass 2: Type checking and validation

// Pass 2: declares the parameters in a new scope and analyses the function body.
static void sema_pass2_decl_function(SemaContext *ctx, AstDecl *decl) {
  assert(decl->decl_function.symbol && "pass 1 declares every function");

  AstDeclFunction *func = &decl->decl_function;
  AstDeclFunction *prev_func = ctx->current_function;

  ctx->current_function = func;
  ctx->current_scope = scope_enter(ctx, ctx->current_scope);

  Type *func_type = func->symbol->type;

  for (uint32_t i = 0; i < func->param_count; i++) {
    AstParam *param = &func->params[i];
    Type *type = func_type->function.param_types[i];

    if (sema_reject_type_name(ctx, param->name, param->loc)) {
      scope_insert(ctx, param->name, SYMBOL_LOCAL_VAR, TYPE_error, param->loc);
      continue;
    }

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
  case AST_DECL_FUNCTION: {
    return sema_pass2_decl_function(ctx, decl);
  }

  case AST_DECL_INVALID:
    return;
  }

  sema_error(ctx, decl->loc, "Unknown declaration type");
}

// Type semantic analysis functions

static Type *sema_type_named(SemaContext *ctx, AstType *type) {
  assert(type->named && "the parser always provides the name of a named type");

  Symbol *sym = scope_lookup_type_name(ctx, type->named);
  if (!sym) {
    sema_error(ctx, type->loc, "Undefined type '%s'", type->named);
    return TYPE_error;
  }

  if (sym->kind != SYMBOL_TYPE) {
    const char *what = sym->kind == SYMBOL_FUNCTION ? "a function" : "a variable";
    sema_error(ctx, type->loc, "'%s' is %s, not a type (declared at line %zu)", type->named, what, sym->loc.line);
    return TYPE_error;
  }

  return sym->type;
}

// Resolves a written type to a Type: no type means void, and pointer types are not implemented yet.
Type *sema_type(SemaContext *ctx, AstType *type) {
  if (!type) {
    return TYPE_void;
  }

  switch (type->kind) {
  case AST_TYPE_INVALID:
    sema_error(ctx, type->loc, "Invalid type");
    return TYPE_error;
  case AST_TYPE_NAMED:
    return sema_type_named(ctx, type);
  case AST_TYPE_POINTER:
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

  ctx->expr_types = NULL;
  da_set_len(ctx->expr_types, ast_expr_count() + 1);
  for (size_t i = 0; i < da_len(ctx->expr_types); i++) {
    ctx->expr_types[i] = NULL;
  }

  sema_init_builtins(ctx);
}

void sema_free(SemaContext *ctx) {
  da_free(ctx->expr_types);
}

// Runs pass 1 over all declarations, then pass 2
void sema_program(SemaContext *ctx, AstProgram *program) {
  for (size_t i = 0; i < program->decl_count; i++) {
    sema_pass1_decl(ctx, program->decls[i]);
  }

  for (size_t i = 0; i < program->decl_count; i++) {
    sema_pass2_decl(ctx, program->decls[i]);
  }
}