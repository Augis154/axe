#include "ir.h"
#include "lib/array.h"
#include "sema.h"
#include "types.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static IrType get_ir_type(Type *type) {
  if (!type) return IR_TYPE_WORD;

  switch (type->kind) {
  case TYPE_I32:
    return IR_TYPE_WORD;
  case TYPE_I64:
    return IR_TYPE_LONG;

  case TYPE_F32:
    return IR_TYPE_SPF;
  case TYPE_F64:
    return IR_TYPE_DPF;

  case TYPE_BOOL:
    return IR_TYPE_WORD;

  case TYPE_ERROR:
  case TYPE_VOID:
  case TYPE_FUNCTION:
  case TYPE_UNTYPED_INT:
  case TYPE_UNTYPED_FLOAT:
    return IR_TYPE_NONE;
  }

  return IR_TYPE_WORD;
}

static inline IrFunction *get_current_func(IrContext *ctx) {
  return &ctx->module->functions[ctx->current_func_idx];
}

static IrReg next_reg(IrContext *ctx, Type *type) {
  IrFunction *current_func = get_current_func(ctx);
  IrReg reg = (IrReg){.id = (ctx)->next_reg++};
  da_append(current_func->reg_types, get_ir_type(type));
  return reg;
}

static IrLabel next_label(IrContext *ctx) {
  IrLabel label = (IrLabel){.id = (ctx)->next_label++};
  return label;
}

static void emit_ir_instr(IrContext *ctx, IrInstr instr) {
  if (instr.op == IR_INST_LABEL) {
    ctx->block_terminated = false;
  }

  if (ctx->block_terminated) {
    return;
  }

  IrFunction *current_func = get_current_func(ctx);
  da_append(current_func->buffer, instr);

  if (instr.op == IR_INST_JUMP || instr.op == IR_INST_RETURN) {
    ctx->block_terminated = true;
  }
}

static IrReg emit_cast_if_needed(IrContext *ctx, IrReg src_reg, Type *from_type, Type *to_type) {
  if (type_equals(from_type, to_type)) {
    return src_reg;
  }

  IrType from_ir = get_ir_type(from_type);
  IrType to_ir = get_ir_type(to_type);

  if (from_ir == to_ir) {
    return src_reg;
  }

  // Widening
  if (to_type->size > from_type->size) {
    IrReg dest_reg = next_reg(ctx, to_type);

    IrInstr inst = {
      .op = IR_INST_SEXT,
      .res = dest_reg,
      .arg1 = src_reg,
    };
    emit_ir_instr(ctx, inst);
    return dest_reg;
  }

  // Narrowing
  if (to_type->size < from_type->size) {
    IrReg dest_reg = next_reg(ctx, to_type);

    IrInstr inst = {
      .op = IR_INST_TRUNC,
      .res = dest_reg,
      .arg1 = src_reg,
    };
    emit_ir_instr(ctx, inst);
    return dest_reg;
  }

  return src_reg;
}

// Expression handling

static IrReg emit_expression(IrContext *ctx, AstExpr *node);

static IrOp get_unary_ir_op(UnaryOp op) {
  switch (op) {
  case UNOP_NEGATE:
    return IR_INST_NEGATE;
  case UNOP_NOT:
    return IR_INST_NOT;

  case UNOP_COUNT:
    fprintf(stderr, "get_unary_ir_op: UNOP_COUNT is not a valid operator\n");
    exit(EXIT_FAILURE);
  }

  fprintf(stderr, "get_unary_ir_op: unhandled UNOP %d\n", op);
  exit(EXIT_FAILURE);
}

static IrOp get_binary_ir_op(BinaryOp op) {
  switch (op) {
  case BINOP_ADD:
    return IR_INST_ADD;
  case BINOP_SUB:
    return IR_INST_SUB;
  case BINOP_MUL:
    return IR_INST_MUL;
  case BINOP_DIV:
    return IR_INST_DIV;

  case BINOP_LESS:
    return IR_INST_CMP_LT;
  case BINOP_GREATER:
    return IR_INST_CMP_GT;
  case BINOP_EQEQ:
    return IR_INST_CMP_EQ;
  case BINOP_NOT_EQ:
    return IR_INST_CMP_NE;
  case BINOP_LESS_EQ:
    return IR_INST_CMP_LE;
  case BINOP_GREATER_EQ:
    return IR_INST_CMP_GE;

  case BINOP_COUNT:
    fprintf(stderr, "get_binary_ir_op: BINOP_COUNT is not a valid operator\n");
    exit(EXIT_FAILURE);
  }

  fprintf(stderr, "get_binary_ir_op: unhandled BINOP %d\n", op);
  exit(EXIT_FAILURE);
}

static IrReg emit_literal(IrContext *ctx, AstExpr *node) {
  assert(node && "emit_literal: node is NULL");

  AstLiteral *literal = &node->literal;

  switch (literal->type) {
  case LITERAL_INT:
    break;
  case LITERAL_FLOAT:
    fprintf(stderr, "emit_literal: float literals not yet supported in IR generation\n");
    exit(EXIT_FAILURE);
  case LITERAL_CHAR:
    fprintf(stderr, "emit_literal: char literals not yet supported in IR generation\n");
    exit(EXIT_FAILURE);
  case LITERAL_STRING:
    fprintf(stderr, "emit_literal: string literals not yet supported in IR generation\n");
    exit(EXIT_FAILURE);
  }

  IrReg res = next_reg(ctx, ctx->sema_ctx->expr_types[node->id]);

  IrInstr inst = {
    .op = IR_INST_LOAD_LITERAL,
    .res = res,
    .literal = node->literal.int_value,
  };
  emit_ir_instr(ctx, inst);

  return res;
}

static IrReg emit_identifier(IrContext *ctx, AstExpr *node) {
  assert(node && "emit_identifier: node is NULL");

  IrReg res = next_reg(ctx, ctx->sema_ctx->expr_types[node->id]);

  IrInstr inst = {
    .op = IR_INST_LOAD_VAR,
    .res = res,
    .var_name = node->identifier.name,
  };
  emit_ir_instr(ctx, inst);

  return res;
}

static IrReg emit_assignment(IrContext *ctx, AstExpr *node) {
  assert(node && "emit_assignment: node is NULL");

  IrReg value = emit_expression(ctx, node->assignment.value);

  IrInstr inst = {
    .op = IR_INST_STORE_VAR,
    .res = value,
    .var_name = node->assignment.target->identifier.name,
  };
  emit_ir_instr(ctx, inst);

  return value;
}

static IrReg emit_call(IrContext *ctx, AstExpr *node) {
  assert(node && "emit_call: node is NULL");

  IrFuncArg *arg_params = NULL;
  size_t arg_count = node->expr_call.argument_count;

  if (arg_count > 0) {
    arg_params = arena_alloc(&ctx->comp_unit->arena, sizeof(IrFuncArg) * arg_count);

    for (size_t i = 0; i < arg_count; i++) {
      AstExpr *arg_expr = node->expr_call.arguments[i];

      IrReg arg_reg = emit_expression(ctx, arg_expr);
      Type *arg_type = ctx->sema_ctx->expr_types[arg_expr->id];

      arg_params[i].reg = arg_reg;
      arg_params[i].type = get_ir_type(arg_type);
    }
  }

  IrReg res = next_reg(ctx, ctx->sema_ctx->expr_types[node->id]);

  IrInstr inst = {
    .op = IR_INST_CALL,
    .res = res,
    .call =
      {
        .callee = node->expr_call.callee->identifier.name,
        .args = arg_params,
        .arg_count = arg_count,
      },
  };
  emit_ir_instr(ctx, inst);

  return res;
}

static IrReg emit_unary_op(IrContext *ctx, AstExpr *node) {
  assert(node && "emit_unary_op: node is NULL");

  IrReg operand = emit_expression(ctx, node->expr_unary.operand);

  IrReg res = next_reg(ctx, ctx->sema_ctx->expr_types[node->id]);

  IrInstr inst = {
    .op = get_unary_ir_op(node->expr_unary.op),
    .res = res,
    .arg1 = operand,
  };
  emit_ir_instr(ctx, inst);

  return res;
}

static IrReg emit_binary_op(IrContext *ctx, AstExpr *node) {
  assert(node && "emit_binary_op: node is NULL");

  IrReg left = emit_expression(ctx, node->expr_binary.left);
  IrReg right = emit_expression(ctx, node->expr_binary.right);

  IrReg res = next_reg(ctx, ctx->sema_ctx->expr_types[node->id]);

  IrInstr inst = {
    .op = get_binary_ir_op(node->expr_binary.op),
    .res = res,
    .arg1 = left,
    .arg2 = right,
  };
  emit_ir_instr(ctx, inst);

  return res;
}

static IrReg emit_expression(IrContext *ctx, AstExpr *node) {
  if (!node) {
    return NULL_REG;
  }

  switch (node->kind) {
  case EXPR_LITERAL:
    return emit_literal(ctx, node);

  case EXPR_IDENTIFIER:
    return emit_identifier(ctx, node);

  case EXPR_ASSIGN:
    return emit_assignment(ctx, node);
  case EXPR_CALL:
    return emit_call(ctx, node);

  case EXPR_UNARY:
    return emit_unary_op(ctx, node);
  case EXPR_BINARY:
    return emit_binary_op(ctx, node);

  case EXPR_INVALID:
    fprintf(stderr, "ir_emit_expression: encountered EXPR_INVALID node\n");
    exit(EXIT_FAILURE);
  }

  fprintf(stderr, "ir_emit_expression: unhandled AST node kind %d\n", node->kind);
  exit(EXIT_FAILURE);
}

// Statement handling

static void emit_statement(IrContext *ctx, AstStmt *stmt);

static void emit_stmt_block(IrContext *ctx, AstStmt *stmt) {
  assert(stmt && "emit_stmt_block: stmt is NULL");

  for (size_t i = 0; i < stmt->stmt_block.count; i++) {
    AstStmt *inner_stmt = stmt->stmt_block.statements[i];
    emit_statement(ctx, inner_stmt);
  }
}

static void emit_stmt_expression(IrContext *ctx, AstStmt *stmt) {
  assert(stmt && "emit_stmt_expression: stmt is NULL");

  emit_expression(ctx, stmt->stmt_expr.expression);
}

static void emit_stmt_var_decl(IrContext *ctx, AstStmt *stmt) {
  assert(stmt && "emit_stmt_var_decl: stmt is NULL");

  IrReg init_reg = emit_expression(ctx, stmt->stmt_var_decl.init);

  if (init_reg.id == NULL_REG.id) {
    init_reg = next_reg(ctx, TYPE_i32);
    IrInstr inst = {
      .op = IR_INST_LOAD_LITERAL,
      .res = init_reg,
      .literal = 0,
    };
    emit_ir_instr(ctx, inst);
  }

  IrInstr inst = {
    .op = IR_INST_STORE_VAR,
    .res = init_reg,
    .var_name = stmt->stmt_var_decl.name,
  };
  emit_ir_instr(ctx, inst);
}

static void emit_stmt_return(IrContext *ctx, AstStmt *stmt) {
  assert(stmt && "emit_stmt_return: stmt is NULL");

  IrReg value = emit_expression(ctx, stmt->stmt_return.value);

  Type *actual_type = ctx->sema_ctx->expr_types[stmt->stmt_return.value->id];
  Type *expected_type = ctx->current_func_sym->type->function.return_type;

  value = emit_cast_if_needed(ctx, value, actual_type, expected_type);

  IrInstr inst = {
    .op = IR_INST_RETURN,
    .arg1 = value,
  };
  emit_ir_instr(ctx, inst);
}

static void emit_stmt_break(IrContext *ctx, AstStmt *stmt) {
  assert(ctx->current_loop && "Break statement not within a loop");

  IrInstr inst = {
    .op = IR_INST_JUMP,
    .target = ctx->current_loop->break_label,
  };
  emit_ir_instr(ctx, inst);
}

static void emit_stmt_continue(IrContext *ctx, AstStmt *stmt) {
  assert(ctx->current_loop && "Continue statement not within a loop");

  IrInstr inst = {
    .op = IR_INST_JUMP,
    .target = ctx->current_loop->continue_label,
  };
  emit_ir_instr(ctx, inst);
}

static void emit_stmt_if(IrContext *ctx, AstStmt *stmt) {
  assert(stmt && "emit_stmt_if: stmt is NULL");

  IrLabel then_label = next_label(ctx);
  IrLabel end_label = next_label(ctx);
  IrLabel else_label = stmt->stmt_if.else_branch ? next_label(ctx) : end_label;

  // Conditional jump
  IrReg cond_reg = emit_expression(ctx, stmt->stmt_if.condition);

  IrInstr cond_jump_instr = {
    .op = IR_INST_JNZ,
    .arg1 = cond_reg,
    .target_true = then_label,
    .target_false = else_label,
  };
  emit_ir_instr(ctx, cond_jump_instr);

  // Then branch
  IrInstr then_label_instr = {
    .op = IR_INST_LABEL,
    .target = then_label,
  };
  emit_ir_instr(ctx, then_label_instr);

  emit_statement(ctx, stmt->stmt_if.then_branch);

  // Jump to end if the then branch is not terminated
  if (!ctx->block_terminated) {
    IrInstr jump_to_end_instr = {
      .op = IR_INST_JUMP,
      .target = end_label,
    };
    emit_ir_instr(ctx, jump_to_end_instr);
  }

  // Else branch
  if (stmt->stmt_if.else_branch) {
    IrInstr else_label_instr = {
      .op = IR_INST_LABEL,
      .target = else_label,
    };
    emit_ir_instr(ctx, else_label_instr);

    emit_statement(ctx, stmt->stmt_if.else_branch);

    if (!ctx->block_terminated) {
      IrInstr end_label_instr = {
        .op = IR_INST_LABEL,
        .target = end_label,
      };
      emit_ir_instr(ctx, end_label_instr);
    }
  }

  IrInstr end_label_instr = {
    .op = IR_INST_LABEL,
    .target = end_label,
  };
  emit_ir_instr(ctx, end_label_instr);
}

static void emit_stmt_while(IrContext *ctx, AstStmt *stmt) {
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
    .op = IR_INST_LABEL,
    .target = start_label,
  };
  emit_ir_instr(ctx, start_label_instr);

  IrReg cond_reg = emit_expression(ctx, stmt->stmt_while.condition);

  IrInstr cond_jump_instr = {
    .op = IR_INST_JNZ,
    .arg1 = cond_reg,
    .target_true = fallthrough_label,
    .target_false = end_label,
  };
  emit_ir_instr(ctx, cond_jump_instr);

  IrInstr fallthrough_label_instr = {
    .op = IR_INST_LABEL,
    .target = fallthrough_label,
  };
  emit_ir_instr(ctx, fallthrough_label_instr);

  emit_statement(ctx, stmt->stmt_while.body);

  if (!ctx->block_terminated) {
    IrInstr jump_to_start_instr = {
      .op = IR_INST_JUMP,
      .target = start_label,
    };
    emit_ir_instr(ctx, jump_to_start_instr);
  }

  IrInstr end_label_instr = {
    .op = IR_INST_LABEL,
    .target = end_label,
  };
  emit_ir_instr(ctx, end_label_instr);

  ctx->current_loop = loop_labels.parent;
}

static void emit_statement(IrContext *ctx, AstStmt *stmt) {
  assert(stmt && "emit_statement: stmt is NULL");

  switch (stmt->kind) {
  case STMT_BLOCK:
    return emit_stmt_block(ctx, stmt);
  case STMT_EXPRESSION:
    return emit_stmt_expression(ctx, stmt);
  case STMT_VAR_DECL:
    return emit_stmt_var_decl(ctx, stmt);

  case STMT_RETURN:
    return emit_stmt_return(ctx, stmt);
  case STMT_BREAK:
    return emit_stmt_break(ctx, stmt);
  case STMT_CONTINUE:
    return emit_stmt_continue(ctx, stmt);

  case STMT_IF:
    return emit_stmt_if(ctx, stmt);
  case STMT_WHILE:
    return emit_stmt_while(ctx, stmt);

  case STMT_INVALID:
    fprintf(stderr, "ir_emit_statement: encountered STMT_INVALID node\n");
    exit(EXIT_FAILURE);
  }

  fprintf(stderr, "ir_emit_statement: unhandled AST statement kind %d\n", stmt->kind);
  exit(EXIT_FAILURE);
}

// Declaration handling
static void emit_decl(IrContext *ctx, AstDecl *decl);

static void emit_decl_function(IrContext *ctx, AstDecl *decl) {
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
      AstFuncParam *param = &func_decl->params[i];

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
      .op = IR_INST_RETURN,
      .arg1 = NULL_REG,
    };
    emit_ir_instr(ctx, ret_inst);
  }
}

static void emit_decl(IrContext *ctx, AstDecl *decl) {
  assert(decl && "emit_decl: decl is NULL");

  switch (decl->kind) {
  case DECL_FUNCTION:
    return emit_decl_function(ctx, decl);

  case DECL_INVALID:
    fprintf(stderr, "ir_emit_decl: encountered DECL_INVALID node\n");
    exit(EXIT_FAILURE);
  }

  fprintf(stderr, "ir_emit_decl: unhandled AST declaration kind %d\n", decl->kind);
  exit(EXIT_FAILURE);
}

void ir_init(IrContext *ctx, CompilationUnit *comp_unit, SemaContext *sema_ctx) {
  ctx->comp_unit = comp_unit;
  ctx->sema_ctx = sema_ctx;

  ctx->module = NULL;
  ctx->current_func_idx = 0;

  ctx->next_reg = 1;
  ctx->next_label = 1;

  ctx->current_loop = NULL;
  ctx->block_terminated = false;
}

void ir_free(IrContext *ctx) {
  for (size_t i = 0; i < da_len(ctx->module->functions); i++) {
    IrFunction *func = &ctx->module->functions[i];
    da_free(func->buffer);
    da_free(func->reg_types);
  }
  da_free(ctx->module->functions);
}

void emit_ir(IrContext *ctx, AstProgram *program) {
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
