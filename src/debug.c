#include "debug.h"
#include "ast.h"

#include "lib/array.h"

#include <stdio.h>

static const char *unop_strings[UNARYOP_COUNT];
static const char *binop_strings[BINARYOP_COUNT];

static void print_indent(int indent) { printf("%*s", indent * 2, ""); }

static void print_ast_node(Arena *arena, AstExpr *node, int indent);

static void print_ast_field(const char *label, Arena *arena, AstExpr *node,
                            int indent) {
  print_indent(indent);
  printf("%s\n", label);
  if (node) {
    print_ast_node(arena, node, indent + 1);
  } else {
    print_indent(indent + 1);
    printf("<null>\n");
  }
}

static void print_ast_node(Arena *arena, AstExpr *node, int indent) {
  if (!node) {
    print_indent(indent);
    printf("<null>\n");
    return;
  }

  print_indent(indent);

  switch (node->type) {
  case EXPR_INVALID:
    printf("Invalid\n");
    break;
  case EXPR_LITERAL:
    printf("Literal: %ld\n", node->literal.value);
    break;
  case EXPR_IDENTIFIER:
    printf("Identifier: %s\n", node->identifier.name);
    break;

  case EXPR_UNARY:
    printf("Unary Op: '%s'\n", unop_strings[node->expr_unary.op]);
    print_ast_field("Operand:", arena, node->expr_unary.operand, indent + 1);
    break;
  case EXPR_BINARY:
    printf("Binary Op: '%s'\n", binop_strings[node->expr_binary.op]);
    print_ast_field("Left:", arena, node->expr_binary.left, indent + 1);
    print_ast_field("Right:", arena, node->expr_binary.right, indent + 1);
    break;

  default:
    printf("Unknown node type: %d\n", node->type);
    break;
  }
}

void debug_print_token(Token token) {
  switch (token.type) {
  case TOKEN_ERROR:
    printf("Error: %.*s\n", (int)token.lexeme_len, token.lexeme);
    break;
  case TOKEN_NUMBER:
  case TOKEN_IDENT:
    printf("Token: %s %.*s\n", lexer_render_token_type(token.type),
           (int)token.lexeme_len, token.lexeme);
    break;
  default:
    printf("Token: %s\n", lexer_render_token_type(token.type));
  }
}

void debug_print_ast_tree(Arena *arena, AstExpr *root) {
  print_ast_node(arena, root, 0);
}

void debug_print_ast_sexpr(Arena *arena, AstExpr *node) {
  if (!node) {
    printf("<null>\n");
    return;
  }

  switch (node->type) {
  case EXPR_LITERAL:
    printf("%ld", node->literal.value);
    break;
  case EXPR_IDENTIFIER:
    printf("%s", node->identifier.name);
    break;

  case EXPR_UNARY:
    printf("(%s ", unop_strings[node->expr_unary.op]);
    debug_print_ast_sexpr(arena, node->expr_unary.operand);
    printf(")");
    break;

  case EXPR_BINARY:
    printf("(%s ", binop_strings[node->expr_binary.op]);
    debug_print_ast_sexpr(arena, node->expr_binary.left);
    printf(" ");
    debug_print_ast_sexpr(arena, node->expr_binary.right);
    printf(")");
    break;

  default:
    printf("<unknown node type>");
  }
}

void debug_print_ir_buffer(IrContext *ir_ctx) {
  for (size_t i = 0; i < da_len(ir_ctx->buffer); i++) {
    IrInstr *inst = &ir_ctx->buffer[i];

    switch (inst->op) {
    case IR_INST_NONE:
      printf("%zu: <none>\n", i);
      break;

    case IR_INST_LOAD_LITERAL:
      printf("%zu: r%u = %ld\n", i, inst->res.id, inst->literal);
      break;
    case IR_INST_LOAD_VAR:
      printf("%zu: r%u = LOAD_VAR(var_id=%u)\n", i, inst->res.id,
             inst->arg1.id);
      break;

      // Unary operations
    case IR_INST_NEGATE:
      printf("%zu: r%u = -r%u\n", i, inst->res.id, inst->arg1.id);
      break;

      // Binary operations
    case IR_INST_ADD:
      printf("%zu: r%u = r%u + r%u\n", i, inst->res.id, inst->arg1.id,
             inst->arg2.id);
      break;
    case IR_INST_SUB:
      printf("%zu: r%u = r%u - r%u\n", i, inst->res.id, inst->arg1.id,
             inst->arg2.id);
      break;
    case IR_INST_MUL:
      printf("%zu: r%u = r%u * r%u\n", i, inst->res.id, inst->arg1.id,
             inst->arg2.id);
      break;
    case IR_INST_DIV:
      printf("%zu: r%u = r%u / r%u\n", i, inst->res.id, inst->arg1.id,
             inst->arg2.id);
      break;

    default:
      printf("%zu: Unknown instruction\n", i);
      break;
    }
  }
}

static const char *unop_strings[UNARYOP_COUNT] = {
    [UNARYOP_NEGATE] = "-",
};

static const char *binop_strings[BINARYOP_COUNT] = {
    [BINARYOP_ADD] = "+",
    [BINARYOP_SUB] = "-",
    [BINARYOP_MUL] = "*",
    [BINARYOP_DIV] = "/",
};
