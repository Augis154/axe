#ifndef GEN_IR_H
#define GEN_IR_H

#include "ast.h"
#include "compiler.h"
#include "ir.h"
#include "sema.h"

#include <stdbool.h>

typedef struct LoopLabels_ {
  IrLabel continue_label;
  IrLabel break_label;
  struct LoopLabels_ *parent;
} LoopLabels;

typedef struct {
  CompilationUnit *comp_unit;
  SemaContext *sema_ctx;

  IrModule *module;
  uint32_t current_func_idx;
  Symbol *current_func_sym;

  uint32_t next_reg;
  uint32_t next_label;

  IrReg *symbol_to_reg;
  IrInstr *entry_allocs;

  LoopLabels *current_loop;
  bool block_terminated;
} IrGenContext;

void gen_ir_init(IrGenContext *context, CompilationUnit *comp_unit, SemaContext *sema_ctx);
void gen_ir_free(IrGenContext *context);

void gen_ir(IrGenContext *ctx, AstProgram *program);

#endif // GEN_IR_H
