#include "types.h"

#define TYPE_DEFINE(kind, size, ident) static Type TYPE_##ident##_DEF = {kind, size, #ident};
TYPES_BUILTIN(TYPE_DEFINE)
#undef TYPE_DEFINE

#define TYPE_PTRS(kind, size, ident) Type *TYPE_##ident = &TYPE_##ident##_DEF;
TYPES_BUILTIN(TYPE_PTRS)
#undef TYPE_PTRS

bool type_is_void(Type *type) {
  return type == TYPE_void;
}

bool type_is_integer(Type *type) {
  if (!type) return false;
  if (type == TYPE_error) return true;
  return type == TYPE_i64 || type == TYPE_i32;
}

bool type_is_float(Type *type) {
  if (!type) return false;
  if (type == TYPE_error) return true;
  return type == TYPE_f64 || type == TYPE_f32;
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