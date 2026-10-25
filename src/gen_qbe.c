#include "backend.h"
#include "lib/array.h"
#include <stdint.h>
#include <stdio.h>

static void emit_ir_instr(FILE *file, IrContext *ctx, IrInstr instr) {
  switch (instr.op) {
  case IR_INST_NONE:
    fprintf(file, "\n");
    return;

  case IR_INST_LOAD_LITERAL:
    fprintf(file, "  %%r%u =w copy %ld\n", instr.res.id, instr.literal);
    return;

  case IR_INST_LOAD_VAR:
    fprintf(file, "  %%r%u =w copy %%%s\n", instr.res.id, instr.var_name);
    return;
  case IR_INST_STORE_VAR:
    fprintf(file, "  %%%s =w copy %%r%u\n", instr.var_name, instr.res.id);
    return;

  case IR_INST_NEGATE:
    fprintf(file, "  %%r%u =w neg %%r%u\n", instr.res.id, instr.arg1.id);
    return;
  case IR_INST_NOT:
    fprintf(file, "  %%r%u =w ceqw %%r%u, 0\n", instr.res.id, instr.arg1.id);
    return;

  case IR_INST_ADD:
    fprintf(file, "  %%r%u =w add %%r%u, %%r%u\n", instr.res.id, instr.arg1.id, instr.arg2.id);
    return;
  case IR_INST_SUB:
    fprintf(file, "  %%r%u =w sub %%r%u, %%r%u\n", instr.res.id, instr.arg1.id, instr.arg2.id);
    return;
  case IR_INST_MUL:
    fprintf(file, "  %%r%u =w mul %%r%u, %%r%u\n", instr.res.id, instr.arg1.id, instr.arg2.id);
    return;
  case IR_INST_DIV:
    fprintf(file, "  %%r%u =w div %%r%u, %%r%u\n", instr.res.id, instr.arg1.id, instr.arg2.id);
    return;

  case IR_INST_CMP_EQ:
    fprintf(file, "  %%r%u =w ceqw %%r%u, %%r%u\n", instr.res.id, instr.arg1.id, instr.arg2.id);
    return;
  case IR_INST_CMP_NE:
    fprintf(file, "  %%r%u =w cnew %%r%u, %%r%u\n", instr.res.id, instr.arg1.id, instr.arg2.id);
    return;
  case IR_INST_CMP_LT:
    fprintf(file, "  %%r%u =w csltw %%r%u, %%r%u\n", instr.res.id, instr.arg1.id, instr.arg2.id);
    return;
  case IR_INST_CMP_GT:
    fprintf(file, "  %%r%u =w csgtw %%r%u, %%r%u\n", instr.res.id, instr.arg1.id, instr.arg2.id);
    return;
  case IR_INST_CMP_LE:
    fprintf(file, "  %%r%u =w cslew %%r%u, %%r%u\n", instr.res.id, instr.arg1.id, instr.arg2.id);
    return;
  case IR_INST_CMP_GE:
    fprintf(file, "  %%r%u =w csgew %%r%u, %%r%u\n", instr.res.id, instr.arg1.id, instr.arg2.id);
    return;

  case IR_INST_LABEL:
    fprintf(file, "@l%u\n", instr.target.id);
    return;
  case IR_INST_JUMP:
    fprintf(file, "  jmp @l%u\n", instr.target.id);
    return;
  case IR_INST_JZ: {
    uint32_t fallthrough_label = ctx->next_label++;
    fprintf(file, "  jnz %%r%u, @l%u, @l%u\n", instr.arg1.id, fallthrough_label, instr.target.id);
    fprintf(file, "@l%u\n", fallthrough_label);
    return;
  }
  case IR_INST_JNZ: {
    uint32_t fallthrough_label = ctx->next_label++;
    fprintf(file, "  jnz %%r%u, @l%u, @l%u\n", instr.arg1.id, instr.target.id, fallthrough_label);
    fprintf(file, "@l%u\n", fallthrough_label);
    return;
  }

  case IR_INST_CALL:
    fprintf(file, "  %%r%u =w call $%s(", instr.res.id, instr.var_name);
    // TODO: Handle function arguments
    fprintf(file, ")\n");
    return;
  case IR_INST_RETURN:
    if (instr.arg1.id == NULL_REG.id) {
      fprintf(file, "  ret\n");
    } else {
      fprintf(file, "  ret %%r%u\n", instr.arg1.id);
    }
    return;
  }

  fprintf(file, "  Unknown instruction\n");
}

static void emit_function(FILE *file, IrContext *ctx, IrFunction *function) {
  fprintf(file, "export function w $%s() {\n@start\n", function->name);

  for (size_t i = 0; i < da_len(function->buffer); i++) {
    emit_ir_instr(file, ctx, function->buffer[i]);
  }

  fprintf(file, "}\n");
}

void generate_qbe(const char *filename, IrContext *context) {
  FILE *file = fopen(filename, "w");
  if (!file) {
    fprintf(stderr, "Failed to open file '%s' for writing.\n", filename);
    return;
  }

  fprintf(file, "# Generated QBE IR\n");

  for (size_t i = 0; i < da_len(context->module->functions); i++) {
    emit_function(file, context, &context->module->functions[i]);
  }

  fclose(file);
}