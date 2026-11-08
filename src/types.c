#include "types.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Builtin types

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
  .pointer = {.base_type = &type_void_def},
};
Type *const type_void_ptr = &type_void_ptr_def;

// What kind a type is

bool type_is_void(Type *type) {
  return type == type_void;
}

bool type_is_bool(Type *type) {
  if (!type) return false;
  if (type == type_error) return true;
  return type == type_bool;
}

bool type_is_integer(Type *type) {
  return type_is_unsigned_integer(type) || type_is_signed_integer(type);
}

bool type_is_signed_integer(Type *type) {
  if (!type) return false;
  if (type == type_error) return true;
  return type == type_i8 || type == type_i16 || type == type_i32 || type == type_i64 || type == type_untyped_int;
}

bool type_is_unsigned_integer(Type *type) {
  if (!type) return false;
  if (type == type_error) return true;
  return type == type_u8 || type == type_u16 || type == type_u32 || type == type_u64 || type == type_untyped_int;
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

bool type_is_array(Type *type) {
  if (!type) return false;
  return type->kind == TYPE_KIND_ARRAY;
}

bool type_is_slice(Type *type) {
  if (!type) return false;
  return type->kind == TYPE_KIND_SLICE;
}

bool type_is_array_like(Type *type) {
  return type_is_array(type) || type_is_slice(type);
}

bool type_is_aggregate(Type *type) {
  return type_is_array(type) || type_is_slice(type);
}

// Compiler-internal types

bool type_is_untyped(Type *type) {
  return type == type_untyped_int || type == type_untyped_float || type == type_null || type == type_undef;
}

bool type_is_internal(Type *type) {
  return type == type_error || type_is_untyped(type);
}

// Properties

bool type_has_storage(Type *type) {
  if (!type) return false;
  return type == type_error || type->size > 0;
}

// Which operators a type supports

bool type_is_comparable(Type *type) {
  if (!type) return false;
  if (type == type_error) return true;
  return type_is_numeric(type) || type_is_bool(type) || type_is_pointer(type) || type == type_null;
}

bool type_is_orderable(Type *type) {
  if (!type) return false;
  if (type == type_error) return true;
  return type_is_numeric(type) || type_is_pointer(type);
}

// Relations between types

bool type_equals(Type *a, Type *b) {
  if (a == b) return true;
  if (!a || !b) return false;
  if (a == type_error || b == type_error) return true;

  if (type_is_pointer(a) && type_is_pointer(b)) {
    return type_equals(a->pointer.base_type, b->pointer.base_type);
  }

  if (type_is_array(a) && type_is_array(b)) {
    return a->array.length == b->array.length && type_equals(a->array.elem_type, b->array.elem_type);
  }

  if (type_is_slice(a) && type_is_slice(b)) {
    return type_equals(a->slice.elem_type, b->slice.elem_type);
  }

  return false;
}

// Implicit conversions: every value of `from` is representable in `to`
bool type_can_widen_to(Type *from, Type *to) {
  if (!from || !to) return false;
  if (type_equals(from, to)) return true;

  if (from == type_null) return type_is_pointer(to);
  if (to == type_null) return false;

  if (to == type_untyped_int) return false;
  if (to == type_untyped_float) return from == type_untyped_int;

  if (from == type_untyped_int) return type_is_integer(to) || type_is_float(to);
  if (from == type_untyped_float) return type_is_float(to);

  if (type_is_integer(from) && type_is_integer(to)) {
    if (type_is_signed_integer(from)) return type_is_signed_integer(to) && to->size >= from->size;
    return type_is_unsigned_integer(to) ? to->size >= from->size : to->size > from->size;
  }

  if (type_is_float(from) && type_is_float(to)) {
    return to->size >= from->size;
  }

  if (type_is_integer(from) && type_is_float(to)) {
    return to->size >= from->size;
  }

  // Any pointer converts implicitly to `*void`; the way back needs an explicit cast.
  if (type_is_pointer(from) && type_is_pointer(to)) {
    return type_is_void(to->pointer.base_type);
  }

  // An array converts to a pointer to its elements (or to `*void`); it is already represented by its address.
  if (type_is_array(from) && type_is_pointer(to)) {
    return type_is_void(to->pointer.base_type) || type_equals(from->array.elem_type, to->pointer.base_type);
  }

  if (type_is_array(from) && type_is_slice(to)) {
    return type_equals(from->array.elem_type, to->slice.elem_type);
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

  if (a == type_null && type_is_pointer(b)) return b;
  if (b == type_null && type_is_pointer(a)) return a;

  if (!type_is_numeric(a) || !type_is_numeric(b)) return NULL;

  if (type_can_widen_to(a, b)) return b;
  if (type_can_widen_to(b, a)) return a;

  return NULL;
}

// Building and deriving types

Type *type_pointer_to(Arena *arena, Type *base_type) {
  size_t name_len = strlen(base_type->name) + 2;
  char *name = arena_alloc(arena, name_len);
  snprintf(name, name_len, "*%s", base_type->name);

  Type *ptr_type = arena_alloc(arena, sizeof(Type));
  *ptr_type = (Type){
    .kind = TYPE_KIND_POINTER,
    .name = name,
    .size = 8,
    .pointer = {.base_type = base_type},
  };

  return ptr_type;
}

Type *type_array_of(Arena *arena, Type *elem_type, uint32_t length) {
  size_t name_len = strlen(elem_type->name) + 16;
  char *name = arena_alloc(arena, name_len);
  snprintf(name, name_len, "[%u]%s", length, elem_type->name);

  Type *array_type = arena_alloc(arena, sizeof(Type));
  *array_type = (Type){
    .kind = TYPE_KIND_ARRAY,
    .name = name,
    .size = elem_type->size * length,
    .array = {.elem_type = elem_type, .length = length},
  };

  return array_type;
}

Type *type_slice_of(Arena *arena, Type *elem_type) {
  size_t name_len = strlen(elem_type->name) + 3;
  char *name = arena_alloc(arena, name_len);
  snprintf(name, name_len, "[]%s", elem_type->name);

  Type *slice_type = arena_alloc(arena, sizeof(Type));
  *slice_type = (Type){
    .kind = TYPE_KIND_SLICE,
    .name = name,
    .size = 16,
    .slice = {.elem_type = elem_type},
  };

  return slice_type;
}

Type *type_elem(Type *type) {
  assert(type_is_array_like(type) && "type_elem: type is neither an array nor a slice");

  return type_is_array(type) ? type->array.elem_type : type->slice.elem_type;
}

Type *type_default(Type *type) {
  if (type == type_untyped_int) return type_i32;
  if (type == type_untyped_float) return type_f64;
  if (type == type_null) return type_void_ptr;
  return type;
}

bool type_find_field(Arena *arena, Type *type, const char *name, TypeField *field) {
  if (type_is_slice(type)) {
    if (strcmp(name, "ptr") == 0) {
      field->offset = SLICE_PTR_OFFSET;
      field->type = type_pointer_to(arena, type->slice.elem_type);
      return true;
    }
    if (strcmp(name, "len") == 0) {
      field->offset = SLICE_LEN_OFFSET;
      field->type = type_i64;
      return true;
    }
    return false;
  }

  return false;
}
