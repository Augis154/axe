#include "backend.h"
#include "lib/array.h"
#include <stdint.h>
#include <stdio.h>

void generate_qbe(const char *filename, IrContext *context) {
  FILE *file = fopen(filename, "w");
  if (!file) {
    fprintf(stderr, "Failed to open file '%s' for writing.\n", filename);
    return;
  }

  fprintf(file, "# QBE IR generated from AST\n");

  fprintf(file, "export function w $main() {\n");
  fprintf(file, "@start\n");

  for (size_t i = 0; i < da_len(context->buffer); i++) {
    IrInstr *inst = &context->buffer[i];

    switch (inst->op) {
    case IR_INST_NONE:
      fprintf(file, "\n");
      break;
    case IR_INST_LOAD_LITERAL:
      fprintf(file, "  %%r%u =w copy %ld\n", inst->res.id, inst->literal);
      break;
    case IR_INST_LOAD_VAR:
      fprintf(file, "  %%r%u =w $%u\n", inst->res.id, inst->arg1.id);
      break;
    case IR_INST_NEGATE:
      fprintf(file, "  %%r%u =w neg %%r%u\n", inst->res.id, inst->arg1.id);
      break;
    case IR_INST_ADD:
      fprintf(file, "  %%r%u =w add %%r%u, %%r%u\n", inst->res.id,
              inst->arg1.id, inst->arg2.id);
      break;
    case IR_INST_SUB:
      fprintf(file, "  %%r%u =w sub %%r%u, %%r%u\n", inst->res.id,
              inst->arg1.id, inst->arg2.id);
      break;
    case IR_INST_MUL:
      fprintf(file, "  %%r%u =w mul %%r%u, %%r%u\n", inst->res.id,
              inst->arg1.id, inst->arg2.id);
      break;
    case IR_INST_DIV:
      fprintf(file, "  %%r%u =w div %%r%u, %%r%u\n", inst->res.id,
              inst->arg1.id, inst->arg2.id);
      break;
    default:
      fprintf(file, "  %zu: Unknown instruction\n", i);
      break;
    }
  }

  uint32_t last_reg_id = context->buffer[da_len(context->buffer) - 1].res.id;
  fprintf(file, "  call $printf(l $fmt, ..., w %%r%u)\n", last_reg_id);

  fprintf(file, "  ret 0\n");
  fprintf(file, "}\n");
  fprintf(file, "data $fmt = {b \"Result: %%d\\n\", b 0}\n");

  fclose(file);
}
