#include "gen_ir.h"

#include "ast.h"
#include "common.h"
#include "ir.h"
#include "lib/array.h"
#include "sema.h"
#include "types.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static IrType get_ir_type(IrGenContext *ctx, Type *type);

// Context and type helpers

static inline IrFunction *get_current_func(IrGenContext *ctx) {
  return &ctx->module->functions[ctx->current_func_idx];
}

// The alignment in bytes of a value of IR type `type`: its size for a scalar, the layout's alignment for an aggregate.
static inline uint32_t ir_type_alignment(IrGenContext *ctx, IrType type) {
  return type.kind == IR_TYPE_AGG ? ctx->module->aggregates[type.agg_id].alignment : type.size;
}

// The id of the aggregate with these fields and alignment, added on first use.
static uint32_t get_agg_id(IrGenContext *ctx, uint32_t alignment, const IrAggField *fields, uint32_t field_count) {
  IrModule *module = ctx->module;

  for (size_t i = 0; i < da_len(module->aggregates); i++) {
    IrAggregate *agg = &module->aggregates[i];
    if (da_len(agg->fields) != field_count) {
      continue;
    }

    bool matches = true;
    for (uint32_t j = 0; j < field_count; j++) {
      if (agg->fields[j].count != fields[j].count || !ir_type_equals(agg->fields[j].type, fields[j].type)) {
        matches = false;
        break;
      }
    }

    if (matches) {
      return i;
    }
  }

  char *name = arena_alloc(&ctx->comp_unit->arena, 16);
  snprintf(name, 16, "agg%zu", da_len(module->aggregates));

  IrAggregate agg = {.name = name, .alignment = alignment, .fields = NULL};
  for (uint32_t i = 0; i < field_count; i++) {
    da_append(agg.fields, fields[i]);
  }
  da_append(module->aggregates, agg);

  return da_len(module->aggregates) - 1;
}

// The id of the aggregate with the layout of array `type`; nested arrays are flattened.
static uint32_t get_array_agg_id(IrGenContext *ctx, Type *type) {
  assert(type_is_array(type) && "get_array_agg_id: type is not an array");

  uint32_t count = 1;
  Type *element = type;
  while (type_is_array(element)) {
    count *= element->array.length;
    element = element->array.elem_type;
  }

  // A bool occupies a single byte in memory, although it is an i32 in a register
  IrType element_type = type_is_bool(element) ? IR_INT(1, false) : get_ir_type(ctx, element);

  IrAggField field = {.type = element_type, .count = count};
  return get_agg_id(ctx, ir_type_alignment(ctx, element_type), &field, 1);
}

// The size of the scalar an array is built from; nested arrays are flattened.
static uint32_t flat_elem_size(Type *array_type) {
  Type *elem_type = array_type;
  while (type_is_array(elem_type)) {
    elem_type = elem_type->array.elem_type;
  }
  return elem_type->size;
}

// The id of the aggregate with the layout of a slice, `{ptr, len}`; all slices share it.
static uint32_t get_slice_agg_id(IrGenContext *ctx) {
  IrAggField fields[] = {{.type = IR_PTR, .count = 1}, {.type = IR_I64, .count = 1}};
  return get_agg_id(ctx, 8, fields, 2);
}

static IrType get_ir_type(IrGenContext *ctx, Type *type) {
  assert(type && "expression has no type; sema did not analyse it");

  switch (type->kind) {
  case TYPE_KIND_U8:
    return IR_U8;
  case TYPE_KIND_U16:
    return IR_U16;
  case TYPE_KIND_U32:
    return IR_U32;
  case TYPE_KIND_U64:
    return IR_U64;

  case TYPE_KIND_I8:
    return IR_I8;
  case TYPE_KIND_I16:
    return IR_I16;
  case TYPE_KIND_I32:
    return IR_I32;
  case TYPE_KIND_I64:
    return IR_I64;

  case TYPE_KIND_BOOL:
    return IR_INT(1, false);

  case TYPE_KIND_F32:
    return IR_F32;
  case TYPE_KIND_F64:
    return IR_F64;

  case TYPE_KIND_UNTYPED_INT:
    return IR_I32;
  case TYPE_KIND_UNTYPED_FLOAT:
    return IR_F64;
  case TYPE_KIND_NULL:
    return IR_PTR;

  case TYPE_KIND_POINTER:
    return IR_PTR;

  case TYPE_KIND_ARRAY:
    return IR_AGG(get_array_agg_id(ctx, type));
  case TYPE_KIND_SLICE:
    return IR_AGG(get_slice_agg_id(ctx));

  case TYPE_KIND_ERROR:
  case TYPE_KIND_VOID:
  case TYPE_KIND_UNDEF:
  case TYPE_KIND_FUNCTION:
    return IR_VOID;
  }

  return IR_I32;
}

// Registers, labels and symbols

static IrReg next_ir_reg(IrGenContext *ctx, IrType type) {
  IrFunction *func = get_current_func(ctx);
  IrReg reg = (IrReg){.id = (ctx)->next_reg++};
  da_append(func->reg_types, type);
  return reg;
}

static IrReg next_reg(IrGenContext *ctx, Type *type) {
  return next_ir_reg(ctx, get_ir_type(ctx, type));
}

static IrLabel next_label(IrGenContext *ctx) {
  IrLabel label = (IrLabel){.id = (ctx)->next_label++};
  return label;
}

// Binds `sym` to the register that holds it, or to the address of its slot when it lives in memory.
static void bind_symbol_reg(IrGenContext *ctx, Symbol *sym, IrReg reg) {
  assert(sym != NULL && "cannot map NULL symbol");
  assert(sym->id < da_len(ctx->symbol_to_reg) && "symbol id outside the register table");
  assert(ctx->symbol_to_reg[sym->id].id == NULL_REG.id && "symbol is already mapped");

  ctx->symbol_to_reg[sym->id] = reg;
}

// The register `sym` was bound to.
static IrReg get_symbol_reg(IrGenContext *ctx, Symbol *sym) {
  assert(sym != NULL && "cannot get NULL symbol");
  assert(sym->id < da_len(ctx->symbol_to_reg) && "symbol id outside the register table");

  IrReg reg = ctx->symbol_to_reg[sym->id];
  assert(reg.id != NULL_REG.id && "symbol was never mapped to a register");

  return reg;
}

// Low-level emitters

// Appends an instruction to the current function; code after a jump or return is dropped until the next label.
static void append_instr(IrGenContext *ctx, IrInstr instr) {
  if (instr.op == IR_OP_LABEL) {
    ctx->block_terminated = false;
  }

  if (ctx->block_terminated) {
    return;
  }

  IrFunction *func = get_current_func(ctx);
  da_append(func->buffer, instr);

  if (instr.op == IR_OP_JUMP || instr.op == IR_OP_RETURN) {
    ctx->block_terminated = true;
  }
}

#define emit_ir_instr(ctx, ir_op, ...) append_instr(ctx, (IrInstr){.op = (ir_op), __VA_ARGS__})

static IrReg emit_int_const(IrGenContext *ctx, int64_t value, IrType type) {
  IrReg reg = next_ir_reg(ctx, type);

  emit_ir_instr(ctx, IR_OP_LOAD_INT, .res = reg, .int_lit = value);

  return reg;
}

// Writes `value_reg` of `type` to the address `ptr_reg`: an aggregate is copied, a scalar stored.
static void emit_store(IrGenContext *ctx, IrReg value_reg, IrReg ptr_reg, Type *value_type) {
  if (type_is_aggregate(value_type)) {
    emit_ir_instr(ctx, IR_OP_MEMCPY, .arg1 = value_reg, .arg2 = ptr_reg, .int_lit = value_type->size);
  } else {
    emit_ir_instr(ctx, IR_OP_STORE, .arg1 = value_reg, .arg2 = ptr_reg);
  }
}

// The offset in bytes of `index_reg` elements of `element_size` bytes. The index is an i64.
static IrReg emit_scaled_offset(IrGenContext *ctx, IrReg index_reg, int64_t element_size) {
  if (element_size == 1) {
    return index_reg;
  }

  IrReg size_reg = emit_int_const(ctx, element_size, IR_I64);
  IrReg bytes_reg = next_ir_reg(ctx, IR_I64);

  emit_ir_instr(ctx, IR_OP_MUL, .res = bytes_reg, .arg1 = index_reg, .arg2 = size_reg);

  return bytes_reg;
}

// The address `byte_offset` bytes past `ptr_reg`; an offset of 0 is `ptr_reg` itself.
static IrReg emit_ptr_offset(IrGenContext *ctx, IrReg ptr_reg, int64_t byte_offset) {
  assert(ptr_reg.id != NULL_REG.id && "emit_ptr_offset: ptr_reg is NULL");
  assert(byte_offset >= 0 && "emit_ptr_offset: byte_offset is negative");

  if (byte_offset == 0) {
    return ptr_reg;
  }

  IrReg offset_reg = emit_int_const(ctx, byte_offset, IR_I64);
  IrReg addr_reg = next_ir_reg(ctx, IR_PTR);

  emit_ir_instr(ctx, IR_OP_ADD, .res = addr_reg, .arg1 = ptr_reg, .arg2 = offset_reg);

  return addr_reg;
}

// The address of element `index_reg` of the elements at `base_reg`; the result register has the type `res_type`.
static IrReg emit_element_addr(IrGenContext *ctx, IrReg base_reg, IrReg index_reg, int64_t elem_size, IrType res_type) {
  assert(base_reg.id != NULL_REG.id && "emit_element_addr: base_reg is NULL");
  assert(index_reg.id != NULL_REG.id && "emit_element_addr: index_reg is NULL");
  assert(elem_size > 0 && "emit_element_addr: elements have no size");
  assert((res_type.kind == IR_TYPE_PTR || res_type.kind == IR_TYPE_AGG) && "emit_element_addr: res_type is no address");

  IrReg offset_reg = emit_scaled_offset(ctx, index_reg, elem_size);
  IrReg addr_reg = next_ir_reg(ctx, res_type);

  emit_ir_instr(ctx, IR_OP_ADD, .res = addr_reg, .arg1 = base_reg, .arg2 = offset_reg);

  return addr_reg;
}

// Loads the field of type `field_type` that is `field_offset` bytes into the aggregate at `object_reg`.
static IrReg emit_field_load(IrGenContext *ctx, IrReg object_reg, uint32_t field_offset, Type *field_type) {
  assert(object_reg.id != NULL_REG.id && "emit_field_load: object_reg is NULL");

  IrReg field_addr = emit_ptr_offset(ctx, object_reg, field_offset);

  IrReg result_reg = next_reg(ctx, field_type);
  emit_ir_instr(ctx, IR_OP_LOAD, .res = result_reg, .arg1 = field_addr);

  return result_reg;
}

// Stores `value_reg` into `count` consecutive elements at `base_reg`; `count` must be at least 1.
static void emit_fill(IrGenContext *ctx, IrReg base_reg, IrReg value_reg, Type *elem_type, uint32_t count) {
  uint32_t elem_size = elem_type->size;

  IrReg size_reg = emit_int_const(ctx, elem_size, IR_I64);
  IrReg total_reg = emit_int_const(ctx, (int64_t)count * elem_size, IR_I64);

  // The count is at least 1, so the loop is tested at the bottom
  IrReg end_reg = next_ir_reg(ctx, IR_PTR);
  emit_ir_instr(ctx, IR_OP_ADD, .res = end_reg, .arg1 = base_reg, .arg2 = total_reg);

  IrReg ptr_reg = next_ir_reg(ctx, IR_PTR);
  emit_ir_instr(ctx, IR_OP_COPY, .res = ptr_reg, .arg1 = base_reg);

  IrLabel loop_start = next_label(ctx);
  IrLabel loop_end = next_label(ctx);

  emit_ir_instr(ctx, IR_OP_LABEL, .target = loop_start);
  emit_store(ctx, value_reg, ptr_reg, elem_type);

  // ptr += elem_size, repeating while ptr < end
  IrReg next_ptr_reg = next_ir_reg(ctx, IR_PTR);
  emit_ir_instr(ctx, IR_OP_ADD, .res = next_ptr_reg, .arg1 = ptr_reg, .arg2 = size_reg);
  emit_ir_instr(ctx, IR_OP_COPY, .res = ptr_reg, .arg1 = next_ptr_reg);

  IrReg cond_reg = next_ir_reg(ctx, IR_I32);
  emit_ir_instr(ctx, IR_OP_CMP_LT, .res = cond_reg, .arg1 = ptr_reg, .arg2 = end_reg);
  emit_ir_instr(ctx, IR_OP_JNZ, .arg1 = cond_reg, .target_true = loop_start, .target_false = loop_end);

  emit_ir_instr(ctx, IR_OP_LABEL, .target = loop_end);
}

// Zeroes `size` bytes at `base_reg` with stores of at most `unit` bytes; `size` must be a multiple of `unit`.
static void emit_zero(IrGenContext *ctx, IrReg base_reg, uint32_t size, uint32_t unit) {
  Type *unit_type = unit >= 8 ? type_i64 : unit == 4 ? type_i32 : type_bool;
  IrReg zero_reg = emit_int_const(ctx, 0, IR_INT(unit_type->size, false));
  emit_fill(ctx, base_reg, zero_reg, unit_type, size / unit_type->size);
}

// Reserves an entry-block stack slot for a value of `type`; the address is typed as the aggregate if it is one.
static IrReg queue_entry_alloc(IrGenContext *ctx, Type *type) {
  IrReg slot_reg = next_ir_reg(ctx, type_is_aggregate(type) ? get_ir_type(ctx, type) : IR_PTR);

  IrInstr alloc_instr = {
    .op = IR_OP_ALLOC,
    .res = slot_reg,
    .int_lit = type->size,
  };
  da_append(ctx->entry_allocs, alloc_instr);

  return slot_reg;
}

// Builds a slice of type `slice_type` in a new stack slot from its data pointer and length; yields the slot's address.
static IrReg emit_slice_value(IrGenContext *ctx, Type *slice_type, IrReg ptr_reg, IrReg len_reg) {
  assert(type_is_slice(slice_type) && "emit_slice_value: type is not a slice");
  assert(ptr_reg.id != NULL_REG.id && "emit_slice_value: ptr_reg is NULL");
  assert(len_reg.id != NULL_REG.id && "emit_slice_value: len_reg is NULL");

  IrReg slice_ptr = queue_entry_alloc(ctx, slice_type);

  emit_ir_instr(ctx, IR_OP_STORE, .arg1 = ptr_reg, .arg2 = slice_ptr);

  IrReg len_ptr = emit_ptr_offset(ctx, slice_ptr, SLICE_LEN_OFFSET);
  emit_ir_instr(ctx, IR_OP_STORE, .arg1 = len_reg, .arg2 = len_ptr);

  return slice_ptr;
}

// Conversions

// IR op that converts a value of IR type `from` to IR type `to`.
static IrOp conversion_op(IrType from, IrType to) {
  // Pointers, aggregates (their address) and i64 have the same representation; only the register's type changes.
  bool from_wide = from.kind == IR_TYPE_PTR || from.kind == IR_TYPE_AGG || (from.kind == IR_TYPE_INT && from.size == 8);
  bool to_wide = to.kind == IR_TYPE_PTR || to.kind == IR_TYPE_AGG || (to.kind == IR_TYPE_INT && to.size == 8);
  if (from_wide && to_wide) return IR_OP_COPY;

  bool from_float = ir_type_is_float(from);
  bool to_float = ir_type_is_float(to);

  if (!from_float && !to_float) {
    if (from.size <= 4 && to.size <= 4) {
      bool needs_extend = to.size < 4 && (from.size > to.size || from.is_signed != to.is_signed);
      return needs_extend ? IR_OP_TRUNC : IR_OP_COPY;
    }

    if (from.size >= to.size) return IR_OP_TRUNC;
    return from.is_signed ? IR_OP_SEXT : IR_OP_ZEXT;
  }

  if (!from_float) return IR_OP_ITOF;
  if (to_float) return from.size < to.size ? IR_OP_FEXT : IR_OP_FTRUNC;
  return IR_OP_FTOI;
}

// Converts the array at `array_ptr` to a slice that points at it and has the array's length.
static IrReg emit_array_to_slice(IrGenContext *ctx, IrReg array_ptr, Type *array_type, Type *slice_type) {
  assert(type_is_array(array_type) && "emit_array_to_slice: type is not an array");
  assert(type_is_slice(slice_type) && "emit_array_to_slice: target type is not a slice");
  assert(type_equals(type_elem(array_type), type_elem(slice_type)) && "emit_array_to_slice: element types differ");

  IrReg ptr_reg = next_ir_reg(ctx, IR_PTR);
  emit_ir_instr(ctx, IR_OP_COPY, .res = ptr_reg, .arg1 = array_ptr);

  IrReg slice_len = emit_int_const(ctx, array_type->array.length, IR_I64);
  return emit_slice_value(ctx, slice_type, ptr_reg, slice_len);
}

// Converts `src_reg` from `from_type` to `to_type`; the same IR type needs no instruction.
static IrReg emit_cast_if_needed(IrGenContext *ctx, IrReg src_reg, Type *from_type, Type *to_type) {
  IrType from_ir = get_ir_type(ctx, from_type);
  IrType to_ir = get_ir_type(ctx, to_type);

  if (ir_type_equals(from_ir, to_ir)) {
    return src_reg;
  }

  if (type_is_array(from_type) && type_is_slice(to_type)) {
    return emit_array_to_slice(ctx, src_reg, from_type, to_type);
  }

  assert(!ir_type_is_void(from_ir) && !ir_type_is_void(to_ir) && "cast between non-value types");

  IrReg res = next_reg(ctx, to_type);

  emit_ir_instr(ctx, conversion_op(from_ir, to_ir), .res = res, .arg1 = src_reg);

  return res;
}

// Returns `reg` with the IR type `type`, copying it to a new register only when the type differs.
static IrReg retype_reg(IrGenContext *ctx, IrReg reg, IrType type) {
  if (ir_type_equals(get_current_func(ctx)->reg_types[reg.id], type)) {
    return reg;
  }

  IrReg res = next_ir_reg(ctx, type);

  emit_ir_instr(ctx, IR_OP_COPY, .res = res, .arg1 = reg);

  return res;
}

// Expression handling

static IrReg emit_expression(IrGenContext *ctx, AstExpr *expr);

static inline Type *expr_type(IrGenContext *ctx, AstExpr *expr) {
  return sema_expr_type(ctx->sema_ctx, expr);
}

static IrOp get_unary_ir_op(UnaryOp op) {
  switch (op) {
  case UNOP_NEGATE:
    return IR_OP_NEGATE;
  case UNOP_NOT:
    return IR_OP_NOT;
  case UNOP_BIT_NOT:
    return IR_OP_BIT_NOT;

  case UNOP_COUNT:
    UNREACHABLE("UNOP_COUNT is not a valid operator");
  }

  UNREACHABLE("unhandled unary operator");
}

static IrOp get_binary_ir_op(BinaryOp op) {
  switch (op) {
  case BINOP_ADD:
    return IR_OP_ADD;
  case BINOP_SUB:
    return IR_OP_SUB;
  case BINOP_MUL:
    return IR_OP_MUL;
  case BINOP_DIV:
    return IR_OP_DIV;

  case BINOP_LESS:
    return IR_OP_CMP_LT;
  case BINOP_GREATER:
    return IR_OP_CMP_GT;
  case BINOP_EQ:
    return IR_OP_CMP_EQ;
  case BINOP_NOT_EQ:
    return IR_OP_CMP_NE;
  case BINOP_LESS_EQ:
    return IR_OP_CMP_LE;
  case BINOP_GREATER_EQ:
    return IR_OP_CMP_GE;

  case BINOP_BIT_OR:
    return IR_OP_BIT_OR;
  case BINOP_BIT_XOR:
    return IR_OP_BIT_XOR;
  case BINOP_BIT_AND:
    return IR_OP_BIT_AND;
  case BINOP_SHL:
    return IR_OP_SHL;
  case BINOP_SHR:
    return IR_OP_SHR;

  case BINOP_COUNT:
    UNREACHABLE("BINOP_COUNT is not a valid operator");
  }

  UNREACHABLE("unhandled binary operator");
}

static IrReg emit_literal(IrGenContext *ctx, AstExpr *expr) {
  assert(expr && "emit_literal: expr is NULL");

  AstExprLiteral *literal = &expr->expr_literal;

  switch (literal->kind) {
  case AST_LITERAL_INT:
  case AST_LITERAL_FLOAT:
  case AST_LITERAL_TRUE:
  case AST_LITERAL_FALSE:
  case AST_LITERAL_NULL:
    break;
  case AST_LITERAL_UNDEF:
    UNREACHABLE("a declaration skips its undef initializer");
  case AST_LITERAL_CHAR:
    UNREACHABLE("sema rejects char literals");
  case AST_LITERAL_STRING:
    UNREACHABLE("sema rejects string literals");
  }

  Type *type = expr_type(ctx, expr);
  IrReg res = next_reg(ctx, type);

  if (ir_type_is_float(get_ir_type(ctx, type))) {
    double float_value = literal->kind == AST_LITERAL_FLOAT ? literal->float_value : (double)literal->int_value;
    emit_ir_instr(ctx, IR_OP_LOAD_FLOAT, .res = res, .float_lit = float_value);
  } else {
    int64_t int_value;

    switch (literal->kind) {
    case AST_LITERAL_INT:
      int_value = literal->int_value;
      break;
    case AST_LITERAL_TRUE:
    case AST_LITERAL_FALSE:
      int_value = literal->kind == AST_LITERAL_TRUE ? 1 : 0;
      break;
    case AST_LITERAL_NULL:
      int_value = 0;
      break;
    default:
      UNREACHABLE("sema rejects non-numeric literals");
    }

    emit_ir_instr(ctx, IR_OP_LOAD_INT, .res = res, .int_lit = int_value);
  }

  return res;
}

// A register variable is its register; a scalar in memory is loaded; an aggregate is the address of its slot.
static IrReg emit_identifier(IrGenContext *ctx, AstExpr *expr) {
  assert(expr && "emit_identifier: expr is NULL");

  Symbol *sym = expr->expr_identifier.symbol;
  IrReg reg = get_symbol_reg(ctx, sym);

  // A scalar in memory is loaded; an aggregate evaluates to the address of its slot
  if (sym->local.in_memory && !type_is_aggregate(sym->type)) {
    IrReg value_reg = next_reg(ctx, sym->type);

    emit_ir_instr(ctx, IR_OP_LOAD, .res = value_reg, .arg1 = reg);

    return value_reg;
  }

  return reg;
}

// Builds `[a, b, ...]` in a new stack slot, storing each element at its offset, and yields the slot's address.
static IrReg emit_array_literal(IrGenContext *ctx, AstExpr *expr) {
  Type *arr_type = expr_type(ctx, expr);
  uint32_t length = arr_type->array.length;

  Type *elem_type = arr_type->array.elem_type;
  uint32_t elem_size = elem_type->size;

  IrReg arr_ptr_reg = queue_entry_alloc(ctx, arr_type);

  if (expr->expr_array_literal.is_repeat) {

    IrReg val_reg = emit_expression(ctx, expr->expr_array_literal.repeat.value);
    emit_fill(ctx, arr_ptr_reg, val_reg, elem_type, length);

    return arr_ptr_reg;
  }

  uint32_t element_count = expr->expr_array_literal.list.element_count;

  if (element_count < arr_type->array.length) {
    emit_zero(ctx, arr_ptr_reg, arr_type->size, flat_elem_size(arr_type));
  }

  for (uint32_t i = 0; i < element_count; i++) {
    AstArrayElement *element = &expr->expr_array_literal.list.elements[i];
    IrReg val_reg = emit_expression(ctx, element->value);

    int64_t offset = (int64_t)element->index * elem_size;
    IrReg elem_addr_reg = emit_ptr_offset(ctx, arr_ptr_reg, offset);

    emit_store(ctx, val_reg, elem_addr_reg, elem_type);
  }

  return arr_ptr_reg;
}

// The address of `target[index]`, whose register has the type `res_type`: the aggregate's type when the element is one.
static IrReg emit_array_access(IrGenContext *ctx, AstExpr *target, AstExpr *index, IrType res_type) {
  IrReg target_reg = emit_expression(ctx, target);
  IrReg index_reg = emit_expression(ctx, index);

  Type *target_type = expr_type(ctx, target);

  if (type_is_slice(target_type)) {
    target_reg = emit_field_load(ctx, target_reg, SLICE_PTR_OFFSET, type_void_ptr);
  }

  return emit_element_addr(ctx, target_reg, index_reg, type_elem(target_type)->size, res_type);
}

// `&x` is the address of the slot of `x`, the pointer of `p.*`, or the element address of `xs[i]`.
static IrReg emit_addr_of(IrGenContext *ctx, AstExpr *expr) {
  assert(expr && "emit_addr_of: expr is NULL");

  AstExpr *operand = expr->expr_addr_of.operand;

  if (operand->kind == AST_EXPR_DEREF) {
    return emit_expression(ctx, operand->expr_deref.operand);
  }

  if (operand->kind == AST_EXPR_SUBSCRIPT) {
    return emit_array_access(ctx, operand->expr_subscript.target, operand->expr_subscript.index, IR_PTR);
  }

  Symbol *sym = operand->expr_identifier.symbol;
  assert(sym->local.in_memory && "sema puts every variable whose address is taken in memory");

  return retype_reg(ctx, get_symbol_reg(ctx, sym), IR_PTR);
}

// Loads the value `p.*` points to.
static IrReg emit_deref(IrGenContext *ctx, AstExpr *expr) {
  assert(expr && "emit_deref: expr is NULL");

  IrReg ptr_reg = emit_expression(ctx, expr->expr_deref.operand);
  Type *type = expr_type(ctx, expr);

  if (type_is_aggregate(type)) {
    return retype_reg(ctx, ptr_reg, get_ir_type(ctx, type));
  }

  IrReg value_reg = next_reg(ctx, type);

  emit_ir_instr(ctx, IR_OP_LOAD, .res = value_reg, .arg1 = ptr_reg);

  return value_reg;
}

// Loads `xs[i]`; an element that is itself an aggregate is yielded as its address.
static IrReg emit_subscript(IrGenContext *ctx, AstExpr *expr) {
  assert(expr && "emit_subscript: expr is NULL");

  Type *type = expr_type(ctx, expr);

  IrType address_type = type_is_aggregate(type) ? get_ir_type(ctx, type) : IR_PTR;
  IrReg ptr_reg = emit_array_access(ctx, expr->expr_subscript.target, expr->expr_subscript.index, address_type);

  if (type_is_aggregate(type)) {
    return ptr_reg;
  }

  IrReg res = next_reg(ctx, type);

  emit_ir_instr(ctx, IR_OP_LOAD, .res = res, .arg1 = ptr_reg);

  return res;
}

// Emits `xs[start:end]` for an array or a slice; a missing start is 0 and a missing end is the length of `xs`.
static IrReg emit_slice(IrGenContext *ctx, AstExpr *expr) {
  assert(expr && "emit_slice: expr is NULL");

  Type *slice_type = expr_type(ctx, expr);
  Type *target_type = expr_type(ctx, expr->expr_slice.target);
  assert(type_is_array_like(target_type) && "sema allows only arrays and slices to be sliced");

  bool is_array = type_is_array(target_type);

  IrReg target_reg = emit_expression(ctx, expr->expr_slice.target);
  IrReg data_reg = is_array ? target_reg : emit_field_load(ctx, target_reg, SLICE_PTR_OFFSET, type_void_ptr);

  IrReg start_reg =
    expr->expr_slice.start ? emit_expression(ctx, expr->expr_slice.start) : emit_int_const(ctx, 0, IR_I64);

  IrReg end_reg;
  if (expr->expr_slice.end) {
    end_reg = emit_expression(ctx, expr->expr_slice.end);
  } else if (is_array) {
    end_reg = emit_int_const(ctx, target_type->array.length, IR_I64);
  } else {
    end_reg = emit_field_load(ctx, target_reg, SLICE_LEN_OFFSET, type_i64);
  }

  IrReg base_reg = emit_element_addr(ctx, data_reg, start_reg, type_elem(slice_type)->size, IR_PTR);

  IrReg len_reg = next_ir_reg(ctx, IR_I64);
  emit_ir_instr(ctx, IR_OP_SUB, .res = len_reg, .arg1 = end_reg, .arg2 = start_reg);

  return emit_slice_value(ctx, slice_type, base_reg, len_reg);
}

// Emits `object.member` by loading the field that sema found; the length of an array is folded into a literal by sema.
static IrReg emit_member(IrGenContext *ctx, AstExpr *expr) {
  assert(expr && "emit_member: expr is NULL");
  assert(!type_is_array(expr_type(ctx, expr->expr_member.object)) && "sema folds the members of an array");

  IrReg object_reg = emit_expression(ctx, expr->expr_member.object);

  return emit_field_load(ctx, object_reg, expr->expr_member.offset, expr_type(ctx, expr));
}

// Emits a call with its argument registers; the result register has the callee's return type.
static IrReg emit_call(IrGenContext *ctx, AstExpr *expr) {
  assert(expr && "emit_call: expr is NULL");

  IrReg *arg_regs = NULL;
  uint32_t arg_count = expr->expr_call.argument_count;

  if (arg_count > 0) {
    arg_regs = arena_alloc(&ctx->comp_unit->arena, sizeof(IrReg) * arg_count);

    for (uint32_t i = 0; i < arg_count; i++) {
      AstExpr *arg_expr = expr->expr_call.arguments[i];

      // The ABI type of an argument comes from its register, so an aggregate address has to be typed as the aggregate
      IrReg arg_reg = emit_expression(ctx, arg_expr);
      arg_regs[i] = retype_reg(ctx, arg_reg, get_ir_type(ctx, expr_type(ctx, arg_expr)));
    }
  }

  IrReg res = next_reg(ctx, expr_type(ctx, expr));

  emit_ir_instr(ctx,
                IR_OP_CALL,
                .res = res,
                .call = {
                  .callee = expr->expr_call.callee->expr_identifier.name,
                  .args = arg_regs,
                  .arg_count = arg_count,
                });

  return res;
}

static IrReg emit_unary_op(IrGenContext *ctx, AstExpr *expr) {
  assert(expr && "emit_unary_op: expr is NULL");

  IrReg operand_reg = emit_expression(ctx, expr->expr_unary.operand);
  IrReg res = next_reg(ctx, expr_type(ctx, expr));

  emit_ir_instr(ctx, get_unary_ir_op(expr->expr_unary.op), .res = res, .arg1 = operand_reg);

  return res;
}

// `ptr +- int` moves by whole elements and `ptr - ptr` counts them, so offsets are scaled by the pointee size.
static IrReg emit_pointer_arithmetic(IrGenContext *ctx, AstExpr *expr) {
  AstExpr *left_expr = expr->expr_binary.left;
  AstExpr *right_expr = expr->expr_binary.right;
  Type *left_type = expr_type(ctx, left_expr);
  Type *right_type = expr_type(ctx, right_expr);

  IrReg left_reg = emit_expression(ctx, left_expr);
  IrReg right_reg = emit_expression(ctx, right_expr);

  bool left_is_ptr = type_is_pointer(left_type);
  bool right_is_ptr = type_is_pointer(right_type);

  Type *ptr_type = left_is_ptr ? left_type : right_type;
  int64_t element_size = ptr_type->pointer.base_type->size;

  if (left_is_ptr && right_is_ptr) {
    IrReg bytes_reg = next_ir_reg(ctx, IR_I64);

    emit_ir_instr(ctx, IR_OP_SUB, .res = bytes_reg, .arg1 = left_reg, .arg2 = right_reg);

    if (element_size == 1) {
      return bytes_reg;
    }

    IrReg size_reg = emit_int_const(ctx, element_size, IR_I64);
    IrReg count_reg = next_ir_reg(ctx, IR_I64);

    emit_ir_instr(ctx, IR_OP_DIV, .res = count_reg, .arg1 = bytes_reg, .arg2 = size_reg);

    return count_reg;
  }

  IrReg ptr_reg = left_is_ptr ? left_reg : right_reg;
  IrReg offset_reg = left_is_ptr ? right_reg : left_reg;

  offset_reg = emit_scaled_offset(ctx, offset_reg, element_size);

  IrReg res = next_ir_reg(ctx, IR_PTR);

  emit_ir_instr(ctx, get_binary_ir_op(expr->expr_binary.op), .res = res, .arg1 = ptr_reg, .arg2 = offset_reg);

  return res;
}

static IrReg emit_binary_op(IrGenContext *ctx, AstExpr *expr) {
  assert(expr && "emit_binary_op: expr is NULL");

  BinaryOp op = expr->expr_binary.op;
  bool is_arithmetic = op == BINOP_ADD || op == BINOP_SUB;
  if (is_arithmetic && (type_is_pointer(expr_type(ctx, expr->expr_binary.left)) ||
                        type_is_pointer(expr_type(ctx, expr->expr_binary.right)))) {
    return emit_pointer_arithmetic(ctx, expr);
  }

  IrReg left_reg = emit_expression(ctx, expr->expr_binary.left);
  IrReg right_reg = emit_expression(ctx, expr->expr_binary.right);
  IrReg res = next_reg(ctx, expr_type(ctx, expr));

  emit_ir_instr(ctx, get_binary_ir_op(expr->expr_binary.op), .res = res, .arg1 = left_reg, .arg2 = right_reg);

  return res;
}

// `&&` and `||` evaluate the right operand only when the left one does not decide the result.
static IrReg emit_logical_op(IrGenContext *ctx, AstExpr *expr) {
  assert(expr && "emit_logical_op: expr is NULL");

  LogicalOp op = expr->expr_logical.op;

  IrLabel right_label = next_label(ctx);
  IrLabel end_label = next_label(ctx);
  IrReg res = next_reg(ctx, expr_type(ctx, expr));

  IrReg left_reg = emit_expression(ctx, expr->expr_logical.left);

  emit_ir_instr(ctx, IR_OP_COPY, .res = res, .arg1 = left_reg);

  switch (op) {
  case LOGIC_OR: {
    emit_ir_instr(ctx, IR_OP_JZ, .arg1 = res, .target_true = right_label, .target_false = end_label);
    break;
  }
  case LOGIC_AND: {
    emit_ir_instr(ctx, IR_OP_JNZ, .arg1 = res, .target_true = right_label, .target_false = end_label);
    break;
  }

  default:
    UNREACHABLE("unhandled logical operator");
  }

  emit_ir_instr(ctx, IR_OP_LABEL, .target = right_label);

  IrReg right_reg = emit_expression(ctx, expr->expr_logical.right);

  emit_ir_instr(ctx, IR_OP_COPY, .res = res, .arg1 = right_reg);

  emit_ir_instr(ctx, IR_OP_LABEL, .target = end_label);

  return res;
}

static IrReg emit_cast(IrGenContext *ctx, AstExpr *expr) {
  assert(expr && "emit_cast: expr is NULL");

  AstExpr *operand = expr->expr_cast.operand;
  IrReg value_reg = emit_expression(ctx, operand);

  return emit_cast_if_needed(ctx, value_reg, expr_type(ctx, operand), expr_type(ctx, expr));
}

// Stores or copies the value into the target (a register, a slot, a dereference or an element) and yields the value.
static IrReg emit_assignment(IrGenContext *ctx, AstExpr *expr) {
  assert(expr && "emit_assignment: expr is NULL");

  AstExpr *target = expr->expr_assign.target;
  Symbol *target_sym = NULL;
  IrReg target_reg;

  if (target->kind == AST_EXPR_DEREF) {
    target_reg = emit_expression(ctx, target->expr_deref.operand);
  } else if (target->kind == AST_EXPR_SUBSCRIPT) {
    target_reg = emit_array_access(ctx, target->expr_subscript.target, target->expr_subscript.index, IR_PTR);
  } else {
    target_sym = target->expr_identifier.symbol;
    target_reg = get_symbol_reg(ctx, target_sym);
  }

  IrReg value_reg = emit_expression(ctx, expr->expr_assign.value);

  bool in_register = target_sym && !target_sym->local.in_memory && !type_is_aggregate(target_sym->type);

  if (in_register) {
    emit_ir_instr(ctx, IR_OP_COPY, .res = target_reg, .arg1 = value_reg);
  } else {
    emit_store(ctx, value_reg, target_reg, expr_type(ctx, target));
  }

  return value_reg;
}

static IrReg emit_expression(IrGenContext *ctx, AstExpr *expr) {
  if (!expr) {
    return NULL_REG;
  }

  switch (expr->kind) {
  case AST_EXPR_LITERAL:
    return emit_literal(ctx, expr);

  case AST_EXPR_IDENTIFIER:
    return emit_identifier(ctx, expr);

  case AST_EXPR_ARRAY_LITERAL:
    return emit_array_literal(ctx, expr);

  case AST_EXPR_ADDR_OF:
    return emit_addr_of(ctx, expr);

  case AST_EXPR_DEREF:
    return emit_deref(ctx, expr);

  case AST_EXPR_SUBSCRIPT:
    return emit_subscript(ctx, expr);

  case AST_EXPR_SLICE:
    return emit_slice(ctx, expr);

  case AST_EXPR_MEMBER:
    return emit_member(ctx, expr);

  case AST_EXPR_CALL:
    return emit_call(ctx, expr);

  case AST_EXPR_UNARY:
    return emit_unary_op(ctx, expr);

  case AST_EXPR_BINARY:
    return emit_binary_op(ctx, expr);

  case AST_EXPR_LOGICAL:
    return emit_logical_op(ctx, expr);

  case AST_EXPR_CAST:
    return emit_cast(ctx, expr);

  case AST_EXPR_ASSIGN:
    return emit_assignment(ctx, expr);

  case AST_EXPR_INVALID:
    UNREACHABLE("AST_EXPR_INVALID reached IR generation");
  }

  UNREACHABLE("unhandled expression kind");
}

// Statement handling

static void emit_statement(IrGenContext *ctx, AstStmt *stmt);

static void emit_stmt_block(IrGenContext *ctx, AstStmt *stmt) {
  assert(stmt && "emit_stmt_block: stmt is NULL");

  for (uint32_t i = 0; i < stmt->stmt_block.statement_count; i++) {
    AstStmt *inner_stmt = stmt->stmt_block.statements[i];
    emit_statement(ctx, inner_stmt);
  }
}

static void emit_stmt_expression(IrGenContext *ctx, AstStmt *stmt) {
  assert(stmt && "emit_stmt_expression: stmt is NULL");

  emit_expression(ctx, stmt->stmt_expression.expression);
}

// Declares a variable in a register or a memory slot; it is zeroed unless the initializer is `undef`.
static void emit_stmt_local_var(IrGenContext *ctx, AstStmt *stmt) {
  assert(stmt && "emit_stmt_local_var: stmt is NULL");

  Symbol *sym = stmt->stmt_local_var.symbol;
  IrReg reg = sym->local.in_memory ? queue_entry_alloc(ctx, sym->type) : next_reg(ctx, sym->type);

  bind_symbol_reg(ctx, sym, reg);

  if (stmt->stmt_local_var.is_undef) {
    return;
  }

  IrReg init_reg = emit_expression(ctx, stmt->stmt_local_var.init);

  if (init_reg.id == NULL_REG.id) {
    if (type_is_aggregate(sym->type)) {
      emit_zero(ctx, reg, sym->type->size, flat_elem_size(sym->type));
      return;
    }

    init_reg = next_reg(ctx, sym->type);

    if (ir_type_is_float(get_ir_type(ctx, sym->type))) {
      emit_ir_instr(ctx, IR_OP_LOAD_FLOAT, .res = init_reg, .float_lit = 0.0);
    } else {
      emit_ir_instr(ctx, IR_OP_LOAD_INT, .res = init_reg, .int_lit = 0);
    }
  }

  if (sym->local.in_memory) {
    emit_store(ctx, init_reg, reg, sym->type);

    return;
  }

  emit_ir_instr(ctx, IR_OP_COPY, .res = reg, .arg1 = init_reg);
}

static void emit_stmt_return(IrGenContext *ctx, AstStmt *stmt) {
  assert(stmt && "emit_stmt_return: stmt is NULL");

  IrReg value_reg = emit_expression(ctx, stmt->stmt_return.value);

  emit_ir_instr(ctx, IR_OP_RETURN, .arg1 = value_reg);
}

static void emit_stmt_break(IrGenContext *ctx, AstStmt *stmt) {
  UNUSED(stmt);
  assert(stmt && "emit_stmt_break: stmt is NULL");
  assert(ctx->current_loop && "emit_stmt_break: break statement not within a loop");

  emit_ir_instr(ctx, IR_OP_JUMP, .target = ctx->current_loop->break_label);
}

static void emit_stmt_continue(IrGenContext *ctx, AstStmt *stmt) {
  UNUSED(stmt);
  assert(stmt && "emit_stmt_continue: stmt is NULL");
  assert(ctx->current_loop && "emit_stmt_continue: continue statement not within a loop");

  emit_ir_instr(ctx, IR_OP_JUMP, .target = ctx->current_loop->continue_label);
}

static void emit_stmt_if(IrGenContext *ctx, AstStmt *stmt) {
  assert(stmt && "emit_stmt_if: stmt is NULL");

  IrLabel then_label = next_label(ctx);
  IrLabel end_label = next_label(ctx);
  IrLabel else_label = stmt->stmt_if.else_branch ? next_label(ctx) : end_label;

  // Conditional jump
  IrReg cond_reg = emit_expression(ctx, stmt->stmt_if.condition);

  emit_ir_instr(ctx, IR_OP_JNZ, .arg1 = cond_reg, .target_true = then_label, .target_false = else_label);

  // Then branch
  emit_ir_instr(ctx, IR_OP_LABEL, .target = then_label);

  emit_statement(ctx, stmt->stmt_if.then_branch);

  // Jump to end if the then branch is not terminated
  if (!ctx->block_terminated) {
    emit_ir_instr(ctx, IR_OP_JUMP, .target = end_label);
  }

  // Else branch
  if (stmt->stmt_if.else_branch) {
    emit_ir_instr(ctx, IR_OP_LABEL, .target = else_label);

    emit_statement(ctx, stmt->stmt_if.else_branch);
  }

  emit_ir_instr(ctx, IR_OP_LABEL, .target = end_label);
}

static void emit_stmt_while(IrGenContext *ctx, AstStmt *stmt) {
  assert(stmt && "emit_stmt_while: stmt is NULL");

  IrLabel start_label = next_label(ctx);
  IrLabel fallthrough_label = next_label(ctx);
  IrLabel end_label = next_label(ctx);

  LoopLabels loop_labels = {
    .continue_label = start_label,
    .break_label = end_label,
    .parent = ctx->current_loop,
  };
  ctx->current_loop = &loop_labels;

  emit_ir_instr(ctx, IR_OP_LABEL, .target = start_label);

  IrReg cond_reg = emit_expression(ctx, stmt->stmt_while.condition);

  emit_ir_instr(ctx, IR_OP_JNZ, .arg1 = cond_reg, .target_true = fallthrough_label, .target_false = end_label);

  emit_ir_instr(ctx, IR_OP_LABEL, .target = fallthrough_label);

  emit_statement(ctx, stmt->stmt_while.body);

  if (!ctx->block_terminated) {
    emit_ir_instr(ctx, IR_OP_JUMP, .target = start_label);
  }

  emit_ir_instr(ctx, IR_OP_LABEL, .target = end_label);

  ctx->current_loop = loop_labels.parent;
}

static void emit_statement(IrGenContext *ctx, AstStmt *stmt) {
  assert(stmt && "emit_statement: stmt is NULL");

  switch (stmt->kind) {
  case AST_STMT_BLOCK:
    return emit_stmt_block(ctx, stmt);
  case AST_STMT_EXPRESSION:
    return emit_stmt_expression(ctx, stmt);
  case AST_STMT_LOCAL_VAR:
    return emit_stmt_local_var(ctx, stmt);

  case AST_STMT_RETURN:
    return emit_stmt_return(ctx, stmt);
  case AST_STMT_BREAK:
    return emit_stmt_break(ctx, stmt);
  case AST_STMT_CONTINUE:
    return emit_stmt_continue(ctx, stmt);

  case AST_STMT_IF:
    return emit_stmt_if(ctx, stmt);
  case AST_STMT_WHILE:
    return emit_stmt_while(ctx, stmt);

  case AST_STMT_INVALID:
    UNREACHABLE("AST_STMT_INVALID reached IR generation");
  }

  UNREACHABLE("unhandled statement kind");
}

// Declaration handling
static void emit_decl(IrGenContext *ctx, AstDecl *decl);

// Emits a function: parameters become registers (or slots), then the body, then the entry-block allocations.
static void emit_decl_function(IrGenContext *ctx, AstDecl *decl) {
  assert(decl && "emit_decl_function: decl is NULL");

  AstDeclFunction *func_decl = &decl->decl_function;

  ctx->next_reg = 1;
  ctx->next_label = 1;
  ctx->current_loop = NULL;
  ctx->block_terminated = false;

  Type *return_type = func_decl->symbol->type->function.return_type;

  IrFunction new_func = {
    .name = func_decl->name,
    .return_type = get_ir_type(ctx, return_type),
    .params = NULL,
    .param_count = func_decl->param_count,
    .buffer = NULL,
    .reg_types = NULL,
  };

  da_append(new_func.reg_types, IR_VOID);
  da_append(ctx->module->functions, new_func);

  ctx->current_func_idx = da_len(ctx->module->functions) - 1;
  ctx->current_func_sym = func_decl->symbol;

  uint32_t param_count = func_decl->param_count;

  if (param_count > 0) {
    IrFunction *func = get_current_func(ctx);
    func->params = arena_alloc(&ctx->comp_unit->arena, sizeof(IrReg) * param_count);

    for (uint32_t i = 0; i < param_count; i++) {
      Symbol *sym = func_decl->params[i].symbol;
      IrReg param_reg = next_reg(ctx, sym->type);
      IrReg slot_reg = param_reg;

      if (sym->local.in_memory) {
        slot_reg = queue_entry_alloc(ctx, sym->type);
        emit_store(ctx, param_reg, slot_reg, sym->type);
      }

      func->params[i] = param_reg;
      bind_symbol_reg(ctx, sym, slot_reg);
    }
  }

  emit_statement(ctx, func_decl->body);

  if (!ctx->block_terminated) {
    emit_ir_instr(ctx, IR_OP_RETURN, .arg1 = NULL_REG);
  }

  IrFunction *func = get_current_func(ctx);
  for (size_t i = da_len(ctx->entry_allocs); i > 0; i--) {
    da_insert(func->buffer, 0, ctx->entry_allocs[i - 1]);
  }
  da_set_len(ctx->entry_allocs, 0);
}

static void emit_decl(IrGenContext *ctx, AstDecl *decl) {
  assert(decl && "emit_decl: decl is NULL");

  switch (decl->kind) {
  case AST_DECL_FUNCTION:
    return emit_decl_function(ctx, decl);

  case AST_DECL_INVALID:
    UNREACHABLE("AST_DECL_INVALID reached IR generation");
  }

  UNREACHABLE("unhandled declaration kind");
}

// IR generation public functions

void gen_ir_init(IrGenContext *ctx, CompilationUnit *comp_unit, SemaContext *sema_ctx) {
  ctx->comp_unit = comp_unit;
  ctx->sema_ctx = sema_ctx;

  ctx->module = NULL;
  ctx->current_func_idx = 0;

  ctx->next_reg = 1;
  ctx->next_label = 1;

  ctx->current_loop = NULL;
  ctx->block_terminated = false;

  ctx->entry_allocs = NULL;
  ctx->symbol_to_reg = NULL;
  da_set_len(ctx->symbol_to_reg, ctx->sema_ctx->next_symbol_id);
  for (size_t i = 0; i < da_len(ctx->symbol_to_reg); i++) {
    ctx->symbol_to_reg[i] = NULL_REG;
  }
}

void gen_ir_free(IrGenContext *ctx) {
  for (size_t i = 0; i < da_len(ctx->module->functions); i++) {
    IrFunction *func = &ctx->module->functions[i];
    da_free(func->buffer);
    da_free(func->reg_types);
  }

  for (size_t i = 0; i < da_len(ctx->module->aggregates); i++) {
    da_free(ctx->module->aggregates[i].fields);
  }

  da_free(ctx->module->aggregates);
  da_free(ctx->module->functions);
  da_free(ctx->symbol_to_reg);
  da_free(ctx->entry_allocs);
}

void gen_ir(IrGenContext *ctx, AstProgram *program) {
  if (!program) {
    return;
  }

  IrModule *module = arena_alloc(&ctx->comp_unit->arena, sizeof(IrModule));
  module->functions = NULL;
  module->aggregates = NULL;
  ctx->module = module;

  for (uint32_t i = 0; i < program->decl_count; i++) {
    emit_decl(ctx, program->decls[i]);
  }
}
