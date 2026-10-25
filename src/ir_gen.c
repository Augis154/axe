#include "ast.h"
#include "ir.h"

#include "lib/array.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define next_reg(ctx)       \
  (IrReg) {                 \
    .id = (ctx)->next_reg++ \
  }

#define next_label(ctx)       \
  (IrLabel) {                 \
    .id = (ctx)->next_label++ \
  }

static void emit_ir_instr(IrContext *ctx, IrInstr instr) {
  if (instr.op == IR_INST_LABEL) {
    ctx->block_terminated = false;
  }

  if (ctx->block_terminated) {
    return;
  }

  da_append(ctx->buffer, instr);

  if (instr.op == IR_INST_JUMP) {
    ctx->block_terminated = true;
  }
}

// Expression handling

static IrReg handle_expression(IrContext *ctx, AstExpr *node);

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

static IrReg handle_literal(IrContext *ctx, AstExpr *node) {
  assert(node && "handle_literal: node is NULL");

  AstLiteral *literal = &node->literal;

  switch (literal->type) {
  case LITERAL_INT:
    break;
  case LITERAL_FLOAT:
    fprintf(stderr, "handle_literal: float literals not yet supported in IR generation\n");
    exit(EXIT_FAILURE);
  case LITERAL_CHAR:
    fprintf(stderr, "handle_literal: char literals not yet supported in IR generation\n");
    exit(EXIT_FAILURE);
  case LITERAL_STRING:
    fprintf(stderr, "handle_literal: string literals not yet supported in IR generation\n");
    exit(EXIT_FAILURE);
  }

  IrReg res = next_reg(ctx);

  IrInstr inst = {
    .op = IR_INST_LOAD_LITERAL,
    .res = res,
    .literal = node->literal.int_value,
  };
  emit_ir_instr(ctx, inst);

  return res;
}

static IrReg handle_identifier(IrContext *ctx, AstExpr *node) {
  assert(node && "handle_identifier: node is NULL");

  IrReg res = next_reg(ctx);

  IrInstr inst = {
    .op = IR_INST_LOAD_VAR,
    .res = res,
    .var_name = node->identifier.name,
  };
  emit_ir_instr(ctx, inst);

  return res;
}

static IrReg handle_assignment(IrContext *ctx, AstExpr *node) {
  assert(node && "handle_assignment: node is NULL");

  IrReg value = handle_expression(ctx, node->assignment.value);

  IrInstr inst = {
    .op = IR_INST_STORE_VAR,
    .res = value,
    .var_name = node->assignment.target->identifier.name,
  };
  emit_ir_instr(ctx, inst);

  return value;
}

static IrReg handle_unary_op(IrContext *ctx, AstExpr *node) {
  assert(node && "handle_unary_op: node is NULL");

  IrReg operand = handle_expression(ctx, node->expr_unary.operand);

  IrReg res = next_reg(ctx);

  IrInstr inst = {
    .op = get_unary_ir_op(node->expr_unary.op),
    .res = res,
    .arg1 = operand,
  };
  emit_ir_instr(ctx, inst);

  return res;
}

static IrReg handle_binary_op(IrContext *ctx, AstExpr *node) {
  assert(node && "handle_binary_op: node is NULL");

  IrReg left = handle_expression(ctx, node->expr_binary.left);
  IrReg right = handle_expression(ctx, node->expr_binary.right);

  IrReg res = next_reg(ctx);

  IrInstr inst = {
    .op = get_binary_ir_op(node->expr_binary.op),
    .res = res,
    .arg1 = left,
    .arg2 = right,
  };
  emit_ir_instr(ctx, inst);

  return res;
}

static IrReg handle_expression(IrContext *ctx, AstExpr *node) {
  if (!node) {
    return NULL_REG;
  }

  switch (node->kind) {
  case EXPR_LITERAL:
    return handle_literal(ctx, node);

  case EXPR_IDENTIFIER:
    return handle_identifier(ctx, node);

  case EXPR_ASSIGN:
    return handle_assignment(ctx, node);

  case EXPR_UNARY:
    return handle_unary_op(ctx, node);
  case EXPR_BINARY:
    return handle_binary_op(ctx, node);

  case EXPR_INVALID:
    fprintf(stderr, "ir_handle_expression: encountered EXPR_INVALID node\n");
    exit(EXIT_FAILURE);
  }

  fprintf(stderr, "ir_handle_expression: unhandled AST node kind %d\n", node->kind);
  exit(EXIT_FAILURE);
}

// Statement handling

static void handle_statement(IrContext *ctx, AstStmt *stmt);

static void handle_stmt_block(IrContext *ctx, AstStmt *stmt) {
  assert(stmt && "handle_stmt_block: stmt is NULL");

  for (size_t i = 0; i < stmt->stmt_block.count; i++) {
    AstStmt *inner_stmt = stmt->stmt_block.statements[i];
    handle_statement(ctx, inner_stmt);
  }
}

static void handle_stmt_expression(IrContext *ctx, AstStmt *stmt) {
  assert(stmt && "handle_stmt_expression: stmt is NULL");

  handle_expression(ctx, stmt->stmt_expr.expression);
}

static void handle_stmt_var_decl(IrContext *ctx, AstStmt *stmt) {
  assert(stmt && "handle_stmt_var_decl: stmt is NULL");

  IrReg init_reg = handle_expression(ctx, stmt->stmt_var_decl.init);

  if (init_reg.id == NULL_REG.id) {
    init_reg = next_reg(ctx);
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

static void handle_stmt_break(IrContext *ctx, AstStmt *stmt) {
  assert(ctx->current_loop && "Break statement not within a loop");

  IrInstr inst = {
    .op = IR_INST_JUMP,
    .target = ctx->current_loop->break_label,
  };
  emit_ir_instr(ctx, inst);
}

static void handle_stmt_continue(IrContext *ctx, AstStmt *stmt) {
  assert(ctx->current_loop && "Continue statement not within a loop");

  IrInstr inst = {
    .op = IR_INST_JUMP,
    .target = ctx->current_loop->continue_label,
  };
  emit_ir_instr(ctx, inst);
}

static void handle_stmt_if(IrContext *ctx, AstStmt *stmt) {
  assert(stmt && "handle_stmt_if: stmt is NULL");

  IrLabel else_label = next_label(ctx);
  IrLabel end_label = next_label(ctx);

  IrReg cond_reg = handle_expression(ctx, stmt->stmt_if.condition);

  IrInstr cond_jump_instr = {
    .op = IR_INST_JZ,
    .arg1 = cond_reg,
    .target = else_label,
  };
  emit_ir_instr(ctx, cond_jump_instr);

  handle_statement(ctx, stmt->stmt_if.then_branch);

  IrInstr jump_to_end_instr = {
    .op = IR_INST_JUMP,
    .target = end_label,
  };
  emit_ir_instr(ctx, jump_to_end_instr);

  IrInstr else_label_instr = {
    .op = IR_INST_LABEL,
    .target = else_label,
  };
  emit_ir_instr(ctx, else_label_instr);

  if (stmt->stmt_if.else_branch) {
    handle_statement(ctx, stmt->stmt_if.else_branch);
  }

  IrInstr end_label_instr = {
    .op = IR_INST_LABEL,
    .target = end_label,
  };
  emit_ir_instr(ctx, end_label_instr);
}

static void handle_stmt_while(IrContext *ctx, AstStmt *stmt) {
  assert(stmt && "handle_stmt_while: stmt is NULL");

  IrLabel start_label = next_label(ctx);
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

  IrReg cond_reg = handle_expression(ctx, stmt->stmt_while.condition);

  IrInstr cond_jump_instr = {
    .op = IR_INST_JZ,
    .arg1 = cond_reg,
    .target = end_label,
  };
  emit_ir_instr(ctx, cond_jump_instr);

  handle_statement(ctx, stmt->stmt_while.body);

  IrInstr jump_to_start_instr = {
    .op = IR_INST_JUMP,
    .target = start_label,
  };
  emit_ir_instr(ctx, jump_to_start_instr);

  IrInstr end_label_instr = {
    .op = IR_INST_LABEL,
    .target = end_label,
  };
  emit_ir_instr(ctx, end_label_instr);

  ctx->current_loop = loop_labels.parent;
}

static void handle_statement(IrContext *ctx, AstStmt *stmt) {
  assert(stmt && "handle_statement: stmt is NULL");

  switch (stmt->kind) {
  case STMT_BLOCK:
    return handle_stmt_block(ctx, stmt);
  case STMT_EXPRESSION:
    return handle_stmt_expression(ctx, stmt);
  case STMT_VAR_DECL:
    return handle_stmt_var_decl(ctx, stmt);

  case STMT_BREAK:
    return handle_stmt_break(ctx, stmt);
  case STMT_CONTINUE:
    return handle_stmt_continue(ctx, stmt);

  case STMT_IF:
    return handle_stmt_if(ctx, stmt);
  case STMT_WHILE:
    return handle_stmt_while(ctx, stmt);

  case STMT_INVALID:
    fprintf(stderr, "ir_handle_statement: encountered STMT_INVALID node\n");
    exit(EXIT_FAILURE);
  }

  fprintf(stderr, "ir_handle_statement: unhandled AST statement kind %d\n", stmt->kind);
  exit(EXIT_FAILURE);
}

void ir_buffer_free(IrContext *ctx) {
  da_free(ctx->buffer);
}

void ir_gen_init(IrContext *ctx, CompilationUnit *comp_unit) {
  ctx->buffer = NULL;
  ctx->comp_unit = comp_unit;
  ctx->current_loop = NULL;

  ctx->next_reg = 1;
  ctx->next_label = 1;

  ctx->block_terminated = false;
}

void emit_ir(IrContext *ctx, AstStmt *root) {
  handle_statement(ctx, root);
}
