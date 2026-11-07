#ifndef IR_H
#define IR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  IR_OP_NONE = 0,

  // Define a register from a constant or another register
  IR_OP_LOAD_INT,
  IR_OP_LOAD_FLOAT,
  IR_OP_COPY,

  // Memory: stack slots and access through an address
  IR_OP_ALLOC,
  IR_OP_LOAD,
  IR_OP_STORE,
  IR_OP_MEMCPY,
  IR_OP_MEMSET,

  // Unary arithmetic and logic
  IR_OP_NEGATE,
  IR_OP_NOT,
  IR_OP_BIT_NOT,

  // Binary arithmetic
  IR_OP_ADD,
  IR_OP_SUB,
  IR_OP_MUL,
  IR_OP_DIV,

  // Comparisons; the result is a bool
  IR_OP_CMP_EQ,
  IR_OP_CMP_NE,
  IR_OP_CMP_LT,
  IR_OP_CMP_GT,
  IR_OP_CMP_LE,
  IR_OP_CMP_GE,

  // Bitwise operations
  IR_OP_BIT_OR,
  IR_OP_BIT_XOR,
  IR_OP_BIT_AND,
  IR_OP_SHL,
  IR_OP_SHR,

  // Conversions between integer and float
  IR_OP_ITOF,
  IR_OP_FTOI,

  // Conversions that change the width of an integer
  IR_OP_SEXT,
  IR_OP_ZEXT,
  IR_OP_TRUNC,

  // Conversions that change the width of a float
  IR_OP_FEXT,
  IR_OP_FTRUNC,

  // Control flow within a function
  IR_OP_LABEL,
  IR_OP_JUMP,
  IR_OP_JZ,
  IR_OP_JNZ,

  // Calls and returns
  IR_OP_CALL,
  IR_OP_RETURN,
} IrOp;

typedef struct {
  uint32_t id;
} IrReg;

typedef struct {
  uint32_t id;
} IrLabel;

#define NULL_REG ((IrReg){0})
#define NULL_LABEL ((IrLabel){0})

typedef struct {
  const char *callee;
  IrReg *args;
  uint32_t arg_count;
} IrCall;

typedef struct {
  IrOp op;

  IrReg res;
  IrReg arg1;
  IrReg arg2;

  union {
    int64_t int_lit;
    double float_lit;
    IrCall call;
    IrLabel target;
    struct {
      IrLabel target_true;
      IrLabel target_false;
    };
  };
} IrInstr;

typedef enum {
  IR_TYPE_NONE = 0,

  IR_TYPE_INT,
  IR_TYPE_FLOAT,
  IR_TYPE_PTR,
  IR_TYPE_AGG,
} IrTypeKind;

typedef struct {
  IrTypeKind kind;
  uint32_t size;   // In bytes. INT: 1, 2, 4 or 8. FLOAT: 4 or 8. PTR: 8. Unused for AGG.
  bool is_signed;  // INT only
  uint32_t agg_id; // AGG only: the index into IrModule.aggregates
} IrType;

// Constructors for the types
#define IR_VOID ((IrType){0})

#define IR_INT(bytes, sign) ((IrType){.kind = IR_TYPE_INT, .size = (bytes), .is_signed = (sign)})
#define IR_FLOAT(bytes) ((IrType){.kind = IR_TYPE_FLOAT, .size = (bytes)})
#define IR_PTR ((IrType){.kind = IR_TYPE_PTR, .size = 8})
#define IR_AGG(id) ((IrType){.kind = IR_TYPE_AGG, .agg_id = (id)})

#define IR_I32 IR_INT(4, true)
#define IR_I64 IR_INT(8, true)
#define IR_F32 IR_FLOAT(4)
#define IR_F64 IR_FLOAT(8)

// An array is a single field; a struct has one field per member.
typedef struct {
  IrType type;
  uint32_t count;
} IrAggField;

// An aggregate type, emitted as a QBE `type :name = align N { ... }`.
typedef struct {
  const char *name;
  uint32_t alignment;
  IrAggField *fields;
} IrAggregate;

typedef struct {
  const char *name;
  IrType return_type;

  IrReg *params;
  uint32_t param_count;

  IrInstr *buffer;
  IrType *reg_types; // Indexed by register id; entry 0 is unused
} IrFunction;

typedef struct {
  IrFunction *functions;
  IrAggregate *aggregates;
} IrModule;

// Type helpers

static inline bool ir_type_is_void(IrType type) {
  return type.kind == IR_TYPE_NONE;
}

static inline bool ir_type_is_float(IrType type) {
  return type.kind == IR_TYPE_FLOAT;
}

static inline bool ir_type_equals(IrType a, IrType b) {
  return a.kind == b.kind && a.size == b.size && a.is_signed == b.is_signed && a.agg_id == b.agg_id;
}

#endif // IR_H
