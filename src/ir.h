#ifndef IR_H
#define IR_H

#include "ast.h"
#include "compiler.h"

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
} IrOp;

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
  IrOp op;

  union {
    IrReg res;
    IrLabel target;
  };

  union {
    struct {
      IrReg arg1;
      IrReg arg2;
    };
    int64_t literal;
    const char *var_name;
  };
} IrInstr;

typedef struct {
  CompilationUnit *comp_unit;

  IrInstr *buffer;
  LoopLabels *current_loop;

  uint32_t next_reg;
  uint32_t next_label;

  bool block_terminated;
} IrContext;

void ir_gen_init(IrContext *context, CompilationUnit *comp_unit);
void ir_buffer_free(IrContext *context);

void emit_ir(IrContext *context, AstStmt *root);

#endif // IR_H
