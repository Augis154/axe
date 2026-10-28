#include "types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TYPE_DEFINE(type_kind, type_size, ident)                                           \
  static Type type_##ident##_def = {.kind = type_kind, .size = type_size, .name = #ident};
TYPES_BUILTIN(TYPE_DEFINE)
#undef TYPE_DEFINE

#define TYPE_PTRS(kind, size, ident) Type *const type_##ident = &type_##ident##_def;
TYPES_BUILTIN(TYPE_PTRS)
#undef TYPE_PTRS

static Type type_void_ptr_def = {
  .kind = TYPE_KIND_POINTER,
  .size = 8,
  .name = "*void",
  .pointer = {.base = &type_void_def},
};
Type *const type_void_ptr = &type_void_ptr_def;

bool type_is_void(Type *type) {
  return type == type_void;
}

bool type_is_bool(Type *type) {
  if (!type) return false;
  if (type == type_error) return true;
  return type == type_bool;
}

bool type_is_integer(Type *type) {
  if (!type) return false;
  if (type == type_error) return true;
  return type == type_i64 || type == type_i32 || type == type_untyped_int;
}

bool type_is_float(Type *type) {
  if (!type) return false;
  if (type == type_error) return true;
  return type == type_f64 || type == type_f32 || type == type_untyped_float;
}

bool type_is_numeric(Type *type) {
  if (!type) return false;
  if (type == type_error) return true;
  return type_is_integer(type) || type_is_float(type);
}

bool type_is_pointer(Type *type) {
  if (!type) return false;
  return type->kind == TYPE_KIND_POINTER;
}

bool type_is_untyped(Type *type) {
  return type == type_untyped_int || type == type_untyped_float || type == type_untyped_null;
}

bool type_is_internal(Type *type) {
  return type == type_error || type_is_untyped(type);
}

bool type_has_storage(Type *type) {
  if (!type) return false;
  return type == type_error || type->size > 0;
}

bool type_is_comparable(Type *type) {
  if (!type) return false;
  if (type == type_error) return true;
  return type_is_numeric(type) || type_is_bool(type) || type_is_pointer(type) || type == type_untyped_null;
}

bool type_is_orderable(Type *type) {
  if (!type) return false;
  if (type == type_error) return true;
  return type_is_numeric(type) || type_is_pointer(type);
}

bool type_equals(Type *a, Type *b) {
  if (a == b) return true;
  if (!a || !b) return false;
  if (a == type_error || b == type_error) return true;

  if (type_is_pointer(a) && type_is_pointer(b)) {
    return type_equals(a->pointer.base, b->pointer.base);
  }

  return false;
}

// Implicit conversions: every value of `from` is representable in `to`
bool type_can_widen_to(Type *from, Type *to) {
  if (!from || !to) return false;
  if (type_equals(from, to)) return true;

  if (from == type_untyped_null) return type_is_pointer(to);
  if (to == type_untyped_null) return false;

  if (to == type_untyped_int) return false;
  if (to == type_untyped_float) return from == type_untyped_int;

  if (from == type_untyped_int) return type_is_integer(to) || type_is_float(to);
  if (from == type_untyped_float) return type_is_float(to);

  if (type_is_integer(from) && type_is_integer(to)) {
    return to->size >= from->size;
  }

  if (type_is_float(from) && type_is_float(to)) {
    return to->size >= from->size;
  }

  if (type_is_integer(from) && type_is_float(to)) {
    return to->size >= from->size;
  }

  // Any pointer converts implicitly to `*void`; the way back needs an explicit cast.
  if (type_is_pointer(from) && type_is_pointer(to)) {
    return type_is_void(to->pointer.base);
  }

  return false;
}

Type *type_common(Type *a, Type *b) {
  if (a == b) return a;

  if (type_is_pointer(a) && type_is_pointer(b)) {
    if (type_can_widen_to(a, b)) return b;
    if (type_can_widen_to(b, a)) return a;
    return NULL;
  }

  if (a == type_untyped_null && type_is_pointer(b)) return b;
  if (b == type_untyped_null && type_is_pointer(a)) return a;

  if (!type_is_numeric(a) || !type_is_numeric(b)) return NULL;

  if (type_can_widen_to(a, b)) return b;
  if (type_can_widen_to(b, a)) return a;

  return NULL;
}

Type *type_pointer_to(Arena *arena, Type *base) {
  size_t name_len = strlen(base->name) + 2;
  char *name = arena_alloc(arena, name_len);
  snprintf(name, name_len, "*%s", base->name);

  Type *ptr_type = arena_alloc(arena, sizeof(Type));
  *ptr_type = (Type){
    .kind = TYPE_KIND_POINTER,
    .name = name,
    .size = 8,
    .pointer = {.base = base},
  };

  return ptr_type;
}

Type *type_default(Type *type) {
  if (type == type_untyped_int) return type_i32;
  if (type == type_untyped_float) return type_f64;
  if (type == type_untyped_null) return type_void_ptr;
  return type;
}
