#ifndef AST_H
#define AST_H

#include <stddef.h>
#include <stdint.h>

#define AST_NULL_ID ((AstId)0)

typedef size_t AstId;

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

typedef struct {
  int64_t value;
} AstLiteral;

typedef struct {
  char *name;
} AstIdentifier;

// Expression nodes

typedef struct {
  UnaryOp op;
  AstId operand;
} AstExprUnary;

typedef struct {
  BinaryOp op;
  AstId left;
  AstId right;
} AstExprBinary;

typedef struct {
  AstNodeType type;

  union {
    AstLiteral literal;
    AstIdentifier identifier;

    AstExprUnary expr_unary;
    AstExprBinary expr_binary;
  };
} AstNode;

typedef struct {
  AstNode *nodes;
  size_t count;
  size_t capacity;
} AstArena;

void ast_arena_init(AstArena *arena);
void ast_arena_free(AstArena *arena);

AstId ast_node_new(AstArena *arena, AstNodeType type);

#endif // AST_H
