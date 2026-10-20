#include "ast.h"
#include "frontend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  PREC_NONE = 0,
  PREC_ASSIGNMENT, // =
  PREC_TERM,       // + -
  PREC_FACTOR,     // * /
  PREC_UNARY,      // -
  PREC_CALL,       // . () []
} Precedence;

typedef AstExpr *(*ParsePrefixFn)(Parser *parser);
typedef AstExpr *(*ParseInfixFn)(Parser *parser, AstExpr *left);

typedef struct {
  Precedence precedence;
  ParsePrefixFn prefix;
  ParseInfixFn infix;
} ParseRule;

static const ParseRule rules[TOKEN_COUNT];

static const UnaryOp token_to_unary_op[TOKEN_COUNT];
static const BinaryOp token_to_binary_op[TOKEN_COUNT];

static const ParseRule *get_rule(TokenType type) {
  if (type < 0 || type >= TOKEN_COUNT) {
    return NULL;
  }
  return &rules[type];
}

static const UnaryOp get_unary_op(TokenType type) {
  if (type < 0 || type >= TOKEN_COUNT) {
    return -1; // Invalid UnaryOp
  }
  return token_to_unary_op[type];
}

static const BinaryOp get_binary_op(TokenType type) {
  if (type < 0 || type >= TOKEN_COUNT) {
    return -1; // Invalid BinaryOp
  }
  return token_to_binary_op[type];
}

static void error_at(Parser *parser, Token token, const char *message) {
  if (parser->panic_mode) {
    return;
  }
  parser->panic_mode = true;

  fprintf(stderr, "[line %zu] Error", token.line);

  if (token.type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token.type == TOKEN_ERROR) {
    // Nothing to print
  } else {
    fprintf(stderr, " at '%.*s'", (int)token.lexeme_len, token.lexeme);
  }

  fprintf(stderr, ": %s\n", message);

  parser->comp_unit->had_error = true;
}

static void advance(Parser *parser) {
  parser->current = parser->next;

  for (;;) {
    parser->next = lexer_get_token(&parser->lexer);

    if (parser->next.type != TOKEN_ERROR) {
      break;
    }

    error_at(parser, parser->next, parser->next.lexeme);
    free((void *)parser->next.lexeme);
  }
}

static bool match(Parser *parser, TokenType type) {
  if (parser->next.type == type) {
    advance(parser);
    return true;
  }
  return false;
}

#define expect(type, message)                \
  if (!match(parser, type)) {                \
    error_at(parser, parser->next, message); \
    return NULL;                             \
  }

// Expression parsing functions

static AstExpr *parse_precedence(Parser *parser, Precedence precedence) {
  advance(parser);
  ParsePrefixFn prefix_rule = get_rule(parser->current.type)->prefix;

  if (!prefix_rule) {
    // Handle error: no prefix rule for this token
    error_at(parser, parser->current, "Expected expression.");
    return NULL;
  }

  AstExpr *left = prefix_rule(parser);

  while (precedence <= get_rule(parser->next.type)->precedence) {
    advance(parser);
    ParseInfixFn infix_rule = get_rule(parser->current.type)->infix;

    if (!infix_rule) {
      // Handle error: no infix rule for this token
      return left;
    }

    left = infix_rule(parser, left);
  }

  return left;
}

static AstExpr *parse_number(Parser *parser) {
  AstExpr *expr = ast_expr_new(&parser->comp_unit->ast_arena, EXPR_LITERAL);

  expr->literal.value = strtoll(parser->current.lexeme, NULL, 10);
  return expr;
}

static AstExpr *parse_identifier(Parser *parser) {
  AstExpr *expr = ast_expr_new(&parser->comp_unit->ast_arena, EXPR_IDENTIFIER);

  size_t length = parser->current.lexeme_len;
  expr->identifier.name = strndup(parser->current.lexeme, length);
  return expr;
}

static AstExpr *parse_grouping(Parser *parser) {
  AstExpr *expr = parse_expression(parser);

  expect(TOKEN_CPAREN, "Expected ')' after expression");

  return expr;
}

static AstExpr *parse_unary(Parser *parser) {
  TokenType operator = parser->current.type;
  const ParseRule *rule = get_rule(operator);

  AstExpr *op_expr = parse_precedence(parser, PREC_UNARY);

  AstExpr *unary_expr = ast_expr_new(&parser->comp_unit->ast_arena, EXPR_UNARY);
  unary_expr->expr_unary.op = get_unary_op(operator);
  unary_expr->expr_unary.operand = op_expr;

  return unary_expr;
}

static AstExpr *parse_binary(Parser *parser, AstExpr *left) {
  TokenType operator = parser->current.type;
  const ParseRule *rule = get_rule(operator);

  AstExpr *right = parse_precedence(parser, rule->precedence + 1);

  AstExpr *binary_expr = ast_expr_new(&parser->comp_unit->ast_arena, EXPR_BINARY);
  binary_expr->expr_binary.op = get_binary_op(operator);
  binary_expr->expr_binary.left = left;
  binary_expr->expr_binary.right = right;

  return binary_expr;
}

// Statement parsing functions

static AstStmt *parse_expression_statement(Parser *parser) {
  AstExpr *expr = parse_expression(parser);

  expect(TOKEN_SEMICOLON, "Expected ';' after expression");

  AstStmt *stmt = ast_stmt_new(&parser->comp_unit->ast_arena, STMT_EXPRESSION);
  stmt->stmt_expr.expression = expr;

  return stmt;
}

static AstStmt *parse_variable_declaration(Parser *parser) {
  advance(parser);
  expect(TOKEN_IDENT, "Expected variable name after 'var'.");

  Token name_token = parser->current;

  char *type_name = NULL;
  if (match(parser, TOKEN_COLON)) {
    expect(TOKEN_IDENT, "Expected type name after ':'.");
    type_name = strndup(parser->current.lexeme, parser->current.lexeme_len);
  }

  AstExpr *init_expr = NULL;
  if (match(parser, TOKEN_EQUAL)) {
    init_expr = parse_expression(parser);
  }

  expect(TOKEN_SEMICOLON, "Expected ';' after variable declaration.");

  // 4. Register in Symbol Table (if using single-pass resolution)
  // if (p->sym_table != NULL) {
  //   if (scope_contains_current(p->sym_table->current, name_token.lexeme)) {
  //     error_at_previous(p, "Variable already declared in this scope.");
  //   } else {
  //     symbol_table_define(p->sym_table, name_token.lexeme, SYM_VARIABLE);
  //   }
  // }

  if (!type_name && !init_expr) {
    error_at(parser, name_token, "Variable declaration must have either a type annotation or an initializer.");
    return NULL;
  }

  AstStmt *stmt = ast_stmt_new(&parser->comp_unit->ast_arena, STMT_VAR_DECL);
  stmt->stmt_var_decl.name = strndup(name_token.lexeme, name_token.lexeme_len);
  stmt->stmt_var_decl.type = type_name;
  stmt->stmt_var_decl.init = init_expr;

  return stmt;
}

// Declaration parsing functions

// Parser public functions

void parser_init(Parser *parser, CompilationUnit *comp_unit) {
  parser->comp_unit = comp_unit;
  lexer_init(&parser->lexer, comp_unit);

  parser->panic_mode = false;

  advance(parser);
}

AstExpr *parse_expression(Parser *parser) {
  return parse_precedence(parser, PREC_ASSIGNMENT);
}

AstStmt *parse_statement(Parser *parser) {
  switch (parser->next.type) {
  case TOKEN_VAR:
    return parse_variable_declaration(parser);
  default:
    return parse_expression_statement(parser);
  }
}

AstDecl *parse_declaration(Parser *parser) {
  // TODO
  return NULL;
}

// Parse rules and operator mappings

static const ParseRule rules[TOKEN_COUNT] = {
  [TOKEN_NUMBER] = {PREC_NONE, parse_number, NULL},
  [TOKEN_IDENT] = {PREC_NONE, parse_identifier, NULL},

  [TOKEN_PLUS] = {PREC_TERM, NULL, parse_binary},
  [TOKEN_MINUS] = {PREC_TERM, parse_unary, parse_binary},
  [TOKEN_STAR] = {PREC_FACTOR, NULL, parse_binary},
  [TOKEN_SLASH] = {PREC_FACTOR, NULL, parse_binary},

  [TOKEN_OPAREN] = {PREC_CALL, parse_grouping, NULL},
};

static const UnaryOp token_to_unary_op[TOKEN_COUNT] = {
  [TOKEN_MINUS] = UNARYOP_NEGATE,
};

static const BinaryOp token_to_binary_op[TOKEN_COUNT] = {
  [TOKEN_PLUS] = BINARYOP_ADD,
  [TOKEN_MINUS] = BINARYOP_SUB,
  [TOKEN_STAR] = BINARYOP_MUL,
  [TOKEN_SLASH] = BINARYOP_DIV,
};
