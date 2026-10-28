#include "gen_qbe.h"

#include "common.h"
#include "ir.h"
#include "lib/array.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static char qbe_type_char(IrType type) {
  switch (type) {
  case IR_TYPE_NONE:
    return 0;

  case IR_TYPE_I32:
    return 'w';
  case IR_TYPE_I64:
  case IR_TYPE_PTR:
    return 'l';

  case IR_TYPE_F32:
    return 's';
  case IR_TYPE_F64:
    return 'd';
  }

  return 'w';
}

static inline char reg_type_char(const IrFunction *func, IrReg reg) {
  assert(reg.id != NULL_REG.id && reg.id < da_len(func->reg_types) && "register without a recorded type");
  return qbe_type_char(func->reg_types[reg.id]);
}

static void qbe_write_inst(FILE *file, const IrFunction *func, IrInstr inst) {
  switch (inst.op) {
  case IR_OP_NONE:
    fprintf(file, "\n");
    return;

  // Define a register from a constant or another register
  case IR_OP_LOAD_INT: {
    char type_char = reg_type_char(func, inst.res);
    fprintf(file, "  %%r%u =%c copy %ld\n", inst.res.id, type_char, inst.int_lit);
    return;
  }

  case IR_OP_LOAD_FLOAT: {
    char type_char = reg_type_char(func, inst.res);
    fprintf(file, "  %%r%u =%c copy %c_%.17g\n", inst.res.id, type_char, type_char, inst.float_lit);
    return;
  }

  case IR_OP_COPY: {
    char type_char = reg_type_char(func, inst.res);
    fprintf(file, "  %%r%u =%c copy %%r%u\n", inst.res.id, type_char, inst.arg1.id);
    return;
  }

    // Memory
  case IR_OP_ALLOC: {
    int align = inst.int_lit <= 4 ? 4 : 8;
    fprintf(file, "  %%r%u =l alloc%d %ld\n", inst.res.id, align, inst.int_lit);
    return;
  }
  case IR_OP_LOAD: {
    char type_char = reg_type_char(func, inst.res);
    fprintf(file, "  %%r%u =%c load%c %%r%u\n", inst.res.id, type_char, type_char, inst.arg1.id);
    return;
  }
  case IR_OP_STORE: {
    char type_char = reg_type_char(func, inst.arg1);
    fprintf(file, "  store%c %%r%u, %%r%u\n", type_char, inst.arg1.id, inst.arg2.id);
    return;
  }

  // Unary arithmetic and logic
  case IR_OP_NEGATE:
    fprintf(file, "  %%r%u =%c neg %%r%u\n", inst.res.id, reg_type_char(func, inst.res), inst.arg1.id);
    return;

  case IR_OP_NOT:
    fprintf(file, "  %%r%u =%c xor %%r%u, 1\n", inst.res.id, reg_type_char(func, inst.res), inst.arg1.id);
    return;

  // Binary arithmetic
  case IR_OP_ADD:
  case IR_OP_SUB:
  case IR_OP_MUL:
  case IR_OP_DIV: {
    static const char *op_names[] = {
      [IR_OP_ADD] = "add",
      [IR_OP_SUB] = "sub",
      [IR_OP_MUL] = "mul",
      [IR_OP_DIV] = "div",
    };
    fprintf(file,
            "  %%r%u =%c %s %%r%u, %%r%u\n",
            inst.res.id,
            reg_type_char(func, inst.res),
            op_names[inst.op],
            inst.arg1.id,
            inst.arg2.id);
    return;
  }

  // Comparisons; the result is a bool
  case IR_OP_CMP_EQ:
  case IR_OP_CMP_NE:
  case IR_OP_CMP_LT:
  case IR_OP_CMP_GT:
  case IR_OP_CMP_LE:
  case IR_OP_CMP_GE: {
    // Pointers are addresses, so their ordering is unsigned.
    static const char *ptr_cmp_names[] = {
      [IR_OP_CMP_EQ] = "ceq",
      [IR_OP_CMP_NE] = "cne",
      [IR_OP_CMP_LT] = "cult",
      [IR_OP_CMP_GT] = "cugt",
      [IR_OP_CMP_LE] = "cule",
      [IR_OP_CMP_GE] = "cuge",
    };
    static const char *int_cmp_names[] = {
      [IR_OP_CMP_EQ] = "ceq",
      [IR_OP_CMP_NE] = "cne",
      [IR_OP_CMP_LT] = "cslt",
      [IR_OP_CMP_GT] = "csgt",
      [IR_OP_CMP_LE] = "csle",
      [IR_OP_CMP_GE] = "csge",
    };
    static const char *float_cmp_names[] = {
      [IR_OP_CMP_EQ] = "ceq",
      [IR_OP_CMP_NE] = "cne",
      [IR_OP_CMP_LT] = "clt",
      [IR_OP_CMP_GT] = "cgt",
      [IR_OP_CMP_LE] = "cle",
      [IR_OP_CMP_GE] = "cge",
    };

    char op_char = reg_type_char(func, inst.arg1);

    const char *cmp_name = NULL;
    switch (func->reg_types[inst.arg1.id]) {
    case IR_TYPE_I32:
    case IR_TYPE_I64:
      cmp_name = int_cmp_names[inst.op];
      break;
    case IR_TYPE_PTR:
      cmp_name = ptr_cmp_names[inst.op];
      break;
    case IR_TYPE_F32:
    case IR_TYPE_F64:
      cmp_name = float_cmp_names[inst.op];
      break;
    case IR_TYPE_NONE:
      UNREACHABLE("comparison of a register without a type");
    }

    fprintf(file, "  %%r%u =w %s%c %%r%u, %%r%u\n", inst.res.id, cmp_name, op_char, inst.arg1.id, inst.arg2.id);
    return;
  }

  // Conversions between integer and float
  case IR_OP_ITOF: {
    char to_char = reg_type_char(func, inst.res);
    char from_char = reg_type_char(func, inst.arg1);

    fprintf(file, "  %%r%u =%c s%ctof %%r%u\n", inst.res.id, to_char, from_char, inst.arg1.id);
    return;
  }
  case IR_OP_FTOI: {
    char to_char = reg_type_char(func, inst.res);
    char from_char = reg_type_char(func, inst.arg1);

    fprintf(file, "  %%r%u =%c %ctosi %%r%u\n", inst.res.id, to_char, from_char, inst.arg1.id);
    return;
  }

  // Conversions that change the width of an integer
  case IR_OP_SEXT: {
    char to_char = reg_type_char(func, inst.res);
    char from_char = reg_type_char(func, inst.arg1);

    fprintf(file, "  %%r%u =%c exts%c %%r%u\n", inst.res.id, to_char, from_char, inst.arg1.id);
    return;
  }
  case IR_OP_ZEXT: {
    char to_char = reg_type_char(func, inst.res);
    char from_char = reg_type_char(func, inst.arg1);

    fprintf(file, "  %%r%u =%c extu%c %%r%u\n", inst.res.id, to_char, from_char, inst.arg1.id);
    return;
  }
  case IR_OP_TRUNC:
    fprintf(file, "  %%r%u =%c copy %%r%u\n", inst.res.id, reg_type_char(func, inst.res), inst.arg1.id);
    return;

  // Conversions that change the width of a float
  case IR_OP_FEXT:
    fprintf(file, "  %%r%u =d exts %%r%u\n", inst.res.id, inst.arg1.id);
    return;
  case IR_OP_FTRUNC:
    fprintf(file, "  %%r%u =s truncd %%r%u\n", inst.res.id, inst.arg1.id);
    return;

  // Control flow within a function
  case IR_OP_LABEL:
    fprintf(file, "@l%u\n", inst.target.id);
    return;

  case IR_OP_JUMP:
    fprintf(file, "  jmp @l%u\n", inst.target.id);
    return;

  case IR_OP_JZ:
    fprintf(file, "  jnz %%r%u, @l%u, @l%u\n", inst.arg1.id, inst.target_false.id, inst.target_true.id);
    return;

  case IR_OP_JNZ:
    fprintf(file, "  jnz %%r%u, @l%u, @l%u\n", inst.arg1.id, inst.target_true.id, inst.target_false.id);
    return;

  // Calls and returns
  case IR_OP_CALL: {
    IrType ret_type = func->reg_types[inst.res.id];

    if (ret_type == IR_TYPE_NONE) {
      fprintf(file, "  call $%s(", inst.call.callee);
    } else {
      fprintf(file, "  %%r%u =%c call $%s(", inst.res.id, qbe_type_char(ret_type), inst.call.callee);
    }

    for (size_t i = 0; i < inst.call.arg_count; i++) {
      if (i > 0) fprintf(file, ", ");

      IrReg arg_reg = inst.call.args[i];
      fprintf(file, "%c %%r%u", reg_type_char(func, arg_reg), arg_reg.id);
    }

    fprintf(file, ")\n");
    return;
  }

  case IR_OP_RETURN:
    if (inst.arg1.id == NULL_REG.id) {
      fprintf(file, "  ret\n");
    } else {
      fprintf(file, "  ret %%r%u\n", inst.arg1.id);
    }
    return;
  }

  UNREACHABLE("unhandled IR opcode");
}

static void qbe_write_function(FILE *file, const IrFunction *func) {
  fprintf(file, "export function ");

  char return_type_char = qbe_type_char(func->return_type);
  if (return_type_char > 0) {
    fprintf(file, "%c ", return_type_char);
  }

  fprintf(file, "$%s(", func->name);

  for (size_t i = 0; i < func->param_count; i++) {
    IrReg param_reg = func->params[i];

    if (i > 0) fprintf(file, ", ");
    fprintf(file, "%c %%r%u", reg_type_char(func, param_reg), param_reg.id);
  }

  fprintf(file, ") {\n@start\n");

  for (size_t i = 0; i < da_len(func->buffer); i++) {
    qbe_write_inst(file, func, func->buffer[i]);
  }

  fprintf(file, "}\n");
}

bool generate_qbe(const char *filename, const IrModule *module) {
  FILE *file = fopen(filename, "w");
  if (!file) {
    fprintf(stderr, "Failed to open file '%s' for writing.\n", filename);
    return false;
  }

  fprintf(file, "# Generated QBE IR\n");

  for (size_t i = 0; i < da_len(module->functions); i++) {
    qbe_write_function(file, &module->functions[i]);
  }

  return fclose(file) == 0;
}
