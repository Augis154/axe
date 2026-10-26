#include "gen_ir.h"

#include "common.h"
#include "lib/array.h"
#include "sema.h"
#include "types.h"

#include <assert.h>

static IrType get_ir_type(Type *type) {
  assert(type && "expression has no type; sema did not analyse it");

  switch (type->kind) {
  case TYPE_KIND_I32:
    return IR_TYPE_I32;
  case TYPE_KIND_I64:
    return IR_TYPE_I64;

  case TYPE_KIND_F32:
    return IR_TYPE_F32;
  case TYPE_KIND_F64:
    return IR_TYPE_F64;

  case TYPE_KIND_BOOL:
    return IR_TYPE_I32;

  case TYPE_KIND_UNTYPED_INT:
    return IR_TYPE_I32;
  case TYPE_KIND_UNTYPED_FLOAT:
    return IR_TYPE_F64;

  case TYPE_KIND_ERROR:
  case TYPE_KIND_VOID:
  case TYPE_KIND_FUNCTION:
    return IR_TYPE_NONE;
  }

  return IR_TYPE_I32;
}

static inline IrFunction *get_current_func(IrGenContext *ctx) {
  return &ctx->module->functions[ctx->current_func_idx];
}

static IrReg next_reg(IrGenContext *ctx, Type *type) {
  IrFunction *current_func = get_current_func(ctx);
  IrReg reg = (IrReg){.id = (ctx)->next_reg++};
  da_append(current_func->reg_types, get_ir_type(type));
  return reg;
}

static IrLabel next_label(IrGenContext *ctx) {
  IrLabel label = (IrLabel){.id = (ctx)->next_label++};
  return label;
}

static void emit_ir_instr(IrGenContext *ctx, IrInstr instr) {
  if (instr.op == IR_OP_LABEL) {
    ctx->block_terminated = false;
  }

  if (ctx->block_terminated) {
    return;
  }

  IrFunction *current_func = get_current_func(ctx);
  da_append(current_func->buffer, instr);

  if (instr.op == IR_OP_JUMP || instr.op == IR_OP_RETURN) {
    ctx->block_terminated = true;
  }
}

static inline bool ir_type_is_float(IrType type) {
  return type == IR_TYPE_F32 || type == IR_TYPE_F64;
}

// IR op that converts a value of IR type `from` to IR type `to`.
static IrOp conversion_op(IrType from, IrType to) {
  bool from_float = ir_type_is_float(from);
  bool to_float = ir_type_is_float(to);

  if (!from_float && !to_float) return from == IR_TYPE_I32 ? IR_OP_SEXT : IR_OP_TRUNC;
  if (!from_float) return IR_OP_ITOF;
  if (to_float) return from == IR_TYPE_F32 ? IR_OP_FEXT : IR_OP_FTRUNC;
  return IR_OP_FTOI;
}

static IrReg emit_cast_if_needed(IrGenContext *ctx, IrReg src_reg, Type *from_type, Type *to_type) {
  IrType from_ir = get_ir_type(from_type);
  IrType to_ir = get_ir_type(to_type);

  if (from_ir == to_ir) {
    return src_reg;
  }

  assert(from_ir != IR_TYPE_NONE && to_ir != IR_TYPE_NONE && "cast between non-value types");

  IrReg dest_reg = next_reg(ctx, to_type);

  IrInstr inst = {
    .op = conversion_op(from_ir, to_ir),
    .res = dest_reg,
    .arg1 = src_reg,
  };
  emit_ir_instr(ctx, inst);

  return dest_reg;
}

// Expression handling

static IrReg emit_expression(IrGenContext *ctx, AstExpr *node);

static inline Type *expr_type(IrGenContext *ctx, AstExpr *node) {
  return sema_expr_type(ctx->sema_ctx, node);
}

static IrOp get_unary_ir_op(UnaryOp op) {
  switch (op) {
  case UNOP_NEGATE:
    return IR_OP_NEGATE;
  case UNOP_NOT:
    return IR_OP_NOT;

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

  case BINOP_COUNT:
    UNREACHABLE("BINOP_COUNT is not a valid operator");
  }

  UNREACHABLE("unhandled binary operator");
}

static IrReg emit_literal(IrGenContext *ctx, AstExpr *node) {
  assert(node && "emit_literal: node is NULL");

  AstExprLiteral *literal = &node->expr_literal;

  switch (literal->kind) {
  case AST_LITERAL_INT:
  case AST_LITERAL_FLOAT:
    break;
  case AST_LITERAL_CHAR:
    UNREACHABLE("sema rejects char literals");
  case AST_LITERAL_STRING:
    UNREACHABLE("sema rejects string literals");
  }

  Type *type = expr_type(ctx, node);
  IrReg res = next_reg(ctx, type);

  IrInstr inst = {.res = res};

  if (ir_type_is_float(get_ir_type(type))) {
    inst.op = IR_OP_LOAD_FLOAT;
    inst.float_literal = literal->kind == AST_LITERAL_FLOAT ? literal->float_value : (double)literal->int_value;
  } else {
    inst.op = IR_OP_LOAD_LITERAL;
    inst.literal = literal->int_value;
  }

  emit_ir_instr(ctx, inst);

  return res;
}

static IrReg emit_identifier(IrGenContext *ctx, AstExpr *node) {
  assert(node && "emit_identifier: node is NULL");

  IrReg res = next_reg(ctx, expr_type(ctx, node));

  IrInstr inst = {
    .op = IR_OP_LOAD_VAR,
    .res = res,
    .var_name = node->expr_identifier.name,
  };
  emit_ir_instr(ctx, inst);

  return res;
}

static IrReg emit_assignment(IrGenContext *ctx, AstExpr *node) {
  assert(node && "emit_assignment: node is NULL");

  IrReg value = emit_expression(ctx, node->expr_assign.value);

  IrInstr inst = {
    .op = IR_OP_STORE_VAR,
    .res = value,
    .var_name = node->expr_assign.target->expr_identifier.name,
  };
  emit_ir_instr(ctx, inst);

  return value;
}

static IrReg emit_call(IrGenContext *ctx, AstExpr *node) {
  assert(node && "emit_call: node is NULL");

  IrFuncArg *arg_params = NULL;
  size_t arg_count = node->expr_call.argument_count;

  if (arg_count > 0) {
    arg_params = arena_alloc(&ctx->comp_unit->arena, sizeof(IrFuncArg) * arg_count);

    for (size_t i = 0; i < arg_count; i++) {
      AstExpr *arg_expr = node->expr_call.arguments[i];

      IrReg arg_reg = emit_expression(ctx, arg_expr);
      Type *arg_type = expr_type(ctx, arg_expr);

      arg_params[i].reg = arg_reg;
      arg_params[i].type = get_ir_type(arg_type);
    }
  }

  IrReg res = next_reg(ctx, expr_type(ctx, node));

  IrInstr inst = {
    .op = IR_OP_CALL,
    .res = res,
    .call =
      {
        .callee = node->expr_call.callee->expr_identifier.name,
        .args = arg_params,
        .arg_count = arg_count,
      },
  };
  emit_ir_instr(ctx, inst);

  return res;
}

static IrReg emit_unary_op(IrGenContext *ctx, AstExpr *node) {
  assert(node && "emit_unary_op: node is NULL");

  IrReg operand = emit_expression(ctx, node->expr_unary.operand);

  IrReg res = next_reg(ctx, expr_type(ctx, node));

  IrInstr inst = {
    .op = get_unary_ir_op(node->expr_unary.op),
    .res = res,
    .arg1 = operand,
  };
  emit_ir_instr(ctx, inst);

  return res;
}

static IrReg emit_binary_op(IrGenContext *ctx, AstExpr *node) {
  assert(node && "emit_binary_op: node is NULL");

  IrReg left = emit_expression(ctx, node->expr_binary.left);
  IrReg right = emit_expression(ctx, node->expr_binary.right);

  IrReg res = next_reg(ctx, expr_type(ctx, node));

  IrInstr inst = {
    .op = get_binary_ir_op(node->expr_binary.op),
    .res = res,
    .arg1 = left,
    .arg2 = right,
  };
  emit_ir_instr(ctx, inst);

  return res;
}

static IrReg emit_cast(IrGenContext *ctx, AstExpr *node) {
  assert(node && "emit_cast: node is NULL");

  AstExpr *operand = node->expr_cast.operand;
  IrReg value = emit_expression(ctx, operand);

  return emit_cast_if_needed(ctx, value, expr_type(ctx, operand), expr_type(ctx, node));
}

static IrReg emit_expression(IrGenContext *ctx, AstExpr *node) {
  if (!node) {
    return NULL_REG;
  }

  switch (node->kind) {
  case AST_EXPR_LITERAL:
    return emit_literal(ctx, node);

  case AST_EXPR_IDENTIFIER:
    return emit_identifier(ctx, node);

  case AST_EXPR_ASSIGN:
    return emit_assignment(ctx, node);
  case AST_EXPR_CALL:
    return emit_call(ctx, node);

  case AST_EXPR_UNARY:
    return emit_unary_op(ctx, node);
  case AST_EXPR_BINARY:
    return emit_binary_op(ctx, node);

  case AST_EXPR_CAST:
    return emit_cast(ctx, node);

  case AST_EXPR_INVALID:
    UNREACHABLE("AST_EXPR_INVALID reached IR generation");
  }

  UNREACHABLE("unhandled expression kind");
}

// Statement handling

static void emit_statement(IrGenContext *ctx, AstStmt *stmt);

static void emit_stmt_block(IrGenContext *ctx, AstStmt *stmt) {
  assert(stmt && "emit_stmt_block: stmt is NULL");

  for (size_t i = 0; i < stmt->stmt_block.statement_count; i++) {
    AstStmt *inner_stmt = stmt->stmt_block.statements[i];
    emit_statement(ctx, inner_stmt);
  }
}

static void emit_stmt_expression(IrGenContext *ctx, AstStmt *stmt) {
  assert(stmt && "emit_stmt_expression: stmt is NULL");

  emit_expression(ctx, stmt->stmt_expression.expression);
}

static void emit_stmt_local_var(IrGenContext *ctx, AstStmt *stmt) {
  assert(stmt && "emit_stmt_local_var: stmt is NULL");

  IrReg init_reg = emit_expression(ctx, stmt->stmt_local_var.init);

  if (init_reg.id == NULL_REG.id) {
    Type *var_type = stmt->stmt_local_var.symbol->type;
    init_reg = next_reg(ctx, var_type);

    IrInstr zero = {.res = init_reg};

    if (ir_type_is_float(get_ir_type(var_type))) {
      zero.op = IR_OP_LOAD_FLOAT;
      zero.float_literal = 0.0;
    } else {
      zero.op = IR_OP_LOAD_LITERAL;
      zero.literal = 0;
    }

    emit_ir_instr(ctx, zero);
  }

  IrInstr inst = {
    .op = IR_OP_STORE_VAR,
    .res = init_reg,
    .var_name = stmt->stmt_local_var.name,
  };
  emit_ir_instr(ctx, inst);
}

static void emit_stmt_return(IrGenContext *ctx, AstStmt *stmt) {
  assert(stmt && "emit_stmt_return: stmt is NULL");

  IrReg value = emit_expression(ctx, stmt->stmt_return.value);

  IrInstr inst = {
    .op = IR_OP_RETURN,
    .arg1 = value,
  };
  emit_ir_instr(ctx, inst);
}

static void emit_stmt_break(IrGenContext *ctx, AstStmt *stmt) {
  UNUSED(stmt);
  assert(stmt && "emit_stmt_break: stmt is NULL");
  assert(ctx->current_loop && "emit_stmt_break: break statement not within a loop");

  IrInstr inst = {
    .op = IR_OP_JUMP,
    .target = ctx->current_loop->break_label,
  };
  emit_ir_instr(ctx, inst);
}

static void emit_stmt_continue(IrGenContext *ctx, AstStmt *stmt) {
  UNUSED(stmt);
  assert(stmt && "emit_stmt_continue: stmt is NULL");
  assert(ctx->current_loop && "emit_stmt_continue: continue statement not within a loop");

  IrInstr inst = {
    .op = IR_OP_JUMP,
    .target = ctx->current_loop->continue_label,
  };
  emit_ir_instr(ctx, inst);
}

static void emit_stmt_if(IrGenContext *ctx, AstStmt *stmt) {
  assert(stmt && "emit_stmt_if: stmt is NULL");

  IrLabel then_label = next_label(ctx);
  IrLabel end_label = next_label(ctx);
  IrLabel else_label = stmt->stmt_if.else_branch ? next_label(ctx) : end_label;

  // Conditional jump
  IrReg cond_reg = emit_expression(ctx, stmt->stmt_if.condition);

  IrInstr cond_jump_instr = {
    .op = IR_OP_JNZ,
    .arg1 = cond_reg,
    .target_true = then_label,
    .target_false = else_label,
  };
  emit_ir_instr(ctx, cond_jump_instr);

  // Then branch
  IrInstr then_label_instr = {
    .op = IR_OP_LABEL,
    .target = then_label,
  };
  emit_ir_instr(ctx, then_label_instr);

  emit_statement(ctx, stmt->stmt_if.then_branch);

  // Jump to end if the then branch is not terminated
  if (!ctx->block_terminated) {
    IrInstr jump_to_end_instr = {
      .op = IR_OP_JUMP,
      .target = end_label,
    };
    emit_ir_instr(ctx, jump_to_end_instr);
  }

  // Else branch
  if (stmt->stmt_if.else_branch) {
    IrInstr else_label_instr = {
      .op = IR_OP_LABEL,
      .target = else_label,
    };
    emit_ir_instr(ctx, else_label_instr);

    emit_statement(ctx, stmt->stmt_if.else_branch);
  }

  IrInstr end_label_instr = {
    .op = IR_OP_LABEL,
    .target = end_label,
  };
  emit_ir_instr(ctx, end_label_instr);
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

  IrInstr start_label_instr = {
    .op = IR_OP_LABEL,
    .target = start_label,
  };
  emit_ir_instr(ctx, start_label_instr);

  IrReg cond_reg = emit_expression(ctx, stmt->stmt_while.condition);

  IrInstr cond_jump_instr = {
    .op = IR_OP_JNZ,
    .arg1 = cond_reg,
    .target_true = fallthrough_label,
    .target_false = end_label,
  };
  emit_ir_instr(ctx, cond_jump_instr);

  IrInstr fallthrough_label_instr = {
    .op = IR_OP_LABEL,
    .target = fallthrough_label,
  };
  emit_ir_instr(ctx, fallthrough_label_instr);

  emit_statement(ctx, stmt->stmt_while.body);

  if (!ctx->block_terminated) {
    IrInstr jump_to_start_instr = {
      .op = IR_OP_JUMP,
      .target = start_label,
    };
    emit_ir_instr(ctx, jump_to_start_instr);
  }

  IrInstr end_label_instr = {
    .op = IR_OP_LABEL,
    .target = end_label,
  };
  emit_ir_instr(ctx, end_label_instr);

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

static void emit_decl_function(IrGenContext *ctx, AstDecl *decl) {
  assert(decl && "emit_decl_function: decl is NULL");

  AstDeclFunction *func_decl = &decl->decl_function;

  ctx->next_reg = 1;
  ctx->next_label = 1;
  ctx->current_loop = NULL;
  ctx->block_terminated = false;

  Type *return_type = func_decl->symbol->type->function.return_type;

  IrFunction ir_func = {
    .name = func_decl->name,
    .return_type = get_ir_type(return_type),
    .params = NULL,
    .param_count = func_decl->param_count,
    .buffer = NULL,
    .reg_types = NULL,
  };

  da_append(ir_func.reg_types, IR_TYPE_NONE);
  da_append(ctx->module->functions, ir_func);

  ctx->current_func_idx = da_len(ctx->module->functions) - 1;
  ctx->current_func_sym = func_decl->symbol;

  uint32_t param_count = func_decl->param_count;
  IrFuncParam *params = NULL;

  if (param_count > 0) {
    params = arena_alloc(&ctx->comp_unit->arena, sizeof(IrFuncParam) * param_count);

    for (size_t i = 0; i < param_count; i++) {
      AstParam *param = &func_decl->params[i];

      IrReg param_reg = next_reg(ctx, param->symbol->type);

      params[i].name = param->symbol->name;
      params[i].type = get_ir_type(param->symbol->type);
      params[i].reg = param_reg;
    }

    get_current_func(ctx)->params = params;
  }

  emit_statement(ctx, func_decl->body);

  if (!ctx->block_terminated) {
    IrInstr ret_inst = {
      .op = IR_OP_RETURN,
      .arg1 = NULL_REG,
    };
    emit_ir_instr(ctx, ret_inst);
  }
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

void gen_ir_init(IrGenContext *ctx, CompilationUnit *comp_unit, SemaContext *sema_ctx) {
  ctx->comp_unit = comp_unit;
  ctx->sema_ctx = sema_ctx;

  ctx->module = NULL;
  ctx->current_func_idx = 0;

  ctx->next_reg = 1;
  ctx->next_label = 1;

  ctx->current_loop = NULL;
  ctx->block_terminated = false;
}

void gen_ir_free(IrGenContext *ctx) {
  for (size_t i = 0; i < da_len(ctx->module->functions); i++) {
    IrFunction *func = &ctx->module->functions[i];
    da_free(func->buffer);
    da_free(func->reg_types);
  }
  da_free(ctx->module->functions);
}

void gen_ir(IrGenContext *ctx, AstProgram *program) {
  if (!program) {
    return;
  }

  IrModule *module = arena_alloc(&ctx->comp_unit->arena, sizeof(IrModule));
  module->functions = NULL;
  ctx->module = module;

  for (size_t i = 0; i < program->decl_count; i++) {
    emit_decl(ctx, program->decls[i]);
  }
}
