#ifndef TYPES_H
#define TYPES_H

#include "common.h"
#include "lib/arena.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TYPES_BUILTIN(X)                       \
  X(TYPE_KIND_ERROR, 0, error)                 \
                                               \
  X(TYPE_KIND_U8, 1, u8)                       \
  X(TYPE_KIND_U16, 2, u16)                     \
  X(TYPE_KIND_U32, 4, u32)                     \
  X(TYPE_KIND_U64, 8, u64)                     \
  X(TYPE_KIND_I8, 1, i8)                       \
  X(TYPE_KIND_I16, 2, i16)                     \
  X(TYPE_KIND_I32, 4, i32)                     \
  X(TYPE_KIND_I64, 8, i64)                     \
  X(TYPE_KIND_F32, 4, f32)                     \
  X(TYPE_KIND_F64, 8, f64)                     \
  X(TYPE_KIND_BOOL, 1, bool)                   \
                                               \
  X(TYPE_KIND_UNTYPED_INT, 8, untyped_int)     \
  X(TYPE_KIND_UNTYPED_FLOAT, 8, untyped_float) \
  X(TYPE_KIND_NULL, 8, null)                   \
                                               \
  X(TYPE_KIND_VOID, 0, void)                   \
  X(TYPE_KIND_UNDEF, 0, undef)

typedef enum {
#define TYPE_ENUM(kind, size, ident) kind,
  TYPES_BUILTIN(TYPE_ENUM)
#undef TYPE_ENUM

  // Kinds built from other types
  TYPE_KIND_POINTER,
  TYPE_KIND_ARRAY,
  TYPE_KIND_SLICE,
  TYPE_KIND_FUNCTION,
} TypeKind;

typedef struct {
  Type *base_type;
} TypePointer;

typedef struct {
  Type *elem_type;
  uint32_t length;
} TypeArray;

typedef struct {
  Type *elem_type;
} TypeSlice;

typedef struct {
  Type *return_type;
  Type **param_types;

  uint32_t param_count;
  bool is_variadic;
} TypeFunction;

struct Type_ {
  TypeKind kind;
  uint32_t size;
  const char *name;

  union {
    TypePointer pointer;
    TypeArray array;
    TypeSlice slice;
    TypeFunction function;
  };
};

// The layout of a slice is `{ptr, len}`: the offsets in bytes of its two fields
#define SLICE_PTR_OFFSET 0
#define SLICE_LEN_OFFSET 8

// A member of a type: where it is in memory and its type
typedef struct {
  uint32_t offset;
  Type *type;
} TypeField;

#define TYPE_EXTERN(kind, size, ident) extern Type *const type_##ident;
TYPES_BUILTIN(TYPE_EXTERN)
#undef TYPE_EXTERN

extern Type *const type_void_ptr;

// What kind a type is
bool type_is_void(Type *type);
bool type_is_bool(Type *type);
bool type_is_integer(Type *type);
bool type_is_signed_integer(Type *type);
bool type_is_unsigned_integer(Type *type);
bool type_is_float(Type *type);
bool type_is_numeric(Type *type);
bool type_is_pointer(Type *type);
bool type_is_array(Type *type);
bool type_is_slice(Type *type);
// An array or a slice: something that can be indexed and sliced
bool type_is_array_like(Type *type);
// Values that are handled by the address of their storage instead of being loaded into a register
bool type_is_aggregate(Type *type);

// Compiler-internal types
bool type_is_untyped(Type *type);
bool type_is_internal(Type *type);

// Properties
bool type_has_storage(Type *type);

// Which operators a type supports
bool type_is_comparable(Type *type);
bool type_is_orderable(Type *type);

// Relations between types
// Same type; pointers and arrays are compared by their element types (and length)
bool type_equals(Type *a, Type *b);
// Whether a value of `from` converts implicitly to `to`
bool type_can_widen_to(Type *from, Type *to);
// The type both operands convert to, or NULL when they have none
Type *type_common(Type *a, Type *b);

// Building and deriving types
// Builds the type `*base`
Type *type_pointer_to(Arena *arena, Type *base_type);
// Builds the type `[length]element`
Type *type_array_of(Arena *arena, Type *elem_type, uint32_t length);
// Builds the type `[]element`
Type *type_slice_of(Arena *arena, Type *elem_type);
// The element type of an array or a slice
Type *type_elem(Type *type);
// The concrete type an untyped literal takes when nothing else decides it
Type *type_default(Type *type);

// Members of a type
// Finds the member `name` of `type`; false when it has none.
bool type_find_field(Arena *arena, Type *type, const char *name, TypeField *field);

#endif // TYPES_H