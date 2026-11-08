#ifndef AST_H
#define AST_H

#include "common.h"
#include "lib/arena.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  UNOP_NEGATE,
  UNOP_NOT,
  UNOP_BIT_NOT,

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

  BINOP_BIT_OR,
  BINOP_BIT_XOR,
  BINOP_BIT_AND,
  BINOP_SHL,
  BINOP_SHR,

  BINOP_COUNT,
} BinaryOp;

typedef enum {
  LOGIC_OR,
  LOGIC_AND,

  LOGIC_COUNT,
} LogicalOp;

// Expression nodes

typedef enum {
  AST_EXPR_INVALID,

  // Leaves
  AST_EXPR_LITERAL,
  AST_EXPR_IDENTIFIER,

  // Composite literals
  AST_EXPR_ARRAY_LITERAL,

  // Access: address-of, dereference, subscript and call
  AST_EXPR_ADDR_OF,
  AST_EXPR_DEREF,
  AST_EXPR_SUBSCRIPT,
  AST_EXPR_SLICE,
  AST_EXPR_MEMBER,
  AST_EXPR_CALL,

  // Operators
  AST_EXPR_UNARY,
  AST_EXPR_BINARY,
  AST_EXPR_LOGICAL,
  AST_EXPR_CAST,

  // Mutation
  AST_EXPR_ASSIGN,
} AstExprKind;

typedef enum {
  AST_LITERAL_INT,
  AST_LITERAL_FLOAT,

  AST_LITERAL_TRUE,
  AST_LITERAL_FALSE,

  AST_LITERAL_NULL,
  AST_LITERAL_UNDEF,

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
  Symbol *symbol; // Filled in by sema
} AstExprIdentifier;

typedef struct {
  AstExpr *index_expr; // NULL if sequential
  AstExpr *value;
  uint32_t index; // Filled in by sema
  SourceLoc loc;
} AstArrayElement;

typedef struct {
  bool is_repeat;
  union {
    // Regular list: [1, 2, 3]
    // Sparse list: [2: 1, 5: 2]
    struct {
      AstArrayElement *elements;
      uint32_t element_count;
      bool is_sparse;
    } list;

    // Repeat syntax: [0; 256]
    struct {
      AstExpr *value;
      AstExpr *length_expr;
    } repeat;
  };
} AstExprArrayLiteral;

typedef struct {
  AstExpr *operand;
} AstExprAddrOf;

typedef struct {
  AstExpr *operand;
} AstExprDeref;

typedef struct {
  AstExpr *target;
  AstExpr *index;
} AstExprSubscript;

typedef struct {
  AstExpr *target;
  AstExpr *start;
  AstExpr *end;
} AstExprSlice;

typedef struct {
  AstExpr *object;
  const char *member;
  uint32_t offset; // Filled in by sema
} AstExprMember;

typedef struct {
  AstExpr *callee;
  AstExpr **arguments;
  uint32_t argument_count;
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

typedef struct {
  LogicalOp op;
  AstExpr *left;
  AstExpr *right;
} AstExprLogical;

typedef struct {
  AstExpr *operand;
  AstType *target;
  bool implicit;
} AstExprCast;

typedef struct {
  AstExpr *target;
  AstExpr *value;
} AstExprAssign;

struct AstExpr_ {
  AstExprKind kind;
  uint32_t id;
  SourceLoc loc;

  union {
    AstExprLiteral expr_literal;
    AstExprIdentifier expr_identifier;

    AstExprArrayLiteral expr_array_literal;

    AstExprAddrOf expr_addr_of;
    AstExprDeref expr_deref;
    AstExprSubscript expr_subscript;
    AstExprSlice expr_slice;
    AstExprMember expr_member;
    AstExprCall expr_call;

    AstExprUnary expr_unary;
    AstExprBinary expr_binary;
    AstExprLogical expr_logical;
    AstExprCast expr_cast;

    AstExprAssign expr_assign;
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
  uint32_t statement_count;
} AstStmtBlock;

typedef struct {
  AstExpr *expression;
} AstStmtExpression;

typedef struct {
  const char *name;
  AstType *declared_type;
  AstExpr *init;

  Symbol *symbol; // Filled in by sema
  bool is_undef;  // Filled in by sema
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
  Symbol *symbol; // Filled in by sema
} AstParam;

typedef struct {
  const char *name;

  AstParam *params;
  uint32_t param_count;

  AstType *declared_return_type;
  AstStmt *body;

  Symbol *symbol; // Filled in by sema
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
  AST_TYPE_ARRAY,
  AST_TYPE_SLICE,
} AstTypeKind;

typedef struct {
  AstType *base_type;
} AstTypePointer;

typedef struct {
  AstType *elem_type;
  AstExpr *length_expr;
} AstTypeArray;

typedef struct {
  AstType *elem_type;
} AstTypeSlice;

struct AstType_ {
  AstTypeKind kind;
  uint32_t id;
  SourceLoc loc;

  union {
    const char *named;
    AstTypePointer pointer;
    AstTypeArray array;
    AstTypeSlice slice;
  };
};

// Program

typedef struct {
  AstDecl **decls;
  uint32_t decl_count;
} AstProgram;

AstExpr *ast_expr_new(Arena *arena, AstExprKind kind, SourceLoc loc);
AstStmt *ast_stmt_new(Arena *arena, AstStmtKind kind, SourceLoc loc);
AstDecl *ast_decl_new(Arena *arena, AstDeclKind kind, SourceLoc loc);
AstType *ast_type_new(Arena *arena, AstTypeKind kind, SourceLoc loc);

uint32_t ast_expr_count(void);
uint32_t ast_stmt_count(void);
uint32_t ast_decl_count(void);
uint32_t ast_type_count(void);

#endif // AST_H
