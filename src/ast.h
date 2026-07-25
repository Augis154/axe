#ifndef AST_H
#define AST_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
  UNARYOP_NEGATE,

  UNARYOP_COUNT,
} UnaryOp;

typedef enum {
  BINARYOP_ADD,
  BINARYOP_SUB,
  BINARYOP_MUL,
  BINARYOP_DIV,

  BINARYOP_COUNT,
} BinaryOp;

typedef enum {
  AST_INVALID,

  AST_LITERAL,
  AST_IDENTIFIER,

  AST_UNARY_OP,
  AST_BINARY_OP,
} AstNodeType;

typedef struct AstNode_ AstNode;

typedef struct {
  int64_t value;
} AstLiteral;

typedef struct {
  char *name;
} AstIdentifier;

// Expression nodes

typedef struct {
  UnaryOp op;
  AstNode *operand;
} AstExprUnary;

typedef struct {
  BinaryOp op;
  AstNode *left;
  AstNode *right;
} AstExprBinary;

struct AstNode_ {
  AstNodeType type;

  union {
    AstLiteral literal;
    AstIdentifier identifier;

    AstExprUnary expr_unary;
    AstExprBinary expr_binary;
  };
};

AstNode *ast_node_new(AstNodeType type);

#endif // AST_H