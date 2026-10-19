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

// Expression handling

static IrReg handle_expression(IrContext *context, AstExpr *node);

static IrOp get_unary_ir_op(UnaryOp op) {
  switch (op) {
  case UNARYOP_NEGATE:
    return IR_INST_NEGATE;
  default:
    fprintf(stderr, "get_unary_ir_op: unhandled UnaryOp %d\n", op);
    exit(EXIT_FAILURE);
  }
}

static IrOp get_binary_ir_op(BinaryOp op) {
  switch (op) {
  case BINARYOP_ADD:
    return IR_INST_ADD;
  case BINARYOP_SUB:
    return IR_INST_SUB;
  case BINARYOP_MUL:
    return IR_INST_MUL;
  case BINARYOP_DIV:
    return IR_INST_DIV;
  default:
    fprintf(stderr, "get_binary_ir_op: unhandled BinaryOp %d\n", op);
    exit(EXIT_FAILURE);
  }
}

static IrReg handle_literal(IrContext *context, AstExpr *node) {
  IrReg res = next_reg(context);

  IrInstr inst = {
    .op = IR_INST_LOAD_LITERAL,
    .res = res,
    .literal = node->literal.value,
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

  switch (node->type) {
  case EXPR_LITERAL:
    return handle_literal(context, node);

  case EXPR_IDENTIFIER:
    return handle_identifier(context, node);

  case EXPR_UNARY:
    return handle_unary_op(context, node);

  case EXPR_BINARY:
    return handle_binary_op(context, node);

  default:
    fprintf(stderr, "gen_ir_expression: unhandled AST node type %d\n", node->type);
    exit(EXIT_FAILURE);
  }
}

// Statement handling

static IrReg handle_var_declaration(IrContext *context, AstStmt *stmt) {
  if (!stmt) {
    return NULL_REG;
  }

  IrReg init_reg = handle_expression(context, stmt->stmt_var_decl.init);

  IrInstr inst = {
    .op = IR_INST_STORE_VAR,
    .res = init_reg,
    .var_name = stmt->stmt_var_decl.name,
  };
  da_append(context->buffer, inst);

  return NULL_REG;
}

static IrReg handle_statement(IrContext *context, AstStmt *stmt) {
  if (!stmt) {
    return NULL_REG;
  }

  switch (stmt->type) {
  case STMT_EXPRESSION:
    return handle_expression(context, stmt->stmt_expr.expression);
  case STMT_VAR_DECL:
    return handle_var_declaration(context, stmt);

  default:
    fprintf(stderr, "gen_ir_statement: unhandled AST statement type %d\n", stmt->type);
    exit(EXIT_FAILURE);
  }
}

void ir_buffer_free(IrContext *context) {
  da_free(context->buffer);
}

void ir_gen_init(IrContext *context, Arena *ast_arena) {
  context->buffer = NULL;
  context->ast_arena = ast_arena;

  context->next_reg = 1;
}

void emit_ir(IrContext *context, AstStmt *root) {
  handle_statement(context, root);
}
