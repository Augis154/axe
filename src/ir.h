#ifndef IR_H
#define IR_H

#include "ast.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
  IR_INST_NONE = 0,

  IR_INST_LOAD_LITERAL,
  IR_INST_LOAD_VAR,

  IR_INST_NEGATE,

  IR_INST_ADD,
  IR_INST_SUB,
  IR_INST_MUL,
  IR_INST_DIV,
} IrOp;

typedef struct {
  uint32_t id;
} IrReg;

#define NULL_REG ((IrReg){0})

typedef struct {
  IrOp op;
  IrReg res;

  union {
    struct {
      IrReg arg1;
      IrReg arg2;
    };
    int64_t literal;
  };
} IrInstr;

typedef struct {
  IrInstr *buffer;
  Arena *ast_arena;

  uint32_t next_reg;
} IrContext;

void ir_gen_init(IrContext *context, Arena *ast_arena);
void ir_buffer_free(IrContext *context);

void emit_ir(IrContext *context, AstExpr *root);

#endif // IR_H
