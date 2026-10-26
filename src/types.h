#ifndef TYPES_H
#define TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TYPES_BUILTIN(X)                       \
  X(TYPE_KIND_ERROR, 0, error)                 \
  X(TYPE_KIND_VOID, 0, void)                   \
  X(TYPE_KIND_UNTYPED_INT, 8, untyped_int)     \
  X(TYPE_KIND_UNTYPED_FLOAT, 8, untyped_float) \
                                               \
  X(TYPE_KIND_I32, 4, i32)                     \
  X(TYPE_KIND_I64, 8, i64)                     \
  X(TYPE_KIND_F32, 4, f32)                     \
  X(TYPE_KIND_F64, 8, f64)                     \
                                               \
  X(TYPE_KIND_BOOL, 1, bool)

typedef enum {
  TYPE_KIND_FUNCTION,

#define TYPE_ENUM(kind, size, ident) kind,
  TYPES_BUILTIN(TYPE_ENUM)
#undef TYPE_ENUM
} TypeKind;

typedef struct {
  struct Type_ *return_type;
  struct Type_ **param_types;

  size_t param_count;
  bool is_variadic;
} TypeFunction;

typedef struct Type_ {
  TypeKind kind;
  uint32_t size;
  const char *name;

  union {
    TypeFunction function;
  };
} Type;

#define TYPE_EXTERN(kind, size, ident) extern Type *TYPE_##ident;
TYPES_BUILTIN(TYPE_EXTERN)
#undef TYPE_EXTERN

bool type_is_void(Type *type);
bool type_is_integer(Type *type);
bool type_is_float(Type *type);
bool type_is_bool(Type *type);
bool type_is_numeric(Type *type);

bool type_is_orderable(Type *type);
bool type_is_comparable(Type *type);

bool type_equals(Type *a, Type *b);

bool type_can_widen_to(Type *from, Type *to);

Type *get_widened_numeric_type(Type *a, Type *b);

#endif // TYPES_H