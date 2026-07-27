#include "ast.h"
#include "ir.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define IR_BUFFER_INIT_CAPACITY 1024

#define next_reg(context)       \
  (IrReg) {                     \
    .id = (context)->next_reg++ \
  }

static IrReg handle_expression(IrContext *context, AstId id);

static AstNode *get_ast_node(IrContext *context, AstId id) {
  return &context->ast_arena->nodes[id];
}

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

static void emit_ir_instr(IrContext *context, IrInstr inst) {
  if (context->buffer->count >= context->buffer->capacity) {
    context->buffer->capacity *= 2;
    context->buffer->insts = realloc(context->buffer->insts, context->buffer->capacity * sizeof(IrInstr));

    if (context->buffer->insts == NULL) {
      fprintf(stderr, "emit_ir: failed to reallocate IR buffer\n");
      exit(EXIT_FAILURE);
    }
  }

  size_t idx = context->buffer->count++;
  context->buffer->insts[idx] = inst;
}

static IrReg handle_literal(IrContext *context, AstNode *node) {
  IrReg res = next_reg(context);

  IrInstr inst = {
    .op = IR_INST_LOAD_LITERAL,
    .res = res,
    .literal = node->literal.value,
  };
  emit_ir_instr(context, inst);

  return res;
}

static IrReg handle_unary_op(IrContext *context, AstNode *node) {
  IrReg operand = handle_expression(context, node->expr_unary.operand);

  IrReg res = next_reg(context);

  IrInstr inst = {
    .op = get_unary_ir_op(node->expr_unary.op),
    .res = res,
    .arg1 = operand,
  };
  emit_ir_instr(context, inst);

  return res;
}

static IrReg handle_binary_op(IrContext *context, AstNode *node) {
  IrReg left = handle_expression(context, node->expr_binary.left);
  IrReg right = handle_expression(context, node->expr_binary.right);

  IrReg res = next_reg(context);

  IrInstr inst = {
    .op = get_binary_ir_op(node->expr_binary.op),
    .res = res,
    .arg1 = left,
    .arg2 = right,
  };
  emit_ir_instr(context, inst);

  return res;
}

static IrReg handle_expression(IrContext *context, AstId id) {
  if (id == AST_NULL_ID) {
    return NULL_REG;
  }

  AstNode *node = get_ast_node(context, id);

  switch (node->type) {
  case AST_LITERAL:
    return handle_literal(context, node);

  case AST_UNARY_OP:
    return handle_unary_op(context, node);

  case AST_BINARY_OP:
    return handle_binary_op(context, node);

  default:
    fprintf(stderr, "gen_ir_expression: unhandled AST node type %d\n", node->type);
    exit(EXIT_FAILURE);
  }
}

void ir_buffer_init(IrBuffer *buffer) {
  buffer->capacity = IR_BUFFER_INIT_CAPACITY;
  buffer->count = 0;
  buffer->insts = calloc(buffer->capacity, sizeof(IrInstr));

  if (buffer->insts == NULL) {
    fprintf(stderr, "Failed to allocate memory for IR buffer\n");
    exit(EXIT_FAILURE);
  }
}

void ir_buffer_free(IrBuffer *buffer) {
  free(buffer->insts);
  buffer->insts = NULL;
  buffer->count = 0;
  buffer->capacity = 0;
}

void ir_gen_init(IrContext *context, IrBuffer *buffer, AstArena *ast_arena) {
  context->buffer = buffer;
  context->ast_arena = ast_arena;

  context->next_reg = 1;
}

void emit_ir(IrContext *context, AstId root_id) {
  handle_expression(context, root_id);
}
