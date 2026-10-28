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

  // Unary arithmetic and logic
  IR_OP_NEGATE,
  IR_OP_NOT,

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

typedef enum {
  IR_TYPE_NONE = 0,

  IR_TYPE_I32,
  IR_TYPE_I64,
  IR_TYPE_F32,
  IR_TYPE_F64,
  IR_TYPE_PTR,
} IrType;

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
  size_t arg_count;
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

typedef struct {
  const char *name;
  IrType return_type;

  IrReg *params;
  uint32_t param_count;

  IrInstr *buffer;
  IrType *reg_types;
} IrFunction;

typedef struct {
  IrFunction *functions;
} IrModule;

#endif // IR_H
