#include "sema.h"

#include "ast.h"
#include "common.h"
#include "lib/arena.h"
#include "lib/array.h"
#include "lib/str_table.h"
#include "types.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static void sema_error(SemaContext *ctx, SourceLoc loc, const char *message, ...) {
  ctx->comp_unit->had_error = true;

  va_list args;
  va_start(args, message);

  fprintf(stderr, "[line %u] Error: ", loc.line);
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
      if (sym->name == name && (sym->kind == SYMBOL_TYPE || sym->type != type_error)) {
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
               "Redeclaration of type '%s' in current scope. Previous declaration at line %u",
               name,
               existing->loc.line);
    return NULL;
  }

  return scope_insert(ctx, name, SYMBOL_TYPE, type, loc);
}

// Reports an error if `name` is a type name.
static bool sema_reject_type_name(SemaContext *ctx, const char *name, SourceLoc loc) {
  Symbol *existing = scope_lookup(ctx, name);
  if (!existing || existing->kind != SYMBOL_TYPE) {
    return false;
  }

  sema_error(ctx, loc, "'%s' is a type name and cannot be used as a variable name", name);
  return true;
}

// Expression types

Type *sema_expr_type(const SemaContext *ctx, const AstExpr *expr) {
  return expr->id < da_len(ctx->expr_types) ? ctx->expr_types[expr->id] : NULL;
}

// Whether `expr` denotes a location that can be assigned to.
static bool is_lvalue(const AstExpr *expr) {
  if (!expr) return false;

  switch (expr->kind) {
  case AST_EXPR_IDENTIFIER: {
    Symbol *sym = expr->expr_identifier.symbol;
    if (!sym) return false;

    return sym->kind == SYMBOL_GLOBAL_VAR || sym->kind == SYMBOL_LOCAL_VAR;
  }
  case AST_EXPR_DEREF:
    return true;

  case AST_EXPR_SUBSCRIPT:
    return is_lvalue(expr->expr_subscript.target);

  default:
    return false;
  }
}

// Names the kind of expression for diagnostics, e.g. "a literal".
static const char *describe_expr(const AstExpr *expr) {
  switch (expr->kind) {
  case AST_EXPR_LITERAL:
    return "a literal";
  case AST_EXPR_IDENTIFIER:
    return "a name";

  case AST_EXPR_ARRAY_LITERAL:
    return "an array literal";

  case AST_EXPR_ADDR_OF:
    return "an address-of expression";
  case AST_EXPR_DEREF:
    return "a dereference";
  case AST_EXPR_SUBSCRIPT:
    return "a subscript";
  case AST_EXPR_CALL:
    return "a function call result";

  case AST_EXPR_UNARY:
  case AST_EXPR_BINARY:
    return "an operator expression";
  case AST_EXPR_LOGICAL:
    return "a logical operator expression";
  case AST_EXPR_CAST:
    return "a cast";

  case AST_EXPR_ASSIGN:
    return "an assignment";

  case AST_EXPR_INVALID:
    return "an invalid expression";
  }

  UNREACHABLE("unhandled expression kind");
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

  if (from == type_error || to == type_error) {
    return;
  }

  if (type_is_untyped(from)) {
    sema_concretize(ctx, expr, type_default(to));
    return;
  }

  if (!type_equals(from, to)) {
    AstExpr *cast = ast_expr_new(&ctx->comp_unit->arena, AST_EXPR_CAST, expr->loc);
    cast->expr_cast.operand = expr;
    cast->expr_cast.implicit = true;
    sema_set_expr_type(ctx, cast, to);

    *slot = cast;
  }
}

// Array types

// The largest size of an array in bytes, so that every element offset fits a signed 32-bit displacement.
#define ARRAY_MAX_SIZE INT32_MAX

// Builds `[length]elem_type`, or reports an error and gives type_error if its size is too large.
static Type *sema_array_of(SemaContext *ctx, SourceLoc loc, Type *elem_type, uint64_t length) {
  if (elem_type->size > 0 && length > ARRAY_MAX_SIZE / elem_type->size) {
    sema_error(ctx, loc, "Array of %llu '%s' is too large", (unsigned long long)length, elem_type->name);
    return type_error;
  }

  return type_array_of(&ctx->comp_unit->arena, elem_type, length);
}

// Expression semantic analysis functions

// Types a literal
static Type *sema_expr_literal(SemaContext *ctx, AstExpr *expr) {
  switch (expr->expr_literal.kind) {
  case AST_LITERAL_INT:
    return type_untyped_int;
  case AST_LITERAL_FLOAT:
    return type_untyped_float;
  case AST_LITERAL_NULL:
    return type_untyped_null;

  case AST_LITERAL_TRUE:
  case AST_LITERAL_FALSE:
    return type_bool;

  case AST_LITERAL_CHAR:
  case AST_LITERAL_STRING:
    sema_error(ctx, expr->loc, "Literal type not yet supported in semantic analysis");
    return type_error;
  }

  sema_error(ctx, expr->loc, "Unknown literal type");
  return type_error;
}

// Resolves a name to a variable; type and function names are not values.
static Type *sema_expr_identifier(SemaContext *ctx, AstExpr *expr) {
  Symbol *sym = scope_lookup(ctx, expr->expr_identifier.name);
  if (!sym) {
    sema_error(ctx, expr->loc, "Undefined identifier '%s'", expr->expr_identifier.name);

    scope_insert(ctx, expr->expr_identifier.name, SYMBOL_LOCAL_VAR, type_error, expr->loc);
    return type_error;
  }

  if (sym->kind == SYMBOL_TYPE) {
    sema_error(ctx, expr->loc, "'%s' is a type, not a value", expr->expr_identifier.name);
    return type_error;
  }

  if (sym->kind == SYMBOL_FUNCTION) {
    sema_error(
      ctx, expr->loc, "'%s' is a function, not a value (did you mean to call it?)", expr->expr_identifier.name);
    return type_error;
  }

  sym->is_used = true;
  expr->expr_identifier.symbol = sym;
  return sym->type;
}

// Reports a literal error unless `report` is off (used when the length is only probed).
static void sema_literal_error(SemaContext *ctx, bool report, SourceLoc loc, const char *message) {
  if (report) sema_error(ctx, loc, "%s", message);
}

// Length of an array literal (-1 if none). Sets each list element's `index`; element without an index follows last.
static int64_t sema_array_literal_length(SemaContext *ctx, AstExpr *expr, bool report) {
  if (expr->expr_array_literal.is_repeat) {
    AstExpr *length_expr = expr->expr_array_literal.repeat.length_expr;

    if (length_expr->kind != AST_EXPR_LITERAL || length_expr->expr_literal.kind != AST_LITERAL_INT) {
      sema_literal_error(ctx, report, length_expr->loc, "Array repeat length must be an integer literal");
      return -1;
    }

    int64_t length = length_expr->expr_literal.int_value;
    if (length <= 0 || length > UINT32_MAX) {
      sema_literal_error(ctx, report, length_expr->loc, "Array size must be between 1 and 2^32 - 1");
      return -1;
    }

    return length;
  }

  AstArrayElement *elements = expr->expr_array_literal.list.elements;
  uint32_t element_count = expr->expr_array_literal.list.element_count;
  int64_t next_index = 0;
  int64_t length = 0;

  for (uint32_t i = 0; i < element_count; i++) {
    AstExpr *index_expr = elements[i].index_expr;

    if (index_expr) {
      if (index_expr->kind != AST_EXPR_LITERAL || index_expr->expr_literal.kind != AST_LITERAL_INT) {
        sema_literal_error(ctx, report, index_expr->loc, "Array index must be an integer literal");
        return -1;
      }

      next_index = index_expr->expr_literal.int_value;
      if (next_index < 0 || next_index >= UINT32_MAX) {
        sema_literal_error(ctx, report, index_expr->loc, "Array index is out of range");
        return -1;
      }
    }

    if (next_index >= UINT32_MAX) {
      sema_literal_error(ctx, report, elements[i].loc, "Array index is out of range");
      return -1;
    }

    elements[i].index = (uint32_t)next_index++;
    if (next_index > length) length = next_index;
  }

  if (expr->expr_array_literal.list.is_sparse) {
    for (uint32_t i = 0; i < element_count; i++) {
      for (uint32_t j = 0; j < i; j++) {
        if (elements[i].index == elements[j].index) {
          sema_literal_error(ctx, report, elements[i].loc, "Array index is initialized twice");
          return -1;
        }
      }
    }
  }

  if (length == 0) {
    sema_literal_error(ctx, report, expr->loc, "An array literal needs at least one element");
    return -1;
  }

  return length;
}

// The slot of the value for element `i`; a repeat literal has a single value shared by every element.
static AstExpr **array_literal_value_slot(AstExpr *expr, uint32_t i) {
  return expr->expr_array_literal.is_repeat ? &expr->expr_array_literal.repeat.value
                                            : &expr->expr_array_literal.list.elements[i].value;
}

// Converts a literal value of `type` to `elem_type`
static bool sema_literal_value_coerce(SemaContext *ctx, AstExpr **slot, Type *type, Type *elem_type, uint32_t i) {
  if (type == type_error) {
    return false;
  }

  if (!type_can_widen_to(type, elem_type)) {
    sema_error(
      ctx, (*slot)->loc, "Array element %u of type '%s' cannot convert to '%s'", i, type->name, elem_type->name);
    return false;
  }

  sema_coerce(ctx, slot, elem_type);
  return true;
}

// Types `[a, b]`, `[i: a]` and `[v; n]`: values convert to the element type of `hint`, or to their common type.
static Type *sema_expr_array_literal(SemaContext *ctx, AstExpr *expr, Type *hint) {
  bool is_repeat = expr->expr_array_literal.is_repeat;

  int64_t length = sema_array_literal_length(ctx, expr, true);
  if (length < 0) {
    return type_error;
  }

  uint32_t value_count = is_repeat ? 1 : expr->expr_array_literal.list.element_count;

  if (hint == type_error) {
    for (uint32_t i = 0; i < value_count; i++) {
      sema_expr(ctx, *array_literal_value_slot(expr, i));
    }
    return type_error;
  }

  Type *elem_type = NULL;

  if (hint) {
    if (!type_is_array(hint)) {
      sema_error(ctx, expr->loc, "Cannot convert an array literal to '%s'", hint->name);
      return type_error;
    }

    bool is_sparse = !is_repeat && expr->expr_array_literal.list.is_sparse;
    if (is_sparse ? length > hint->array.length : length != hint->array.length) {
      sema_error(ctx,
                 expr->loc,
                 "Array literal has %lld elements, but '%s' has %u",
                 (long long)length,
                 hint->name,
                 hint->array.length);
      return type_error;
    }

    elem_type = hint->array.elem_type;
  } else {
    // Without a hint the element type is the common type of the values
    for (uint32_t i = 0; i < value_count; i++) {
      AstExpr *value = *array_literal_value_slot(expr, i);
      Type *type = sema_expr(ctx, value);
      if (type == type_error) {
        return type_error;
      }

      Type *common_type = !elem_type || type_equals(elem_type, type) ? type : type_common(elem_type, type);
      if (!common_type) {
        sema_error(
          ctx, value->loc, "Array element %u of type '%s' does not match '%s'", i, type->name, elem_type->name);
        return type_error;
      }

      elem_type = common_type;
    }

    elem_type = type_default(elem_type);
  }

  bool values_ok = true;

  for (uint32_t i = 0; i < value_count; i++) {
    AstExpr **slot = array_literal_value_slot(expr, i);
    Type *type = hint ? sema_expr_hint(ctx, *slot, elem_type) : sema_expr_type(ctx, *slot);
    values_ok &= sema_literal_value_coerce(ctx, slot, type, elem_type, i);
  }

  if (!values_ok) {
    return type_error;
  }

  return hint ? hint : sema_array_of(ctx, expr->loc, elem_type, length);
}

// Types `&x`: an lvalue operand, giving a pointer to it. A variable whose address is taken lives in memory.
static Type *sema_expr_addr_of(SemaContext *ctx, AstExpr *expr) {
  AstExpr *operand = expr->expr_addr_of.operand;
  Type *operand_type = sema_expr(ctx, operand);

  if (operand_type != type_error && !is_lvalue(operand)) {
    sema_error(ctx, expr->loc, "Cannot take the address of %s", describe_expr(operand));
    return type_error;
  }

  if (operand_type == type_error) {
    return type_error;
  }

  if (operand->kind == AST_EXPR_IDENTIFIER) {
    Symbol *sym = operand->expr_identifier.symbol;
    assert(sym->kind == SYMBOL_LOCAL_VAR && "sema rejects type and function names as values");
    sym->local.in_memory = true;
  }

  return type_pointer_to(&ctx->comp_unit->arena, operand_type);
}

// Types `p.*`: the operand must be a pointer to a type with storage.
static Type *sema_expr_deref(SemaContext *ctx, AstExpr *expr) {
  AstExpr *operand = expr->expr_deref.operand;
  Type *operand_type = sema_expr(ctx, operand);

  if (operand_type == type_error) {
    return type_error;
  }

  if (operand_type->kind != TYPE_KIND_POINTER) {
    sema_error(ctx, expr->loc, "Cannot dereference non-pointer type '%s'", operand_type->name);
    return type_error;
  }

  if (!type_has_storage(operand_type->pointer.base_type)) {
    sema_error(ctx, expr->loc, "Cannot dereference '%s'", operand_type->name);
    return type_error;
  }

  return operand_type->pointer.base_type;
}

// Whether `expr` is an integer literal, possibly negated; its value is stored in `value`.
static bool const_int_value(AstExpr *expr, int64_t *value) {
  if (expr->kind == AST_EXPR_LITERAL && expr->expr_literal.kind == AST_LITERAL_INT) {
    *value = expr->expr_literal.int_value;
    return true;
  }

  if (expr->kind == AST_EXPR_UNARY && expr->expr_unary.op == UNOP_NEGATE) {
    int64_t operand;
    if (const_int_value(expr->expr_unary.operand, &operand)) {
      *value = -operand;
      return true;
    }
  }

  return false;
}

// Types `xs[i]`: the target must be an array and the index an integer, which is converted to i64.
static Type *sema_expr_subscript(SemaContext *ctx, AstExpr *expr) {
  Type *target_type = sema_expr(ctx, expr->expr_subscript.target);
  Type *index_type = sema_expr(ctx, expr->expr_subscript.index);

  if (target_type == type_error || index_type == type_error) {
    return type_error;
  }

  if (target_type->kind != TYPE_KIND_ARRAY) {
    sema_error(ctx, expr->loc, "Cannot subscript non-array type '%s'", target_type->name);
    return type_error;
  }

  if (!type_is_integer(index_type)) {
    sema_error(ctx, expr->loc, "Array index must be an integer, got '%s'", index_type->name);
    return type_error;
  }

  int64_t index_value;
  if (const_int_value(expr->expr_subscript.index, &index_value) &&
      (index_value < 0 || (uint64_t)index_value >= target_type->array.length)) {
    sema_error(ctx,
               expr->expr_subscript.index->loc,
               "Array index %lld is out of bounds for '%s'",
               (long long)index_value,
               target_type->name);
    return type_error;
  }

  sema_coerce(ctx, &expr->expr_subscript.index, type_i64);

  return target_type->array.elem_type;
}

// Analyses the arguments of a call that could not be resolved, so that errors inside them are still reported.
static void sema_call_arguments(SemaContext *ctx, AstExpr *expr) {
  for (uint32_t i = 0; i < expr->expr_call.argument_count; i++) {
    sema_expr(ctx, expr->expr_call.arguments[i]);
  }
}

// Types a call: the callee must be a function and each argument converts to its parameter type.
static Type *sema_expr_call(SemaContext *ctx, AstExpr *expr) {
  AstExpr *callee = expr->expr_call.callee;

  if (callee->kind != AST_EXPR_IDENTIFIER) {
    sema_error(ctx, expr->loc, "Function calls must be made to identifiers");
    sema_call_arguments(ctx, expr);
    return type_error;
  }

  Symbol *func_sym = scope_lookup(ctx, callee->expr_identifier.name);

  if (!func_sym) {
    sema_error(ctx, expr->loc, "Undefined function '%s'", callee->expr_identifier.name);
    sema_call_arguments(ctx, expr);
    return type_error;
  }

  if (func_sym->kind != SYMBOL_FUNCTION) {
    sema_error(ctx, expr->loc, "Expected function, got %s", callee->expr_identifier.name);
    sema_call_arguments(ctx, expr);
    return type_error;
  }

  callee->expr_identifier.symbol = func_sym;

  TypeFunction *func_type = &func_sym->type->function;

  if (expr->expr_call.argument_count != func_type->param_count) {
    sema_error(ctx,
               expr->loc,
               "Function '%s' expects %u arguments, but %u were provided",
               callee->expr_identifier.name,
               func_type->param_count,
               expr->expr_call.argument_count);
    sema_call_arguments(ctx, expr);
    return type_error;
  }

  bool arguments_ok = true;

  for (uint32_t i = 0; i < expr->expr_call.argument_count; i++) {
    AstExpr *arg_expr = expr->expr_call.arguments[i];
    Type *expected_type = func_type->param_types[i];
    Type *arg_type = sema_expr_hint(ctx, arg_expr, expected_type);

    if (arg_type == type_error) {
      arguments_ok = false;
      continue;
    }

    if (!type_can_widen_to(arg_type, expected_type)) {
      sema_error(ctx,
                 arg_expr->loc,
                 "Argument %u of function '%s' expects type '%s', but got '%s'",
                 i + 1,
                 callee->expr_identifier.name,
                 expected_type->name,
                 arg_type->name);
      arguments_ok = false;
      continue;
    }

    sema_coerce(ctx, &expr->expr_call.arguments[i], expected_type);
  }

  return arguments_ok ? func_type->return_type : type_error;
}

// Types `-x`, `!x` and `~x`: numeric, bool and integer operands respectively.
static Type *sema_expr_unary(SemaContext *ctx, AstExpr *expr) {
  Type *operand_type = sema_expr(ctx, expr->expr_unary.operand);

  if (operand_type == type_error) {
    return type_error;
  }

  switch (expr->expr_unary.op) {
  case UNOP_NEGATE:
    if (!type_is_numeric(operand_type)) {
      sema_error(ctx, expr->loc, "Unary '-' operator requires numeric type");
      return type_error;
    }

    return operand_type;

  case UNOP_NOT:
    if (!type_is_bool(operand_type)) {
      sema_error(ctx, expr->loc, "Unary '!' operator requires 'bool' type");
      return type_error;
    }

    return operand_type;

  case UNOP_BIT_NOT:
    if (!type_is_integer(operand_type)) {
      sema_error(ctx, expr->loc, "Unary '~' operator requires integer type");
      return type_error;
    }

    return operand_type;

  case UNOP_COUNT:
    sema_error(ctx, expr->loc, "Invalid unary operator");
    return type_error;
  }

  sema_error(ctx, expr->loc, "Unknown unary operator");
  return type_error;
}

// Types `ptr + int`, `int + ptr`, `ptr - int` (the pointer type) and `ptr - ptr` (i64, counted in elements).
static Type *sema_pointer_arithmetic(SemaContext *ctx, AstExpr *expr, Type *left_type, Type *right_type) {
  BinaryOp op = expr->expr_binary.op;
  bool left_is_ptr = type_is_pointer(left_type);
  bool right_is_ptr = type_is_pointer(right_type);

  if (op != BINOP_ADD && op != BINOP_SUB) {
    sema_error(ctx, expr->loc, "Pointer arithmetic only supports + and -");
    return type_error;
  }

  Type *ptr_type = left_is_ptr ? left_type : right_type;
  if (!type_has_storage(ptr_type->pointer.base_type)) {
    sema_error(ctx, expr->loc, "Cannot do arithmetic on a pointer to '%s'", ptr_type->pointer.base_type->name);
    return type_error;
  }

  if (left_is_ptr && right_is_ptr) {
    if (op == BINOP_ADD) {
      sema_error(ctx, expr->loc, "Cannot add two pointers");
      return type_error;
    }

    if (!type_equals(left_type, right_type)) {
      sema_error(ctx, expr->loc, "Cannot subtract '%s' from '%s'", right_type->name, left_type->name);
      return type_error;
    }

    return type_i64;
  }

  if (right_is_ptr && op == BINOP_SUB) {
    sema_error(ctx, expr->loc, "Cannot subtract a pointer from '%s'", left_type->name);
    return type_error;
  }

  Type *offset_type = left_is_ptr ? right_type : left_type;
  if (!type_is_integer(offset_type)) {
    sema_error(ctx, expr->loc, "Pointer arithmetic needs an integer offset, got '%s'", offset_type->name);
    return type_error;
  }

  sema_coerce(ctx, left_is_ptr ? &expr->expr_binary.right : &expr->expr_binary.left, type_i64);

  return ptr_type;
}

// Checks the operands, converts both to a common type; arithmetic yields that type, comparisons bool.
static Type *sema_expr_binary(SemaContext *ctx, AstExpr *expr) {
  Type *left_type = sema_expr(ctx, expr->expr_binary.left);
  Type *right_type = sema_expr(ctx, expr->expr_binary.right);

  if (left_type == type_error || right_type == type_error) {
    return type_error;
  }

  switch (expr->expr_binary.op) {
  case BINOP_ADD:
  case BINOP_SUB:
  case BINOP_MUL:
  case BINOP_DIV: {
    if (type_is_pointer(left_type) || type_is_pointer(right_type)) {
      return sema_pointer_arithmetic(ctx, expr, left_type, right_type);
    }

    if (!type_is_numeric(left_type) || !type_is_numeric(right_type)) {
      sema_error(ctx, expr->loc, "Arithmetic operators require numeric operands");
      return type_error;
    }

    Type *common_type = type_common(left_type, right_type);
    if (!common_type) {
      sema_error(
        ctx, expr->loc, "Incompatible types for arithmetic (got '%s' and '%s')", left_type->name, right_type->name);
      return type_error;
    }

    // Untyped operands stay untyped so a later consumer can pick the concrete type.
    if (!type_is_untyped(common_type)) {
      sema_coerce(ctx, &expr->expr_binary.left, common_type);
      sema_coerce(ctx, &expr->expr_binary.right, common_type);
    }

    return common_type;
  }

  case BINOP_EQ:
  case BINOP_NOT_EQ: {
    if (!type_is_comparable(left_type) || !type_is_comparable(right_type)) {
      Type *bad_type = type_is_comparable(left_type) ? right_type : left_type;
      sema_error(ctx, expr->loc, "Type '%s' cannot be compared for equality", bad_type->name);
      return type_error;
    }

    Type *common_type = type_common(left_type, right_type);
    if (!common_type) {
      sema_error(ctx, expr->loc, "Incompatible types for equality: '%s' and '%s'", left_type->name, right_type->name);
      return type_error;
    }

    sema_coerce(ctx, &expr->expr_binary.left, type_default(common_type));
    sema_coerce(ctx, &expr->expr_binary.right, type_default(common_type));

    return type_bool;
  }

  case BINOP_LESS:
  case BINOP_GREATER:
  case BINOP_LESS_EQ:
  case BINOP_GREATER_EQ: {
    if (!type_is_orderable(left_type) || !type_is_orderable(right_type)) {
      sema_error(ctx, expr->loc, "Comparisons require ordered types");
      return type_error;
    }

    Type *common_type = type_common(left_type, right_type);
    if (!common_type) {
      sema_error(ctx, expr->loc, "Cannot compare '%s' with '%s'", left_type->name, right_type->name);
      return type_error;
    }

    sema_coerce(ctx, &expr->expr_binary.left, type_default(common_type));
    sema_coerce(ctx, &expr->expr_binary.right, type_default(common_type));

    return type_bool;
  }

  case BINOP_BIT_OR:
  case BINOP_BIT_XOR:
  case BINOP_BIT_AND: {
    if (!type_is_integer(left_type) || !type_is_integer(right_type)) {
      sema_error(ctx, expr->loc, "Bitwise operators require integer operands");
      return type_error;
    }

    Type *common_type = type_common(left_type, right_type);
    if (!common_type) {
      sema_error(
        ctx, expr->loc, "Incompatible types for bitwise operation: '%s' and '%s'", left_type->name, right_type->name);
      return type_error;
    }

    if (!type_is_untyped(common_type)) {
      sema_coerce(ctx, &expr->expr_binary.left, common_type);
      sema_coerce(ctx, &expr->expr_binary.right, common_type);
    }

    return common_type;
  }

  case BINOP_SHL:
  case BINOP_SHR: {
    if (!type_is_integer(left_type) || !type_is_integer(right_type)) {
      sema_error(ctx, expr->loc, "Shift operators require integer operands");
      return type_error;
    }

    return left_type;
  }

  case BINOP_COUNT:
    sema_error(ctx, expr->loc, "Invalid binary operator");
    return type_error;
  }

  sema_error(ctx, expr->loc, "Unknown binary operator");
  return type_error;
}

// Types `a && b` and `a || b`: both operands must be bool.
static Type *sema_expr_logical(SemaContext *ctx, AstExpr *expr) {
  Type *left_type = sema_expr(ctx, expr->expr_logical.left);
  Type *right_type = sema_expr(ctx, expr->expr_logical.right);

  if (left_type == type_error || right_type == type_error) {
    return type_error;
  }

  if (!type_is_bool(left_type) || !type_is_bool(right_type)) {
    sema_error(ctx, expr->loc, "Logical operators require 'bool' operands");
    return type_error;
  }

  return type_bool;
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

  if (from == type_error || to == type_error) {
    return to;
  }

  bool from_is_value = type_is_numeric(from) || from == type_bool;

  if (from_is_value && type_is_numeric(to)) {
    if (type_is_untyped(from)) {
      sema_concretize(ctx, operand, type_can_widen_to(from, to) ? to : type_default(from));
    }
    return to;
  }

  if (from == type_untyped_null && type_is_pointer(to)) {
    sema_concretize(ctx, operand, to);
    return to;
  }

  if (type_is_pointer(from) && (type_is_pointer(to) || to == type_i64)) {
    return to;
  }

  if (from == type_i64 && type_is_pointer(to)) {
    return to;
  }

  if (from == to && from != type_void) {
    return to;
  }

  if (to == type_bool && type_is_numeric(from)) {
    sema_error(ctx, expr->loc, "Cannot cast from '%s' to 'bool'. Use x != 0", from->name);
    return type_error;
  }

  sema_error(ctx, expr->loc, "Cannot cast from '%s' to '%s'", from->name, to->name);
  return type_error;
}

// Types `target = value`: the target must be an lvalue that is not an array, and the value converts to its type.
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
      return type_error;
    }
  }

  Type *target_type = sema_expr(ctx, target);
  Type *value_type = sema_expr_hint(ctx, expr->expr_assign.value, target_type);

  if (target_type != type_error && !is_lvalue(target)) {
    sema_error(ctx, expr->loc, "Invalid assignment target: %s cannot be assigned to", describe_expr(target));
    return type_error;
  }

  if (target_type == type_error || value_type == type_error) {
    return type_error;
  }

  if (target->kind == AST_EXPR_IDENTIFIER) {
    Symbol *target_sym = target->expr_identifier.symbol;
    if (!target_sym || (target_sym->kind != SYMBOL_LOCAL_VAR && target_sym->kind != SYMBOL_GLOBAL_VAR)) {
      sema_error(ctx, expr->loc, "Invalid assignment target: '%s' is not a variable", target->expr_identifier.name);
      return type_error;
    }
  }

  if (!type_can_widen_to(value_type, target_type)) {
    sema_error(ctx, expr->loc, "Cannot assign '%s' to '%s'", value_type->name, target_type->name);
    return type_error;
  }

  sema_coerce(ctx, &expr->expr_assign.value, target_type);

  return target_type;
}

// Analyses an expression, records its type and returns it.
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

  case AST_EXPR_ARRAY_LITERAL:
    result_type = sema_expr_array_literal(ctx, expr, NULL);
    break;

  case AST_EXPR_ADDR_OF:
    result_type = sema_expr_addr_of(ctx, expr);
    break;

  case AST_EXPR_DEREF:
    result_type = sema_expr_deref(ctx, expr);
    break;

  case AST_EXPR_SUBSCRIPT:
    result_type = sema_expr_subscript(ctx, expr);
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

  case AST_EXPR_LOGICAL:
    result_type = sema_expr_logical(ctx, expr);
    break;

  case AST_EXPR_CAST:
    result_type = sema_expr_cast(ctx, expr);
    break;

  case AST_EXPR_ASSIGN:
    result_type = sema_expr_assignment(ctx, expr);
    break;

  case AST_EXPR_INVALID:
    sema_set_expr_type(ctx, expr, type_error);
    return type_error;
  }

  assert(result_type && "every expression kind produces a type");

  sema_set_expr_type(ctx, expr, result_type);

  return result_type;
}

// Like sema_expr, but `hint` is the type the expression will be converted to.
Type *sema_expr_hint(SemaContext *ctx, AstExpr *expr, Type *hint) {
  assert(expr && "sema_expr_hint called without an expression");

  if (expr->kind != AST_EXPR_ARRAY_LITERAL) {
    return sema_expr(ctx, expr);
  }

  Type *type = sema_expr_array_literal(ctx, expr, hint);
  sema_set_expr_type(ctx, expr, type);

  return type;
}

// Statement semantic analysis functions

static void sema_stmt_block(SemaContext *ctx, AstStmt *stmt) {
  ctx->current_scope = scope_enter(ctx, ctx->current_scope);

  for (uint32_t i = 0; i < stmt->stmt_block.statement_count; i++) {
    sema_stmt(ctx, stmt->stmt_block.statements[i]);
  }

  ctx->current_scope = scope_exit(ctx->current_scope);
}

static void sema_stmt_expression(SemaContext *ctx, AstStmt *stmt) {
  sema_expr(ctx, stmt->stmt_expression.expression);
}

// Resolves `[_]T` and analyses the initializer: a literal against the full type, other values first for their length.
static Type *sema_inferred_array_type(SemaContext *ctx, AstType *type, AstExpr *initializer) {
  if (!initializer) {
    sema_error(ctx, type->loc, "Array with inferred length '[_]' must have an initializer");
    return type_error;
  }

  Type *elem_type = sema_type(ctx, type->array.elem_type);
  Type *array_type = NULL;

  // A literal knows its length before it is analysed, so it gets the full type as its hint
  int64_t literal_length =
    initializer->kind == AST_EXPR_ARRAY_LITERAL ? sema_array_literal_length(ctx, initializer, false) : -1;

  if (elem_type == type_error) {
    array_type = type_error;
  } else if (literal_length > 0) {
    array_type = sema_array_of(ctx, initializer->loc, elem_type, literal_length);
  }

  Type *init_type = sema_expr_hint(ctx, initializer, array_type);

  if (array_type) {
    return array_type;
  }

  if (init_type == type_error) {
    return type_error;
  }

  if (!type_is_array(init_type)) {
    sema_error(ctx, initializer->loc, "Cannot initialize array with non-array type '%s'", init_type->name);
    return type_error;
  }

  return sema_array_of(ctx, initializer->loc, elem_type, init_type->array.length);
}

// Resolves a local variable's annotation (NULL if none) and analyses the initializer against it.
static Type *sema_local_var_type(SemaContext *ctx, AstStmt *stmt) {
  AstType *declared_type = stmt->stmt_local_var.declared_type;
  AstExpr *initializer = stmt->stmt_local_var.init;

  if (declared_type && declared_type->kind == AST_TYPE_ARRAY && !declared_type->array.length_expr) {
    return sema_inferred_array_type(ctx, declared_type, initializer);
  }

  Type *expected_type = declared_type ? sema_type(ctx, declared_type) : NULL;

  if (initializer) {
    sema_expr_hint(ctx, initializer, expected_type);
  }

  return expected_type;
}

// Declares a variable; the type comes from the annotation or the initializer, which is converted to it.
static void sema_stmt_local_var(SemaContext *ctx, AstStmt *stmt) {
  bool reuses_type_name = sema_reject_type_name(ctx, stmt->stmt_local_var.name, stmt->loc);

  Type *expected_type = sema_local_var_type(ctx, stmt);
  Type *init_type = stmt->stmt_local_var.init ? sema_expr_type(ctx, stmt->stmt_local_var.init) : NULL;

  Type *var_type = expected_type ? expected_type : type_default(init_type);
  assert(var_type && "the parser requires a type annotation or an initializer");

  if (!type_has_storage(var_type)) {
    sema_error(ctx, stmt->loc, "Cannot declare variable '%s' of type '%s'", stmt->stmt_local_var.name, var_type->name);
    var_type = type_error;
  }

  if (expected_type && init_type && !type_can_widen_to(init_type, var_type)) {
    sema_error(ctx, stmt->loc, "Cannot implicitly convert '%s' to '%s'", init_type->name, var_type->name);

    scope_insert(ctx, stmt->stmt_local_var.name, SYMBOL_LOCAL_VAR, type_error, stmt->loc);
    return;
  }

  if (stmt->stmt_local_var.init) {
    sema_coerce(ctx, &stmt->stmt_local_var.init, var_type);
  }

  if (reuses_type_name) {
    var_type = type_error;
  }

  Symbol *sym = scope_insert(ctx, stmt->stmt_local_var.name, SYMBOL_LOCAL_VAR, var_type, stmt->loc);
  if (!sym) {
    if (!reuses_type_name) {
      sema_error(ctx, stmt->loc, "Redeclaration of '%s' in current scope", stmt->stmt_local_var.name);
    }
    return;
  }

  if (type_is_aggregate(var_type)) {
    sym->local.in_memory = true;
  }

  sym->is_defined = stmt->stmt_local_var.init != NULL;
  stmt->stmt_local_var.symbol = sym;
}

static void sema_stmt_return(SemaContext *ctx, AstStmt *stmt) {
  Type *expected_type = ctx->current_function->symbol->type->function.return_type;
  Type *actual_type = type_void;

  if (stmt->stmt_return.value) {
    actual_type = sema_expr_hint(ctx, stmt->stmt_return.value, expected_type);
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
  if (cond_type != type_bool && cond_type != type_error) {
    sema_error(ctx, stmt->stmt_if.condition->loc, "Condition in 'if' must be of type 'bool'");
  }

  sema_stmt(ctx, stmt->stmt_if.then_branch);
  if (stmt->stmt_if.else_branch) {
    sema_stmt(ctx, stmt->stmt_if.else_branch);
  }
}

static void sema_stmt_while(SemaContext *ctx, AstStmt *stmt) {
  Type *cond_type = sema_expr(ctx, stmt->stmt_while.condition);
  if (cond_type != type_bool && cond_type != type_error) {
    sema_error(ctx, stmt->stmt_while.condition->loc, "Condition in 'while' must be of type 'bool'");
  }

  ctx->loop_depth++;
  sema_stmt(ctx, stmt->stmt_while.body);
  ctx->loop_depth--;
}

// Analyses a statement.
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
  AstDeclFunction *func_decl = &decl->decl_function;

  Type *return_type = sema_type(ctx, func_decl->declared_return_type);

  Type **param_types = arena_alloc(&ctx->comp_unit->arena, sizeof(Type *) * func_decl->param_count);
  for (uint32_t i = 0; i < func_decl->param_count; i++) {
    param_types[i] = sema_type(ctx, func_decl->params[i].declared_type);

    if (!type_has_storage(param_types[i])) {
      sema_error(ctx,
                 func_decl->params[i].loc,
                 "Cannot declare parameter '%s' of type '%s'",
                 func_decl->params[i].name,
                 param_types[i]->name);
      param_types[i] = type_error;
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
        .param_count = func_decl->param_count,
      },
  };

  Symbol *sym = scope_insert(ctx, func_decl->name, SYMBOL_FUNCTION, func_type, decl->loc);
  if (!sym) {
    sema_error(ctx, decl->loc, "Redeclaration of function '%s' in current scope", func_decl->name);

    const char *sym_name = st_intern(&ctx->comp_unit->intern_table, func_decl->name);
    sym = symbol_new(ctx, sym_name, SYMBOL_FUNCTION, func_type, decl->loc);
  }

  sym->type = func_type;
  sym->is_defined = func_decl->body != NULL;
  sym->function.decl = decl;
  func_decl->symbol = sym;
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

  AstDeclFunction *func_decl = &decl->decl_function;
  AstDeclFunction *prev_func = ctx->current_function;

  ctx->current_function = func_decl;
  ctx->current_scope = scope_enter(ctx, ctx->current_scope);

  Type *func_type = func_decl->symbol->type;

  for (uint32_t i = 0; i < func_decl->param_count; i++) {
    AstParam *param = &func_decl->params[i];
    Type *param_type = func_type->function.param_types[i];

    if (sema_reject_type_name(ctx, param->name, param->loc)) {
      scope_insert(ctx, param->name, SYMBOL_LOCAL_VAR, type_error, param->loc);
      continue;
    }

    Symbol *sym = scope_insert(ctx, param->name, SYMBOL_LOCAL_VAR, param_type, param->loc);

    if (!sym) {
      sema_error(ctx, param->loc, "Redeclaration of parameter '%s' in function '%s'", param->name, func_decl->name);
      continue;
    }

    sym->is_defined = true;
    sym->local.is_param = true;

    if (type_is_aggregate(param_type)) {
      sym->local.in_memory = true;
    }

    param->symbol = sym;
  }

  if (func_decl->body) {
    sema_stmt(ctx, decl->decl_function.body);
  }

  ctx->current_scope = scope_exit(ctx->current_scope);
  ctx->current_function = prev_func;
}

static void sema_pass2_decl(SemaContext *ctx, AstDecl *decl) {
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

// Resolves a type name such as i32.
static Type *sema_type_named(SemaContext *ctx, AstType *type) {
  assert(type->named && "the parser always provides the name of a named type");

  Symbol *sym = scope_lookup_type_name(ctx, type->named);
  if (!sym) {
    sema_error(ctx, type->loc, "Undefined type '%s'", type->named);
    return type_error;
  }

  if (sym->kind != SYMBOL_TYPE) {
    const char *what = sym->kind == SYMBOL_FUNCTION ? "a function" : "a variable";
    sema_error(ctx, type->loc, "'%s' is %s, not a type (declared at line %u)", type->named, what, sym->loc.line);
    return type_error;
  }

  return sym->type;
}

// Resolves `*T`.
static Type *sema_type_pointer(SemaContext *ctx, AstType *type) {
  Type *base_type = sema_type(ctx, type->pointer.base_type);

  if (base_type == type_error) {
    return type_error;
  }

  return type_pointer_to(&ctx->comp_unit->arena, base_type);
}

// Resolves `[N]T`: the size must be a positive integer literal.
static Type *sema_type_array(SemaContext *ctx, AstType *type) {
  if (!type->array.length_expr) {
    sema_error(ctx, type->loc, "Array with inferred length '[_]' must have an initializer");
    return type_error;
  }

  Type *element_type = sema_type(ctx, type->array.elem_type);
  Type *size_type = sema_expr(ctx, type->array.length_expr);

  if (element_type == type_error || size_type == type_error) {
    return type_error;
  }

  if (!type_is_integer(size_type)) {
    sema_error(ctx, type->array.length_expr->loc, "Array size must be an integer, got '%s'", size_type->name);
    return type_error;
  }

  if (type->array.length_expr->kind != AST_EXPR_LITERAL) {
    sema_error(ctx, type->array.length_expr->loc, "Array size must be an integer literal");
    return type_error;
  }

  int64_t size_value = type->array.length_expr->expr_literal.int_value;

  if (size_value == 0) {
    sema_error(ctx, type->array.length_expr->loc, "Array size must be greater than zero");
    return type_error;
  }

  return sema_array_of(ctx, type->array.length_expr->loc, element_type, size_value);
}

// Resolves a written type to a Type: no type means void.
Type *sema_type(SemaContext *ctx, AstType *type) {
  if (!type) {
    return type_void;
  }

  switch (type->kind) {
  case AST_TYPE_INVALID:
    sema_error(ctx, type->loc, "Invalid type");
    return type_error;
  case AST_TYPE_NAMED:
    return sema_type_named(ctx, type);
  case AST_TYPE_POINTER:
    return sema_type_pointer(ctx, type);
  case AST_TYPE_ARRAY:
    return sema_type_array(ctx, type);
  }

  sema_error(ctx, type->loc, "Unknown type kind");
  return type_error;
}

// Semantic analysis functions

static void sema_init_builtins(SemaContext *ctx) {
#define TYPE_DEFINE(kind, size, ident)                            \
  if (!type_is_internal(type_##ident)) {                          \
    scope_insert_type(ctx, #ident, type_##ident, (SourceLoc){0}); \
  }
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
  for (uint32_t i = 0; i < program->decl_count; i++) {
    sema_pass1_decl(ctx, program->decls[i]);
  }

  for (uint32_t i = 0; i < program->decl_count; i++) {
    sema_pass2_decl(ctx, program->decls[i]);
  }
}