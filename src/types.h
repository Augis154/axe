#ifndef TYPES_H
#define TYPES_H

#include "common.h"
#include "lib/arena.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TYPES_BUILTIN(X)                       \
  X(TYPE_KIND_ERROR, 0, error)                 \
  X(TYPE_KIND_VOID, 0, void)                   \
  X(TYPE_KIND_UNTYPED_INT, 8, untyped_int)     \
  X(TYPE_KIND_UNTYPED_FLOAT, 8, untyped_float) \
  X(TYPE_KIND_UNTYPED_NULL, 8, untyped_null)   \
                                               \
  X(TYPE_KIND_I32, 4, i32)                     \
  X(TYPE_KIND_I64, 8, i64)                     \
  X(TYPE_KIND_F32, 4, f32)                     \
  X(TYPE_KIND_F64, 8, f64)                     \
                                               \
  X(TYPE_KIND_BOOL, 1, bool)

typedef enum {
#define TYPE_ENUM(kind, size, ident) kind,
  TYPES_BUILTIN(TYPE_ENUM)
#undef TYPE_ENUM

  // Kinds built from other types
  TYPE_KIND_FUNCTION,
  TYPE_KIND_POINTER,
} TypeKind;

typedef struct {
  Type *base;
} TypePointer;

typedef struct {
  Type *return_type;
  Type **param_types;

  size_t param_count;
  bool is_variadic;
} TypeFunction;

struct Type_ {
  TypeKind kind;
  uint32_t size;
  const char *name;

  union {
    TypePointer pointer;
    TypeFunction function;
  };
};

#define TYPE_EXTERN(kind, size, ident) extern Type *const type_##ident;
TYPES_BUILTIN(TYPE_EXTERN)
#undef TYPE_EXTERN

extern Type *const type_void_ptr;

// What kind a type is
bool type_is_void(Type *type);
bool type_is_bool(Type *type);
bool type_is_integer(Type *type);
bool type_is_float(Type *type);
bool type_is_numeric(Type *type);
bool type_is_pointer(Type *type);

// Compiler-internal types
bool type_is_untyped(Type *type);
bool type_is_internal(Type *type);

// Properties
bool type_has_storage(Type *type);

// Which operators a type supports
bool type_is_comparable(Type *type);
bool type_is_orderable(Type *type);

// Relations between types
bool type_equals(Type *a, Type *b);
bool type_can_widen_to(Type *from, Type *to);
Type *type_common(Type *a, Type *b);

// Building and deriving types

// Builds the type `*base`
Type *type_pointer_to(Arena *arena, Type *base);
Type *type_default(Type *type);

#endif // TYPES_H