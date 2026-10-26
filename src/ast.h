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
  BINOP_EQ,
  BINOP_NOT_EQ,
  BINOP_LESS_EQ,
  BINOP_GREATER_EQ,

  BINOP_COUNT,
} BinaryOp;

// Expression nodes

typedef enum {
  AST_EXPR_INVALID,

  AST_EXPR_LITERAL,
  AST_EXPR_IDENTIFIER,

  AST_EXPR_ASSIGN,
  AST_EXPR_CALL,

  AST_EXPR_UNARY,
  AST_EXPR_BINARY,
} AstExprKind;

typedef enum {
  AST_LITERAL_INT,
  AST_LITERAL_FLOAT,
  AST_LITERAL_CHAR,
  AST_LITERAL_STRING,
} AstLiteralKind;

typedef struct {
  AstLiteralKind kind;
  union {
    int64_t int_value;
    double float_value;
    char char_value;
    const char *string_value;
  };
} AstExprLiteral;

typedef struct {
  const char *name;
  Symbol *symbol; // Filled in by sema.
} AstExprIdentifier;

typedef struct {
  AstExpr *target;
  AstExpr *value;
} AstExprAssign;

typedef struct {
  AstExpr *callee;
  AstExpr **arguments;
  size_t argument_count;
} AstExprCall;

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
    AstExprLiteral expr_literal;
    AstExprIdentifier expr_identifier;

    AstExprAssign expr_assign;
    AstExprCall expr_call;

    AstExprUnary expr_unary;
    AstExprBinary expr_binary;
  };
};

// Statement nodes

typedef enum {
  AST_STMT_INVALID,

  AST_STMT_BLOCK,
  AST_STMT_EXPRESSION,
  AST_STMT_LOCAL_VAR,

  AST_STMT_RETURN,
  AST_STMT_BREAK,
  AST_STMT_CONTINUE,

  AST_STMT_IF,
  AST_STMT_WHILE,
} AstStmtKind;

typedef struct {
  AstStmt **statements;
  size_t statement_count;
} AstStmtBlock;

typedef struct {
  AstExpr *expression;
} AstStmtExpression;

typedef struct {
  const char *name;
  AstType *declared_type;
  AstExpr *init;

  Symbol *symbol; // Filled in by sema.
} AstStmtLocalVar;

typedef struct {
  AstExpr *value;
} AstStmtReturn;

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
    AstStmtExpression stmt_expression;
    AstStmtLocalVar stmt_local_var;

    AstStmtReturn stmt_return;

    AstStmtIf stmt_if;
    AstStmtWhile stmt_while;
  };
};

// Declaration nodes

typedef enum {
  AST_DECL_INVALID,
  AST_DECL_FUNCTION,
} AstDeclKind;

typedef struct {
  SourceLoc loc;
  const char *name;
  AstType *declared_type;
  Symbol *symbol; // Filled in by sema.
} AstParam;

typedef struct {
  const char *name;

  AstParam *params;
  size_t param_count;

  AstType *declared_return_type;
  AstStmt *body;

  Symbol *symbol; // Filled in by sema.
} AstDeclFunction;

struct AstDecl_ {
  AstDeclKind kind;
  uint32_t id;
  SourceLoc loc;

  union {
    AstDeclFunction decl_function;
  };
};

// Type nodes

typedef enum {
  AST_TYPE_INVALID,

  AST_TYPE_NAMED,
  AST_TYPE_POINTER,
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

// Program

typedef struct {
  AstDecl **decls;
  size_t decl_count;
} AstProgram;

AstExpr *ast_expr_new(Arena *arena, AstExprKind kind, SourceLoc loc);
AstStmt *ast_stmt_new(Arena *arena, AstStmtKind kind, SourceLoc loc);
AstDecl *ast_decl_new(Arena *arena, AstDeclKind kind, SourceLoc loc);
AstType *ast_type_new(Arena *arena, AstTypeKind kind, SourceLoc loc);

uint32_t ast_expr_count();
uint32_t ast_stmt_count();
uint32_t ast_decl_count();
uint32_t ast_type_count();

#endif // AST_H
