#include "ast.h"
#include "ir.h"

#include "lib/array.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define next_reg(context)       \
  (IrReg) {                     \
    .id = (context)->next_reg++ \
  }

#define next_label(context)       \
  (IrLabel) {                     \
    .id = (context)->next_label++ \
  }

// Expression handling

static IrReg handle_expression(IrContext *context, AstExpr *node);

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

static IrReg handle_literal(IrContext *context, AstExpr *node) {
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

  IrReg res = next_reg(context);

  IrInstr inst = {
    .op = IR_INST_LOAD_LITERAL,
    .res = res,
    .literal = node->literal.int_value,
  };
  da_append(context->buffer, inst);

  return res;
}

static IrReg handle_identifier(IrContext *context, AstExpr *node) {
  IrReg res = next_reg(context);

  IrInstr inst = {
    .op = IR_INST_LOAD_VAR,
    .res = res,
    .var_name = node->identifier.name,
  };
  da_append(context->buffer, inst);

  return res;
}

static IrReg handle_assignment(IrContext *context, AstExpr *node) {
  IrReg value = handle_expression(context, node->assignment.value);

  IrInstr inst = {
    .op = IR_INST_STORE_VAR,
    .res = value,
    .var_name = node->assignment.target->identifier.name,
  };
  da_append(context->buffer, inst);

  return value;
}

static IrReg handle_unary_op(IrContext *context, AstExpr *node) {
  IrReg operand = handle_expression(context, node->expr_unary.operand);

  IrReg res = next_reg(context);

  IrInstr inst = {
    .op = get_unary_ir_op(node->expr_unary.op),
    .res = res,
    .arg1 = operand,
  };
  da_append(context->buffer, inst);

  return res;
}

static IrReg handle_binary_op(IrContext *context, AstExpr *node) {
  IrReg left = handle_expression(context, node->expr_binary.left);
  IrReg right = handle_expression(context, node->expr_binary.right);

  IrReg res = next_reg(context);

  IrInstr inst = {
    .op = get_binary_ir_op(node->expr_binary.op),
    .res = res,
    .arg1 = left,
    .arg2 = right,
  };
  da_append(context->buffer, inst);

  return res;
}

static IrReg handle_expression(IrContext *context, AstExpr *node) {
  if (!node) {
    return NULL_REG;
  }

  switch (node->kind) {
  case EXPR_LITERAL:
    return handle_literal(context, node);

  case EXPR_IDENTIFIER:
    return handle_identifier(context, node);

  case EXPR_ASSIGN:
    return handle_assignment(context, node);

  case EXPR_UNARY:
    return handle_unary_op(context, node);
  case EXPR_BINARY:
    return handle_binary_op(context, node);

  case EXPR_INVALID:
    fprintf(stderr, "ir_handle_expression: encountered EXPR_INVALID node\n");
    exit(EXIT_FAILURE);
  }

  fprintf(stderr, "ir_handle_expression: unhandled AST node kind %d\n", node->kind);
  exit(EXIT_FAILURE);
}

// Statement handling

static void handle_statement(IrContext *context, AstStmt *stmt);

static void handle_stmt_block(IrContext *context, AstStmt *stmt) {
  if (!stmt) {
    return;
  }

  for (size_t i = 0; i < stmt->stmt_block.count; i++) {
    AstStmt *inner_stmt = stmt->stmt_block.statements[i];
    handle_statement(context, inner_stmt);
  }
}

static void handle_stmt_expression(IrContext *context, AstStmt *stmt) {
  if (!stmt) {
    return;
  }

  handle_expression(context, stmt->stmt_expr.expression);
}

static void handle_stmt_var_decl(IrContext *context, AstStmt *stmt) {
  if (!stmt) {
    return;
  }

  IrReg init_reg = handle_expression(context, stmt->stmt_var_decl.init);

  if (init_reg.id == NULL_REG.id) {
    init_reg = next_reg(context);
    IrInstr inst = {
      .op = IR_INST_LOAD_LITERAL,
      .res = init_reg,
      .literal = 0,
    };
    da_append(context->buffer, inst);
  }

  IrInstr inst = {
    .op = IR_INST_STORE_VAR,
    .res = init_reg,
    .var_name = stmt->stmt_var_decl.name,
  };
  da_append(context->buffer, inst);
}

static void handle_stmt_if(IrContext *context, AstStmt *stmt) {
  if (!stmt) {
    return;
  }

  IrLabel else_label = next_label(context);
  IrLabel end_label = next_label(context);

  IrReg cond_reg = handle_expression(context, stmt->stmt_if.condition);

  IrInstr cond_jump_instr = {
    .op = IR_INST_JZ,
    .arg1 = cond_reg,
    .target = else_label,
  };
  da_append(context->buffer, cond_jump_instr);

  handle_statement(context, stmt->stmt_if.then_branch);

  IrInstr jump_to_end_instr = {
    .op = IR_INST_JUMP,
    .target = end_label,
  };
  da_append(context->buffer, jump_to_end_instr);

  IrInstr else_label_instr = {
    .op = IR_INST_LABEL,
    .target = else_label,
  };
  da_append(context->buffer, else_label_instr);

  if (stmt->stmt_if.else_branch) {
    handle_statement(context, stmt->stmt_if.else_branch);
  }

  IrInstr end_label_instr = {
    .op = IR_INST_LABEL,
    .target = end_label,
  };
  da_append(context->buffer, end_label_instr);
}

static void handle_stmt_while(IrContext *context, AstStmt *stmt) {
  if (!stmt) {
    return;
  }

  IrLabel start_label = next_label(context);
  IrLabel end_label = next_label(context);

  IrInstr start_label_instr = {
    .op = IR_INST_LABEL,
    .target = start_label,
  };
  da_append(context->buffer, start_label_instr);

  IrReg cond_reg = handle_expression(context, stmt->stmt_while.condition);

  IrInstr cond_jump_instr = {
    .op = IR_INST_JZ,
    .arg1 = cond_reg,
    .target = end_label,
  };
  da_append(context->buffer, cond_jump_instr);

  handle_statement(context, stmt->stmt_while.body);

  IrInstr jump_to_start_instr = {
    .op = IR_INST_JUMP,
    .target = start_label,
  };
  da_append(context->buffer, jump_to_start_instr);

  IrInstr end_label_instr = {
    .op = IR_INST_LABEL,
    .target = end_label,
  };
  da_append(context->buffer, end_label_instr);
}

static void handle_statement(IrContext *context, AstStmt *stmt) {
  if (!stmt) {
    return;
  }

  switch (stmt->kind) {
  case STMT_BLOCK:
    return handle_stmt_block(context, stmt);
  case STMT_EXPRESSION:
    return handle_stmt_expression(context, stmt);
  case STMT_VAR_DECL:
    return handle_stmt_var_decl(context, stmt);

  case STMT_IF:
    return handle_stmt_if(context, stmt);
  case STMT_WHILE:
    return handle_stmt_while(context, stmt);

  case STMT_INVALID:
    fprintf(stderr, "ir_handle_statement: encountered STMT_INVALID node\n");
    exit(EXIT_FAILURE);
  }

  fprintf(stderr, "ir_handle_statement: unhandled AST statement kind %d\n", stmt->kind);
  exit(EXIT_FAILURE);
}

void ir_buffer_free(IrContext *context) {
  da_free(context->buffer);
}

void ir_gen_init(IrContext *context, CompilationUnit *comp_unit) {
  context->buffer = NULL;
  context->comp_unit = comp_unit;

  context->next_reg = 1;
  context->next_label = 1;
}

void emit_ir(IrContext *context, AstStmt *root) {
  handle_statement(context, root);
}
