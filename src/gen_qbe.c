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

  fprintf(file, "# Generated QBE IR\n");

  fprintf(file, "export function w $main() {\n");
  fprintf(file, "@start\n");

  for (size_t i = 0; i < da_len(context->buffer); i++) {
    IrInstr *inst = &context->buffer[i];

    switch (inst->op) {
    case IR_INST_NONE:
      fprintf(file, "\n");
      continue;

    case IR_INST_LOAD_LITERAL:
      fprintf(file, "  %%r%u =w copy %ld\n", inst->res.id, inst->literal);
      continue;

    case IR_INST_LOAD_VAR:
      fprintf(file, "  %%r%u =w copy %%%s\n", inst->res.id, inst->var_name);
      continue;
    case IR_INST_STORE_VAR:
      fprintf(file, "  %%%s =w copy %%r%u\n", inst->var_name, inst->res.id);
      continue;

    case IR_INST_NEGATE:
      fprintf(file, "  %%r%u =w neg %%r%u\n", inst->res.id, inst->arg1.id);
      continue;
    case IR_INST_NOT:
      fprintf(file, "  %%r%u =w ceqw %%r%u, 0\n", inst->res.id, inst->arg1.id);
      continue;

    case IR_INST_ADD:
      fprintf(file, "  %%r%u =w add %%r%u, %%r%u\n", inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;
    case IR_INST_SUB:
      fprintf(file, "  %%r%u =w sub %%r%u, %%r%u\n", inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;
    case IR_INST_MUL:
      fprintf(file, "  %%r%u =w mul %%r%u, %%r%u\n", inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;
    case IR_INST_DIV:
      fprintf(file, "  %%r%u =w div %%r%u, %%r%u\n", inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;

    case IR_INST_CMP_EQ:
      fprintf(file, "  %%r%u =w ceqw %%r%u, %%r%u\n", inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;
    case IR_INST_CMP_NE:
      fprintf(file, "  %%r%u =w cnew %%r%u, %%r%u\n", inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;
    case IR_INST_CMP_LT:
      fprintf(file, "  %%r%u =w csltw %%r%u, %%r%u\n", inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;
    case IR_INST_CMP_GT:
      fprintf(file, "  %%r%u =w csgtw %%r%u, %%r%u\n", inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;
    case IR_INST_CMP_LE:
      fprintf(file, "  %%r%u =w cslew %%r%u, %%r%u\n", inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;
    case IR_INST_CMP_GE:
      fprintf(file, "  %%r%u =w csgew %%r%u, %%r%u\n", inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;

    case IR_INST_LABEL:
      fprintf(file, "@l%u\n", inst->target.id);
      continue;
    case IR_INST_JUMP:
      fprintf(file, "  jmp @l%u\n", inst->target.id);
      continue;
    case IR_INST_JZ: {
      uint32_t fallthrough_label = context->next_label++;
      fprintf(file, "  jnz %%r%u, @l%u, @l%u\n", inst->arg1.id, fallthrough_label, inst->target.id);
      fprintf(file, "@l%u\n", fallthrough_label);
      continue;
    }
    case IR_INST_JNZ: {
      uint32_t fallthrough_label = context->next_label++;
      fprintf(file, "  jnz %%r%u, @l%u, @l%u\n", inst->arg1.id, inst->target.id, fallthrough_label);
      fprintf(file, "@l%u\n", fallthrough_label);
      continue;
    }
    }
    fprintf(file, "  %zu: Unknown instruction\n", i);
  }

  uint32_t last_reg_id = context->buffer[da_len(context->buffer) - 1].res.id;
  fprintf(file, "  call $printf(l $fmt, ..., w %%r%u)\n", last_reg_id);

  fprintf(file, "  ret 0\n");
  fprintf(file, "}\n");
  fprintf(file, "data $fmt = {b \"Result: %%d\\n\", b 0}\n");

  fclose(file);
}
