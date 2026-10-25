#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>

typedef struct AstExpr_ AstExpr;
typedef struct AstStmt_ AstStmt;
typedef struct AstDecl_ AstDecl;
typedef struct AstType_ AstType;

typedef struct Type_ Type;
typedef struct Symbol_ Symbol;
typedef struct Scope_ Scope;

typedef struct {
  const char *filename;
  size_t line;
} SourceLoc;

#endif // COMMON_H