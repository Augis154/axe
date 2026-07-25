#include "ast.h"

#include <stdlib.h>

AstNode *ast_node_new(AstNodeType type) {
  AstNode *node = malloc(sizeof(AstNode));
  node->type = type;
  return node;
}