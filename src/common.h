#ifndef COMMON_H
#define COMMON_H

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

#define UNUSED(x) ((void)(x))

#if defined(__GNUC__) || defined(__clang__)
#define UNREACHABLE(message) (assert(0 && message), __builtin_unreachable())
#else
#define UNREACHABLE(message) (assert(0 && message), abort())
#endif

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