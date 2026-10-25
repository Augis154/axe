#ifndef AST_H
#define AST_H

#include "common.h"
#include "lib/arena.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
  UNOP_NEGATE,
  UNOP_NOT,

  UNOP_COUNT,
} UnaryOp;

typedef enum {
  BINOP_ADD,
  BINOP_SUB,
  BINOP_MUL,
  BINOP_DIV,

  BINOP_LESS,
  BINOP_GREATER,
  BINOP_EQEQ,
  BINOP_NOT_EQ,
  BINOP_LESS_EQ,
  BINOP_GREATER_EQ,

  BINOP_COUNT,
} BinaryOp;

// Expression nodes

typedef enum {
  EXPR_INVALID,

  EXPR_LITERAL,
  EXPR_IDENTIFIER,

  EXPR_ASSIGN,

  EXPR_UNARY,
  EXPR_BINARY,
} AstExprKind;

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
  Symbol *symbol;
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
  AstExprKind kind;
  uint32_t id;
  SourceLoc loc;

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
  STMT_BREAK,
  STMT_CONTINUE,

  STMT_IF,
  STMT_WHILE,
} AstStmtKind;

typedef struct {
  AstStmt **statements;
  size_t count;
} AstStmtBlock;

typedef struct {
  AstExpr *expression;
} AstStmtExpr;

typedef struct {
  const char *name;
  AstType *type;
  AstExpr *init;
} AstStmtVarDecl;

typedef struct {
  AstExpr *condition;
  AstStmt *then_branch;
  AstStmt *else_branch;
} AstStmtIf;

typedef struct {
  AstExpr *condition;
  AstStmt *body;
} AstStmtWhile;

struct AstStmt_ {
  AstStmtKind kind;
  uint32_t id;
  SourceLoc loc;

  union {
    AstStmtBlock stmt_block;
    AstStmtExpr stmt_expr;
    AstStmtVarDecl stmt_var_decl;

    AstStmtIf stmt_if;
    AstStmtWhile stmt_while;
  };
};

// Declaration nodes

typedef enum {
  DECL_INVALID,
} AstDeclKind;

typedef struct {
  char *name;
  AstExpr *init;
} AstDeclVar;

struct AstDecl_ {
  AstDeclKind kind;
  uint32_t id;
  SourceLoc loc;

  union {
    AstDeclVar decl_var;
  };
};

// Type nodes

typedef enum {
  TYPE_NAMED,
  TYPE_POINTER,
} AstTypeKind;

typedef struct {
  AstType *base;
} AstTypePointer;

struct AstType_ {
  AstTypeKind kind;
  uint32_t id;
  SourceLoc loc;

  union {
    const char *named;
    AstTypePointer pointer;
  };
};

void ast_arena_free(Arena *arena);

AstExpr *ast_expr_new(Arena *arena, AstExprKind kind, SourceLoc loc);
AstStmt *ast_stmt_new(Arena *arena, AstStmtKind kind, SourceLoc loc);
AstDecl *ast_decl_new(Arena *arena, AstDeclKind kind, SourceLoc loc);
AstType *ast_type_new(Arena *arena, AstTypeKind kind, SourceLoc loc);

uint32_t ast_expr_count();
uint32_t ast_stmt_count();
uint32_t ast_decl_count();
uint32_t ast_type_count();

#endif // AST_H
