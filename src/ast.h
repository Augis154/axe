#ifndef AST_H
#define AST_H

#include "lib/arena.h"

#include <stddef.h>
#include <stdint.h>

typedef struct AstExpr_ AstExpr;
typedef struct AstStmt_ AstStmt;
typedef struct AstDecl_ AstDecl;

typedef enum {
  UNARYOP_NEGATE,

  UNARYOP_COUNT,
} UnaryOp;

typedef enum {
  BINARYOP_ADD,
  BINARYOP_SUB,
  BINARYOP_MUL,
  BINARYOP_DIV,

  BINARYOP_LESS,
  BINARYOP_GREATER,
  BINARYOP_EQEQ,
  BINARYOP_NOT_EQ,
  BINARYOP_LESS_EQ,
  BINARYOP_GREATER_EQ,

  BINARYOP_COUNT,
} BinaryOp;

// Expression nodes

typedef enum {
  EXPR_INVALID,

  EXPR_LITERAL,
  EXPR_IDENTIFIER,

  EXPR_ASSIGN,

  EXPR_UNARY,
  EXPR_BINARY,
} AstExprType;

typedef enum {
  LITERAL_INT,
  LITERAL_FLOAT,
  LITERAL_CHAR,
  LITERAL_STRING,
} AstLiteralType;

typedef struct {
  AstLiteralType type;
  union {
    int64_t int_value;
    double float_value;
    char char_value;
    const char *string_value;
  };
} AstLiteral;

typedef struct {
  const char *name;
} AstIdentifier;

typedef struct {
  AstExpr *target;
  AstExpr *value;
} AstAssignment;

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
  AstExprType type;
  uint32_t id;

  union {
    AstLiteral literal;
    AstIdentifier identifier;

    AstAssignment assignment;

    AstExprUnary expr_unary;
    AstExprBinary expr_binary;
  };
};

// Statement nodes

typedef enum {
  STMT_INVALID,

  STMT_BLOCK,
  STMT_EXPRESSION,
  STMT_VAR_DECL,
  STMT_IF,
} AstStmtType;

typedef struct {
  AstStmt **statements;
  size_t count;
} AstStmtBlock;

typedef struct {
  AstExpr *expression;
} AstStmtExpr;

typedef struct {
  const char *name;
  const char *type;
  AstExpr *init;
} AstStmtVarDecl;

typedef struct {
  AstExpr *condition;
  AstStmt *then_branch;
  AstStmt *else_branch;
} AstIfStmt;

struct AstStmt_ {
  AstStmtType type;
  uint32_t id;

  union {
    AstStmtBlock stmt_block;
    AstStmtExpr stmt_expr;
    AstStmtVarDecl stmt_var_decl;
    AstIfStmt stmt_if;
  };
};

// Declaration nodes

typedef enum {
  DECL_INVALID,
} AstDeclType;

typedef struct {
  char *name;
  AstExpr *init;
} AstDeclVar;

struct AstDecl_ {
  AstDeclType type;
  uint32_t id;

  union {
    AstDeclVar decl_var;
  };
};

void ast_arena_free(Arena *arena);

AstExpr *ast_expr_new(Arena *arena, AstExprType type);
AstStmt *ast_stmt_new(Arena *arena, AstStmtType type);
AstDecl *ast_decl_new(Arena *arena, AstDeclType type);

#endif // AST_H
