#ifndef TYPES_H
#define TYPES_H

#include <stdbool.h>
#include <stdint.h>

#define TYPES_BUILTIN(X)  \
  X(TYPE_ERROR, 0, error) \
  X(TYPE_VOID, 0, void)   \
  X(TYPE_I64, 8, i64)     \
  X(TYPE_F64, 8, f64)     \
  X(TYPE_BOOL, 1, bool)   \
  X(TYPE_PTR, 8, ptr)

typedef enum {
#define TYPE_ENUM(kind, size, ident) kind,
  TYPES_BUILTIN(TYPE_ENUM)
#undef X
} TypeKind;

typedef struct Type_ {
  TypeKind kind;
  uint32_t size;
  const char *name;
} Type;

#define TYPE_EXTERN(kind, size, ident) extern Type *TYPE_##ident;
TYPES_BUILTIN(TYPE_EXTERN)
#undef TYPE_EXTERN

bool type_is_void(Type *type);
bool type_is_integer(Type *type);
bool type_is_float(Type *type);
bool type_is_bool(Type *type);
bool type_is_numeric(Type *type);
bool type_is_pointer(Type *type);

bool type_is_orderable(Type *type);
bool type_is_comparable(Type *type);

bool type_equals(Type *a, Type *b);

#endif // TYPES_H