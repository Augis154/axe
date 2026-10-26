#ifndef IR_H
#define IR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  IR_OP_NONE = 0,

  IR_OP_LOAD_LITERAL,
  IR_OP_LOAD_FLOAT,

  IR_OP_LOAD_VAR,
  IR_OP_STORE_VAR,

  IR_OP_NEGATE,
  IR_OP_NOT,

  IR_OP_ADD,
  IR_OP_SUB,
  IR_OP_MUL,
  IR_OP_DIV,

  IR_OP_CMP_EQ,
  IR_OP_CMP_NE,
  IR_OP_CMP_LT,
  IR_OP_CMP_GT,
  IR_OP_CMP_LE,
  IR_OP_CMP_GE,

  IR_OP_LABEL,
  IR_OP_JUMP,
  IR_OP_JZ,
  IR_OP_JNZ,

  IR_OP_CALL,
  IR_OP_RETURN,

  IR_OP_ITOF,
  IR_OP_SEXT,
  IR_OP_ZEXT,
  IR_OP_TRUNC,
  IR_OP_FEXT,
  IR_OP_FTRUNC,
} IrOp;

typedef enum {
  IR_TYPE_NONE = 0,

  IR_TYPE_I32,
  IR_TYPE_I64,
  IR_TYPE_F32,
  IR_TYPE_F64,
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
  IrReg reg;
  IrType type;
} IrFuncArg;

typedef struct {
  IrOp op;

  union {
    IrReg res;
    IrLabel target;
    struct {
      IrLabel target_true;
      IrLabel target_false;
    };
  };

  union {
    struct {
      IrReg arg1;
      IrReg arg2;
    };

    int64_t literal;
    double float_literal;

    const char *var_name;

    struct {
      const char *callee;
      IrFuncArg *args;
      size_t arg_count;
    } call;
  };
} IrInstr;

typedef struct {
  const char *name;
  IrType type;
  IrReg reg;
} IrFuncParam;

typedef struct {
  const char *name;
  IrType return_type;

  IrFuncParam *params;
  uint32_t param_count;

  IrInstr *buffer;
  IrType *reg_types;
} IrFunction;

typedef struct {
  IrFunction *functions;
} IrModule;

#endif // IR_H
