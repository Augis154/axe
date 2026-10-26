#include "backend.h"

#include "ir.h"
#include "lib/array.h"

#include <stdint.h>
#include <stdio.h>

static char qbe_type_char(IrType type) {
  switch (type) {
  case IR_TYPE_NONE:
    return 0;

  case IR_TYPE_WORD:
    return 'w';
  case IR_TYPE_LONG:
    return 'l';

  case IR_TYPE_SPF:
    return 'f';
  case IR_TYPE_DPF:
    return 'd';
  }

  return 'w';
}

static inline char reg_type_char(IrFunction *fn, IrReg reg) {
  if (reg.id == NULL_REG.id || reg.id >= da_len(fn->reg_types)) {
    return 'w';
  }
  return qbe_type_char(fn->reg_types[reg.id]);
}

static void emit_ir_instr(FILE *file, IrContext *ctx, IrInstr instr) {
  IrFunction *current_func = &ctx->module->functions[ctx->current_func_idx];

  switch (instr.op) {
  case IR_INST_NONE:
    fprintf(file, "\n");
    return;

  // Memory & Variables
  case IR_INST_LOAD_LITERAL:
    fprintf(file, "  %%r%u =%c copy %ld\n", instr.res.id, reg_type_char(current_func, instr.res), instr.literal);
    return;

  case IR_INST_LOAD_VAR:
    fprintf(file, "  %%r%u =%c copy %%%s\n", instr.res.id, reg_type_char(current_func, instr.res), instr.var_name);
    return;

  case IR_INST_STORE_VAR:
    fprintf(file, "  %%%s =%c copy %%r%u\n", instr.var_name, reg_type_char(current_func, instr.res), instr.res.id);
    return;

  // Unary Operations
  case IR_INST_NEGATE:
    fprintf(file, "  %%r%u =%c neg %%r%u\n", instr.res.id, reg_type_char(current_func, instr.res), instr.arg1.id);
    return;

  case IR_INST_NOT:
    fprintf(file, "  %%r%u =%c xor %%r%u, 1\n", instr.res.id, reg_type_char(current_func, instr.res), instr.arg1.id);
    return;

  // Binary Arithmetic
  case IR_INST_ADD:
  case IR_INST_SUB:
  case IR_INST_MUL:
  case IR_INST_DIV: {
    static const char *op_names[] = {
      [IR_INST_ADD] = "add",
      [IR_INST_SUB] = "sub",
      [IR_INST_MUL] = "mul",
      [IR_INST_DIV] = "div",
    };
    fprintf(file,
            "  %%r%u =%c %s %%r%u, %%r%u\n",
            instr.res.id,
            reg_type_char(current_func, instr.res),
            op_names[instr.op],
            instr.arg1.id,
            instr.arg2.id);
    return;
  }

  // Comparisons
  case IR_INST_CMP_EQ:
  case IR_INST_CMP_NE:
  case IR_INST_CMP_LT:
  case IR_INST_CMP_GT:
  case IR_INST_CMP_LE:
  case IR_INST_CMP_GE: {
    static const char *cmp_names[] = {
      [IR_INST_CMP_EQ] = "ceq",
      [IR_INST_CMP_NE] = "cne",
      [IR_INST_CMP_LT] = "cslt",
      [IR_INST_CMP_GT] = "csgt",
      [IR_INST_CMP_LE] = "csle",
      [IR_INST_CMP_GE] = "csge",
    };
    char op_char = reg_type_char(current_func, instr.arg1);
    fprintf(
      file, "  %%r%u =w %s%c %%r%u, %%r%u\n", instr.res.id, cmp_names[instr.op], op_char, instr.arg1.id, instr.arg2.id);
    return;
  }

  // Labels & Control Flow
  case IR_INST_LABEL:
    fprintf(file, "@l%u\n", instr.target.id);
    return;

  case IR_INST_JUMP:
    fprintf(file, "  jmp @l%u\n", instr.target.id);
    return;

  case IR_INST_JNZ:
    fprintf(file, "  jnz %%r%u, @l%u, @l%u\n", instr.arg1.id, instr.target_true.id, instr.target_false.id);
    return;

  case IR_INST_JZ:
    fprintf(file, "  jnz %%r%u, @l%u, @l%u\n", instr.arg1.id, instr.target_false.id, instr.target_true.id);
    return;

  case IR_INST_RETURN:
    if (instr.arg1.id == NULL_REG.id) {
      fprintf(file, "  ret\n");
    } else {
      fprintf(file, "  ret %%r%u\n", instr.arg1.id);
    }
    return;

  // Function Calls
  case IR_INST_CALL: {
    IrType ret_type = current_func->reg_types[instr.res.id];

    if (ret_type == IR_TYPE_NONE) {
      fprintf(file, "  call $%s(", instr.call.callee);
    } else {
      fprintf(file, "  %%r%u =%c call $%s(", instr.res.id, qbe_type_char(ret_type), instr.call.callee);
    }

    for (size_t i = 0; i < instr.call.arg_count; i++) {
      if (i > 0) fprintf(file, ", ");

      IrFuncArg *arg = &instr.call.args[i];
      fprintf(file, "%c %%r%u", qbe_type_char(arg->type), arg->reg.id);
    }

    fprintf(file, ")\n");
    return;
  }

  // Type Casting
  case IR_INST_SEXT: {
    char to_char = reg_type_char(current_func, instr.res);
    char from_char = reg_type_char(current_func, instr.arg1);

    fprintf(file, "  %%r%u =%c exts%c %%r%u\n", instr.res.id, to_char, from_char, instr.arg1.id);
    return;
  }
  case IR_INST_ZEXT: {
    char to_char = reg_type_char(current_func, instr.res);
    char from_char = reg_type_char(current_func, instr.arg1);

    fprintf(file, "  %%r%u =%c extu%c %%r%u\n", instr.res.id, to_char, from_char, instr.arg1.id);
    return;
  }
  case IR_INST_TRUNC:
    // QBE auto truncates when assigning to a smaller type
    return;
  }

  fprintf(stderr, "emit_ir_instr: unhandled IR opcode %d\n", instr.op);
  assert(false);
}

static void emit_function(FILE *file, IrContext *ctx) {
  IrFunction *function = &ctx->module->functions[ctx->current_func_idx];

  fprintf(file, "export function ");

  char return_type_char = qbe_type_char(function->return_type);
  if (return_type_char > 0) {
    fprintf(file, "%c ", return_type_char);
  }

  fprintf(file, "$%s(", function->name);

  for (size_t i = 0; i < function->param_count; i++) {
    IrFuncParam *param = &function->params[i];
    char param_type_char = qbe_type_char(param->type);

    if (i > 0) fprintf(file, ", ");
    fprintf(file, "%c %%%s", param_type_char, param->name);
  }

  fprintf(file, ") {\n@start\n");

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
    context->current_func_idx = i;
    emit_function(file, context);
  }

  fclose(file);
}