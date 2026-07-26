#include "debug.h"
#include "ast.h"

#include <stdio.h>

static const char *unop_strings[UNARYOP_COUNT];
static const char *binop_strings[BINARYOP_COUNT];

static void print_indent(int indent) {
  printf("%*s", indent * 2, "");
}

static void print_ast_node(AstArena *arena, AstId node_id, int indent);

static void print_ast_field(const char *label, AstArena *arena, AstId node_id, int indent) {
  print_indent(indent);
  printf("%s\n", label);
  if (node_id != AST_NULL_ID) {
    print_ast_node(arena, node_id, indent + 1);
  } else {
    print_indent(indent + 1);
    printf("<null>\n");
  }
}

static void print_ast_node(AstArena *arena, AstId node_id, int indent) {
  if (node_id == AST_NULL_ID) {
    print_indent(indent);
    printf("<null>\n");
    return;
  }

  AstNode node = arena->nodes[node_id];

  print_indent(indent);

  switch (node.type) {
  case AST_INVALID:
    printf("Invalid\n");
    break;
  case AST_LITERAL:
    printf("Literal: %ld\n", node.literal.value);
    break;
  case AST_IDENTIFIER:
    printf("Identifier: %s\n", node.identifier.name);
    break;

  case AST_UNARY_OP:
    printf("Unary Op: '%s'\n", unop_strings[node.expr_unary.op]);
    print_ast_field("Operand:", arena, node.expr_unary.operand, indent + 1);
    break;
  case AST_BINARY_OP:
    printf("Binary Op: '%s'\n", binop_strings[node.expr_binary.op]);
    print_ast_field("Left:", arena, node.expr_binary.left, indent + 1);
    print_ast_field("Right:", arena, node.expr_binary.right, indent + 1);
    break;

  default:
    printf("Unknown node type: %d\n", node.type);
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
    printf("Token: %s %.*s\n", lexer_render_token_type(token.type), (int)token.lexeme_len, token.lexeme);
    break;
  default:
    printf("Token: %s\n", lexer_render_token_type(token.type));
  }
}

void debug_print_ast_tree(AstArena *arena, AstId root) {
  print_ast_node(arena, root, 0);
}

void debug_print_ast_sexpr(AstArena *arena, AstId root) {
  if (root == AST_NULL_ID) {
    printf("<null>\n");
    return;
  }

  AstNode *node = &arena->nodes[root];

  switch (node->type) {
  case AST_LITERAL:
    printf("%ld", node->literal.value);
    break;
  case AST_IDENTIFIER:
    printf("%s", node->identifier.name);
    break;
  case AST_UNARY_OP:
    printf("(%s ", unop_strings[node->expr_unary.op]);
    debug_print_ast_sexpr(arena, node->expr_unary.operand);
    printf(")");
    break;
  case AST_BINARY_OP:
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

static const char *unop_strings[UNARYOP_COUNT] = {
  [UNARYOP_NEGATE] = "-",
};

static const char *binop_strings[BINARYOP_COUNT] = {
  [BINARYOP_ADD] = "+",
  [BINARYOP_SUB] = "-",
  [BINARYOP_MUL] = "*",
  [BINARYOP_DIV] = "/",
};
