#ifndef AST_H
#define AST_H

#include "lib/arena.h"

#include <stddef.h>
#include <stdint.h>

typedef struct AstExpr_ AstExpr;

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
  EXPR_INVALID,

  EXPR_LITERAL,
  EXPR_IDENTIFIER,

  EXPR_UNARY,
  EXPR_BINARY,
} AstNodeType;

typedef struct {
  int64_t value;
} AstLiteral;

typedef struct {
  char *name;
} AstIdentifier;

// Expression nodes

typedef struct {
  UnaryOp op;
  AstExpr *operand;
} AstExprUnary;

typedef struct {
  BinaryOp op;
  AstExpr *left;
  AstExpr *right;
} AstExprBinary;

struct AstExpr_ {
  AstNodeType type;

  union {
    AstLiteral literal;
    AstIdentifier identifier;

    AstExprUnary expr_unary;
    AstExprBinary expr_binary;
  };
};

void ast_arena_free(Arena *arena);

AstExpr *ast_expr_new(Arena *arena, AstNodeType type);

#endif // AST_H
