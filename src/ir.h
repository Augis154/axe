#ifndef IR_H
#define IR_H

#include "ast.h"
#include "compiler.h"
#include "sema.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
  IR_INST_NONE = 0,

  IR_INST_LOAD_LITERAL,

  IR_INST_LOAD_VAR,
  IR_INST_STORE_VAR,

  IR_INST_NEGATE,
  IR_INST_NOT,

  IR_INST_ADD,
  IR_INST_SUB,
  IR_INST_MUL,
  IR_INST_DIV,

  IR_INST_CMP_EQ,
  IR_INST_CMP_NE,
  IR_INST_CMP_LT,
  IR_INST_CMP_GT,
  IR_INST_CMP_LE,
  IR_INST_CMP_GE,

  IR_INST_LABEL,
  IR_INST_JUMP,
  IR_INST_JZ,
  IR_INST_JNZ,

  IR_INST_CALL,
  IR_INST_RETURN,
} IrOp;

typedef enum {
  IR_TYPE_NONE = 0,

  IR_TYPE_WORD,
  IR_TYPE_LONG,
  IR_TYPE_SPF, // Single-precision float
  IR_TYPE_DPF, // Double-precision float
} IrType;

typedef struct {
  uint32_t id;
} IrReg;

typedef struct {
  uint32_t id;
} IrLabel;

#define NULL_REG ((IrReg){0})
#define NULL_LABEL ((IrLabel){0})

typedef struct LoopLabels_ {
  IrLabel continue_label;
  IrLabel break_label;
  struct LoopLabels_ *parent;
} LoopLabels;

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

typedef struct {
  CompilationUnit *comp_unit;
  SemaContext *sema_ctx;

  IrModule *module;
  uint32_t current_func_idx;

  uint32_t next_reg;
  uint32_t next_label;

  LoopLabels *current_loop;
  bool block_terminated;
} IrContext;

void ir_init(IrContext *context, CompilationUnit *comp_unit, SemaContext *sema_ctx);
void ir_free(IrContext *context);

void emit_ir(IrContext *ctx, AstProgram *program);

#endif // IR_H
