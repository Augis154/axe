#include "types.h"

#define TYPE_DEFINE(type_kind, type_size, ident)                                           \
  static Type TYPE_##ident##_DEF = {.kind = type_kind, .size = type_size, .name = #ident};
TYPES_BUILTIN(TYPE_DEFINE)
#undef TYPE_DEFINE

#define TYPE_PTRS(kind, size, ident) Type *TYPE_##ident = &TYPE_##ident##_DEF;
TYPES_BUILTIN(TYPE_PTRS)
#undef TYPE_PTRS

bool type_is_void(Type *type) {
  return type == TYPE_void;
}

bool type_has_storage(Type *type) {
  return type == TYPE_error || type->size > 0;
}

bool type_is_untyped(Type *type) {
  return type == TYPE_untyped_int || type == TYPE_untyped_float;
}

bool type_is_integer(Type *type) {
  if (!type) return false;
  if (type == TYPE_error) return true;
  return type == TYPE_i64 || type == TYPE_i32 || type == TYPE_untyped_int;
}

bool type_is_float(Type *type) {
  if (!type) return false;
  if (type == TYPE_error) return true;
  return type == TYPE_f64 || type == TYPE_f32 || type == TYPE_untyped_float;
}

bool type_is_bool(Type *type) {
  if (!type) return false;
  if (type == TYPE_error) return true;
  return type == TYPE_bool;
}

bool type_is_numeric(Type *type) {
  if (!type) return false;
  if (type == TYPE_error) return true;
  return type_is_integer(type) || type_is_float(type);
}

bool type_is_orderable(Type *type) {
  if (!type) return false;
  if (type == TYPE_error) return true;
  return type_is_numeric(type);
}

bool type_is_comparable(Type *type) {
  if (!type) return false;
  if (type == TYPE_error) return true;
  return type_is_numeric(type) || type_is_bool(type);
}

bool type_equals(Type *a, Type *b) {
  if (a == b) return true;
  if (!a || !b) return false;
  if (a == TYPE_error || b == TYPE_error) return true;
  return false;
}

Type *type_default(Type *type) {
  if (type == TYPE_untyped_int) return TYPE_i32;
  if (type == TYPE_untyped_float) return TYPE_f64;
  return type;
}

// Implicit conversions: every value of `from` is representable in `to`
bool type_can_widen_to(Type *from, Type *to) {
  if (type_equals(from, to)) return true;
  if (!from || !to) return false;

  if (to == TYPE_untyped_int) return false;
  if (to == TYPE_untyped_float) return from == TYPE_untyped_int;

  if (from == TYPE_untyped_int) return type_is_integer(to) || type_is_float(to);
  if (from == TYPE_untyped_float) return type_is_float(to);

  if (type_is_integer(from) && type_is_integer(to)) {
    return to->size >= from->size;
  }

  if (type_is_float(from) && type_is_float(to)) {
    return to->size >= from->size;
  }

  if (type_is_integer(from) && type_is_float(to)) {
    return to->size >= from->size;
  }

  return false;
}

Type *get_widened_numeric_type(Type *a, Type *b) {
  if (a == b) return a;
  if (!type_is_numeric(a) || !type_is_numeric(b)) return NULL;

  if (type_can_widen_to(a, b)) return b;
  if (type_can_widen_to(b, a)) return a;

  return NULL;
}