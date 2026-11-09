#include "gen_qbe.h"

#include "common.h"
#include "ir.h"
#include "lib/array.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

// Type and suffix helpers

// The QBE type letter of an IR type; pointers are 64-bit and an aggregate is its address.
static char qbe_type_char(IrType type) {
  switch (type.kind) {
  case IR_TYPE_NONE:
    return 0;

  // Integers narrower than 64 bits live in 32-bit registers
  case IR_TYPE_INT:
    return type.size > 4 ? 'l' : 'w';
  case IR_TYPE_PTR:
    return 'l';

  case IR_TYPE_FLOAT:
    return type.size > 4 ? 'd' : 's';

  // An aggregate is handled by its address, except in signatures and calls where it is named by qbe_write_abi_type
  case IR_TYPE_AGG:
    return 'l';
  }

  return 'w';
}

// The QBE type letter of a register.
static inline char reg_type_char(const IrFunction *func, IrReg reg) {
  assert(reg.id != NULL_REG.id && reg.id < da_len(func->reg_types) && "register without a recorded type");
  return qbe_type_char(func->reg_types[reg.id]);
}

// How an operand is treated by operations whose QBE mnemonic depends on it; the columns of the opcode tables.
typedef enum {
  QBE_KIND_UNSIGNED,
  QBE_KIND_SIGNED,
  QBE_KIND_FLOAT,
} QbeKind;

// The kind of a register's type. Pointers, and aggregates which are handled by their address, are unsigned.
static QbeKind qbe_kind(const IrFunction *func, IrReg reg) {
  IrType type = func->reg_types[reg.id];

  switch (type.kind) {
  case IR_TYPE_INT:
    return type.is_signed ? QBE_KIND_SIGNED : QBE_KIND_UNSIGNED;
  case IR_TYPE_PTR:
  case IR_TYPE_AGG:
    return QBE_KIND_UNSIGNED;
  case IR_TYPE_FLOAT:
    return QBE_KIND_FLOAT;
  case IR_TYPE_NONE:
    break;
  }

  UNREACHABLE("operation on a register without a type");
}

// The suffix of the QBE load that reads a value of `type` from memory; narrow integers are extended to a word.
static const char *qbe_load_suffix(IrType type) {
  switch (type.kind) {
  case IR_TYPE_INT:
    if (type.size == 1) return type.is_signed ? "sb" : "ub";
    if (type.size == 2) return type.is_signed ? "sh" : "uh";
    return type.size == 4 ? "w" : "l";
  case IR_TYPE_FLOAT:
    return type.size == 4 ? "s" : "d";
  case IR_TYPE_PTR:
    return "l";
  case IR_TYPE_NONE:
  case IR_TYPE_AGG:
    break;
  }

  UNREACHABLE("load of a value without a scalar type");
}

// The suffix of the QBE store that writes a value of `type`; only the low bytes of a narrow integer are written.
static const char *qbe_store_suffix(IrType type) {
  if (type.kind == IR_TYPE_INT && type.size == 1) return "b";
  if (type.kind == IR_TYPE_INT && type.size == 2) return "h";

  return qbe_load_suffix(type);
}

// The QBE letter of a field of an aggregate; unlike registers, fields keep their exact width.
static char qbe_field_char(IrType type) {
  switch (type.kind) {
  case IR_TYPE_INT:
    return type.size == 1 ? 'b' : type.size == 2 ? 'h' : type.size == 4 ? 'w' : 'l';
  case IR_TYPE_FLOAT:
    return type.size == 4 ? 's' : 'd';
  case IR_TYPE_PTR:
    return 'l';
  case IR_TYPE_NONE:
  case IR_TYPE_AGG:
    break;
  }

  UNREACHABLE("aggregate field without a scalar type");
}

// Writes the type of a value crossing a call boundary: `:name` for an aggregate
static void qbe_write_abi_type(FILE *file, const IrModule *module, IrType type) {
  if (type.kind == IR_TYPE_AGG) {
    fprintf(file, ":%s", module->aggregates[type.agg_id].name);
  } else if (type.kind == IR_TYPE_INT && type.size < 4) {
    fprintf(file, "%s", qbe_load_suffix(type));
  } else {
    fprintf(file, "%c", qbe_type_char(type));
  }
}

// Instructions

// Writes one IR instruction as QBE IL, without normalizing narrow results.
static void qbe_write_op(FILE *file, const IrModule *module, const IrFunction *func, IrInstr instr) {
  switch (instr.op) {
  case IR_OP_NONE:
    fprintf(file, "\n");
    return;

  // Define a register from a constant or another register
  case IR_OP_LOAD_INT: {
    char type_char = reg_type_char(func, instr.res);
    fprintf(file, "  %%r%u =%c copy %ld\n", instr.res.id, type_char, instr.int_lit);
    return;
  }

  case IR_OP_LOAD_FLOAT: {
    char type_char = reg_type_char(func, instr.res);
    fprintf(file, "  %%r%u =%c copy %c_%.17g\n", instr.res.id, type_char, type_char, instr.float_lit);
    return;
  }

  case IR_OP_GLOBAL_ADDR: {
    char type_char = reg_type_char(func, instr.res);
    const char *name = module->data[instr.int_lit].name;
    fprintf(file, "  %%r%u =%c copy $%s\n", instr.res.id, type_char, name);
    return;
  }

  case IR_OP_COPY: {
    char type_char = reg_type_char(func, instr.res);
    fprintf(file, "  %%r%u =%c copy %%r%u\n", instr.res.id, type_char, instr.arg1.id);
    return;
  }

  // Memory
  case IR_OP_ALLOC: {
    int align = instr.int_lit <= 4 ? 4 : 8;
    fprintf(file, "  %%r%u =l alloc%d %ld\n", instr.res.id, align, instr.int_lit);
    return;
  }
  case IR_OP_LOAD: {
    char type_char = reg_type_char(func, instr.res);
    const char *suffix = qbe_load_suffix(func->reg_types[instr.res.id]);
    fprintf(file, "  %%r%u =%c load%s %%r%u\n", instr.res.id, type_char, suffix, instr.arg1.id);
    return;
  }
  case IR_OP_STORE: {
    const char *suffix = qbe_store_suffix(func->reg_types[instr.arg1.id]);
    fprintf(file, "  store%s %%r%u, %%r%u\n", suffix, instr.arg1.id, instr.arg2.id);
    return;
  }
  case IR_OP_MEMCPY:
  case IR_OP_MEMSET: {
    fprintf(file, "  blit %%r%u, %%r%u, %ld\n", instr.arg1.id, instr.arg2.id, instr.int_lit);
    return;
  }

  // Unary arithmetic and logic
  case IR_OP_NEGATE:
    fprintf(file, "  %%r%u =%c neg %%r%u\n", instr.res.id, reg_type_char(func, instr.res), instr.arg1.id);
    return;

  case IR_OP_NOT:
    fprintf(file, "  %%r%u =%c xor %%r%u, 1\n", instr.res.id, reg_type_char(func, instr.res), instr.arg1.id);
    return;

  case IR_OP_BIT_NOT:
    fprintf(file, "  %%r%u =%c xor %%r%u, -1\n", instr.res.id, reg_type_char(func, instr.res), instr.arg1.id);
    return;

  // Binary arithmetic
  case IR_OP_ADD:
  case IR_OP_SUB:
  case IR_OP_MUL:
  case IR_OP_DIV: {
    static const char *op_names[][3] = {
      [IR_OP_ADD] = {[QBE_KIND_UNSIGNED] = "add", [QBE_KIND_SIGNED] = "add", [QBE_KIND_FLOAT] = "add"},
      [IR_OP_SUB] = {[QBE_KIND_UNSIGNED] = "sub", [QBE_KIND_SIGNED] = "sub", [QBE_KIND_FLOAT] = "sub"},
      [IR_OP_MUL] = {[QBE_KIND_UNSIGNED] = "mul", [QBE_KIND_SIGNED] = "mul", [QBE_KIND_FLOAT] = "mul"},
      [IR_OP_DIV] = {[QBE_KIND_UNSIGNED] = "udiv", [QBE_KIND_SIGNED] = "div", [QBE_KIND_FLOAT] = "div"},
    };

    fprintf(file,
            "  %%r%u =%c %s %%r%u, %%r%u\n",
            instr.res.id,
            reg_type_char(func, instr.res),
            op_names[instr.op][qbe_kind(func, instr.arg1)],
            instr.arg1.id,
            instr.arg2.id);
    return;
  }

  // Comparisons; the result is a bool
  case IR_OP_CMP_EQ:
  case IR_OP_CMP_NE:
  case IR_OP_CMP_LT:
  case IR_OP_CMP_GT:
  case IR_OP_CMP_LE:
  case IR_OP_CMP_GE: {
    static const char *op_names[][3] = {
      [IR_OP_CMP_EQ] = {[QBE_KIND_UNSIGNED] = "ceq", [QBE_KIND_SIGNED] = "ceq", [QBE_KIND_FLOAT] = "ceq"},
      [IR_OP_CMP_NE] = {[QBE_KIND_UNSIGNED] = "cne", [QBE_KIND_SIGNED] = "cne", [QBE_KIND_FLOAT] = "cne"},
      [IR_OP_CMP_LT] = {[QBE_KIND_UNSIGNED] = "cult", [QBE_KIND_SIGNED] = "cslt", [QBE_KIND_FLOAT] = "clt"},
      [IR_OP_CMP_GT] = {[QBE_KIND_UNSIGNED] = "cugt", [QBE_KIND_SIGNED] = "csgt", [QBE_KIND_FLOAT] = "cgt"},
      [IR_OP_CMP_LE] = {[QBE_KIND_UNSIGNED] = "cule", [QBE_KIND_SIGNED] = "csle", [QBE_KIND_FLOAT] = "cle"},
      [IR_OP_CMP_GE] = {[QBE_KIND_UNSIGNED] = "cuge", [QBE_KIND_SIGNED] = "csge", [QBE_KIND_FLOAT] = "cge"},
    };

    char op_char = reg_type_char(func, instr.arg1);
    const char *cmp_name = op_names[instr.op][qbe_kind(func, instr.arg1)];

    fprintf(file, "  %%r%u =w %s%c %%r%u, %%r%u\n", instr.res.id, cmp_name, op_char, instr.arg1.id, instr.arg2.id);
    return;
  }

  // Binary bitwise operations
  case IR_OP_BIT_OR:
  case IR_OP_BIT_XOR:
  case IR_OP_BIT_AND:
  case IR_OP_SHL:
  case IR_OP_SHR: {
    // Only integers (and pointers) have bitwise operations, so there is no float column
    static const char *op_names[][2] = {
      [IR_OP_BIT_OR] = {[QBE_KIND_UNSIGNED] = "or", [QBE_KIND_SIGNED] = "or"},
      [IR_OP_BIT_XOR] = {[QBE_KIND_UNSIGNED] = "xor", [QBE_KIND_SIGNED] = "xor"},
      [IR_OP_BIT_AND] = {[QBE_KIND_UNSIGNED] = "and", [QBE_KIND_SIGNED] = "and"},
      [IR_OP_SHL] = {[QBE_KIND_UNSIGNED] = "shl", [QBE_KIND_SIGNED] = "shl"},
      [IR_OP_SHR] = {[QBE_KIND_UNSIGNED] = "shr", [QBE_KIND_SIGNED] = "sar"},
    };

    QbeKind kind = qbe_kind(func, instr.arg1);
    assert(kind != QBE_KIND_FLOAT && "qbe_write_op: bitwise operation on a float");

    fprintf(file,
            "  %%r%u =%c %s %%r%u, %%r%u\n",
            instr.res.id,
            reg_type_char(func, instr.res),
            op_names[instr.op][kind],
            instr.arg1.id,
            instr.arg2.id);
    return;
  }

  // Conversions between integer and float
  case IR_OP_ITOF: {
    char to_char = reg_type_char(func, instr.res);
    char from_char = reg_type_char(func, instr.arg1);

    const char sign_char = func->reg_types[instr.arg1.id].is_signed ? 's' : 'u';

    fprintf(file, "  %%r%u =%c %c%ctof %%r%u\n", instr.res.id, to_char, sign_char, from_char, instr.arg1.id);
    return;
  }
  case IR_OP_FTOI: {
    char to_char = reg_type_char(func, instr.res);
    char from_char = reg_type_char(func, instr.arg1);

    const char sign_char = func->reg_types[instr.res.id].is_signed ? 's' : 'u';

    fprintf(file, "  %%r%u =%c %cto%ci %%r%u\n", instr.res.id, to_char, from_char, sign_char, instr.arg1.id);
    return;
  }

  // Conversions that change the width of an integer
  case IR_OP_SEXT: {
    char to_char = reg_type_char(func, instr.res);
    char from_char = reg_type_char(func, instr.arg1);

    fprintf(file, "  %%r%u =%c exts%c %%r%u\n", instr.res.id, to_char, from_char, instr.arg1.id);
    return;
  }
  case IR_OP_ZEXT: {
    char to_char = reg_type_char(func, instr.res);
    char from_char = reg_type_char(func, instr.arg1);

    fprintf(file, "  %%r%u =%c extu%c %%r%u\n", instr.res.id, to_char, from_char, instr.arg1.id);
    return;
  }
  case IR_OP_TRUNC:
    fprintf(file, "  %%r%u =%c copy %%r%u\n", instr.res.id, reg_type_char(func, instr.res), instr.arg1.id);
    return;

  // Conversions that change the width of a float
  case IR_OP_FEXT:
    fprintf(file, "  %%r%u =d exts %%r%u\n", instr.res.id, instr.arg1.id);
    return;
  case IR_OP_FTRUNC:
    fprintf(file, "  %%r%u =s truncd %%r%u\n", instr.res.id, instr.arg1.id);
    return;

  // Control flow within a function
  case IR_OP_LABEL:
    fprintf(file, "@l%u\n", instr.target.id);
    return;

  case IR_OP_JUMP:
    fprintf(file, "  jmp @l%u\n", instr.target.id);
    return;

  case IR_OP_JZ:
    fprintf(file, "  jnz %%r%u, @l%u, @l%u\n", instr.arg1.id, instr.target_false.id, instr.target_true.id);
    return;

  case IR_OP_JNZ:
    fprintf(file, "  jnz %%r%u, @l%u, @l%u\n", instr.arg1.id, instr.target_true.id, instr.target_false.id);
    return;

  // Calls and returns
  case IR_OP_CALL: {
    IrType ret_type = func->reg_types[instr.res.id];

    if (ir_type_is_void(ret_type)) {
      fprintf(file, "  call $%s(", instr.call.callee);
    } else {
      fprintf(file, "  %%r%u =", instr.res.id);
      qbe_write_abi_type(file, module, ret_type);
      fprintf(file, " call $%s(", instr.call.callee);
    }

    for (uint32_t i = 0; i < instr.call.arg_count; i++) {
      if (i > 0) fprintf(file, ", ");

      IrReg arg_reg = instr.call.args[i];
      qbe_write_abi_type(file, module, func->reg_types[arg_reg.id]);
      fprintf(file, " %%r%u", arg_reg.id);
    }

    fprintf(file, ")\n");
    return;
  }

  case IR_OP_RETURN:
    if (instr.arg1.id == NULL_REG.id) {
      fprintf(file, "  ret\n");
    } else {
      fprintf(file, "  ret %%r%u\n", instr.arg1.id);
    }
    return;
  }

  UNREACHABLE("unhandled IR opcode");
}

// Writes one IR instruction as QBE IL. QBE has no registers narrower than a word, so after an operation that can leave
// garbage above the value of a u8/i8/u16/i16, the result is extended again from its low bytes.
static void qbe_write_instr(FILE *file, const IrModule *module, const IrFunction *func, IrInstr instr) {
  qbe_write_op(file, module, func, instr);

  switch (instr.op) {
  case IR_OP_NEGATE:
  case IR_OP_BIT_NOT:
  case IR_OP_ADD:
  case IR_OP_SUB:
  case IR_OP_MUL:
  case IR_OP_DIV:
  case IR_OP_SHL:
  case IR_OP_TRUNC:
    break;
  default:
    return;
  }

  IrType res_type = func->reg_types[instr.res.id];
  if (res_type.kind != IR_TYPE_INT || res_type.size >= 4) return;

  fprintf(file,
          "  %%r%u =w ext%c%c %%r%u\n",
          instr.res.id,
          res_type.is_signed ? 's' : 'u',
          res_type.size == 1 ? 'b' : 'h',
          instr.res.id);
}

// Aggregates, data and functions

// Writes `type :name = align N { ... }` for every aggregate.
static void qbe_write_aggregates(FILE *file, const IrModule *module) {
  for (size_t i = 0; i < da_len(module->aggregates); i++) {
    const IrAggregate *agg = &module->aggregates[i];

    fprintf(file, "type :%s = align %u { ", agg->name, agg->alignment);

    for (size_t j = 0; j < da_len(agg->fields); j++) {
      if (j > 0) fprintf(file, ", ");

      IrAggField *field = &agg->fields[j];

      if (field->type.kind == IR_TYPE_AGG) {
        fprintf(file, ":%s ", module->aggregates[field->type.agg_id].name);
      } else {
        fprintf(file, "%c ", qbe_field_char(field->type));
      }

      fprintf(file, "%u", field->count);
    }

    fprintf(file, " }\n");
  }
}

// Writes `data $name = align N { b ... }` for every data item, one byte at a time.
static void qbe_write_data(FILE *file, const IrModule *module) {
  for (size_t i = 0; i < da_len(module->data); i++) {
    const IrData *data = &module->data[i];

    fprintf(file, "data $%s = align %u { b ", data->name, data->alignment);

    for (size_t j = 0; j < data->size; j++) {
      fprintf(file, "%d ", data->bytes[j]);
    }

    fprintf(file, "}\n");
  }
}

// Writes a function: signature, entry label, then its instructions.
static void qbe_write_function(FILE *file, const IrModule *module, const IrFunction *func) {
  fprintf(file, "export function ");

  if (!ir_type_is_void(func->return_type)) {
    qbe_write_abi_type(file, module, func->return_type);
    fprintf(file, " ");
  }

  fprintf(file, "$%s(", func->name);

  for (uint32_t i = 0; i < func->param_count; i++) {
    IrReg param_reg = func->params[i];

    if (i > 0) fprintf(file, ", ");
    qbe_write_abi_type(file, module, func->reg_types[param_reg.id]);
    fprintf(file, " %%r%u", param_reg.id);
  }

  fprintf(file, ") {\n@start\n");

  for (size_t i = 0; i < da_len(func->buffer); i++) {
    qbe_write_instr(file, module, func, func->buffer[i]);
  }

  fprintf(file, "}\n");
}

// QBE generation public functions

bool generate_qbe(const char *filename, const IrModule *module) {
  FILE *file = fopen(filename, "w");
  if (!file) {
    fprintf(stderr, "Failed to open file '%s' for writing.\n", filename);
    return false;
  }

  fprintf(file, "# Generated QBE IR\n");

  qbe_write_aggregates(file, module);
  qbe_write_data(file, module);

  for (size_t i = 0; i < da_len(module->functions); i++) {
    qbe_write_function(file, module, &module->functions[i]);
  }

  return fclose(file) == 0;
}
