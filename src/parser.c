#include "ast.h"
#include "frontend.h"
#include "lib/array.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

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

static const ParseRule *get_rule(TokenKind kind) {
  if (kind < 0 || kind >= TOKEN_COUNT) return NULL;
  return &rules[kind];
}

static void error_at(Parser *parser, Token token, const char *message) {
  if (parser->panic_mode) return;

  parser->panic_mode = true;
  parser->comp_unit->had_error = true;

  fprintf(stderr, "[line %zu] Error", token.loc.line);

  if (token.kind == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token.kind == TOKEN_ERROR) {
    // Nothing to print
  } else {
    fprintf(stderr, " at '%.*s'", (int)token.lexeme_len, token.lexeme);
  }

  fprintf(stderr, ": %s\n", message);
}

static void advance(Parser *parser) {
  parser->current = parser->next;

  for (;;) {
    parser->next = lexer_get_token(&parser->lexer);

    if (parser->next.kind != TOKEN_ERROR) {
      break;
    }

    error_at(parser, parser->next, parser->next.lexeme);
    free((void *)parser->next.lexeme);
  }
}

static inline bool check(Parser *parser, TokenKind kind) {
  return parser->next.kind == kind;
}

static bool match(Parser *parser, TokenKind kind) {
  if (!check(parser, kind)) return false;
  advance(parser);
  return true;
}

static bool consume(Parser *parser, TokenKind kind, const char *message) {
  if (match(parser, kind)) return true;
  error_at(parser, parser->current, message);
  return false;
}

static const char *parse_identifier_name(Parser *parser, Token token) {
  if (token.kind != TOKEN_IDENT) {
    error_at(parser, token, "Expected identifier.");
    return NULL;
  }

  return st_intern_len(&parser->comp_unit->intern_table, token.lexeme, token.lexeme_len);
}

static inline bool is_lvalue(const AstExpr *node) {
  if (!node) return false;

  switch (node->kind) {
  case EXPR_IDENTIFIER:
    return true;
  default:
    return false;
  }
}

// Expression parsing functions

static AstExpr *parse_precedence(Parser *parser, Precedence precedence) {
  advance(parser);
  ParsePrefixFn prefix_rule = get_rule(parser->current.kind)->prefix;

  if (!prefix_rule) {
    // Handle error: no prefix rule for this token
    error_at(parser, parser->current, "Expected expression.");
    return NULL;
  }

  AstExpr *left = prefix_rule(parser);

  while (precedence <= get_rule(parser->next.kind)->precedence) {
    advance(parser);
    ParseInfixFn infix_rule = get_rule(parser->current.kind)->infix;

    if (!infix_rule) {
      // Handle error: no infix rule for this token
      return left;
    }

    left = infix_rule(parser, left);
  }

  return left;
}

static const UnaryOp get_unary_op(TokenKind kind) {
  switch (kind) {
  case TOKEN_MINUS:
    return UNOP_NEGATE;
  case TOKEN_BANG:
    return UNOP_NOT;

  default:
    error_at(NULL, (Token){.kind = kind}, "Invalid unary operator.");
    return -1;
  }
}

static const BinaryOp get_binary_op(TokenKind kind) {
  switch (kind) {
  case TOKEN_PLUS:
    return BINOP_ADD;
  case TOKEN_MINUS:
    return BINOP_SUB;
  case TOKEN_STAR:
    return BINOP_MUL;
  case TOKEN_SLASH:
    return BINOP_DIV;

  case TOKEN_EQEQ:
    return BINOP_EQEQ;
  case TOKEN_NOT_EQ:
    return BINOP_NOT_EQ;
  case TOKEN_LESS:
    return BINOP_LESS;
  case TOKEN_GREATER:
    return BINOP_GREATER;
  case TOKEN_LESS_EQ:
    return BINOP_LESS_EQ;
  case TOKEN_GREATER_EQ:
    return BINOP_GREATER_EQ;

  default:
    error_at(NULL, (Token){.kind = kind}, "Invalid binary operator.");
    return -1;
  }
}

static AstExpr *parse_int_literal(Parser *parser) {
  uint8_t base = 10;

  Token token = parser->current;
  const char *start = token.lexeme;
  const char *end = start + token.lexeme_len;

  if (token.lexeme_len > 2 && start[0] == '0') {
    if (start[1] == 'x' || start[1] == 'X') {
      base = 16;
      start += 2;
    } else if (start[1] == 'b' || start[1] == 'B') {
      base = 2;
      start += 2;
    }
  }

  uint64_t value = 0;

  for (const char *current = start; current < end; current++) {
    char c = *current;
    if (c == '_') {
      continue;
    }

    int8_t digit = -1;

    if (c >= '0' && c <= '9') {
      digit = c - '0';
    } else if (c >= 'a' && c <= 'f') {
      digit = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
      digit = c - 'A' + 10;
    }

    if (digit < 0 || digit >= base) {
      error_at(parser, token, "Invalid character in integer literal.");
      return NULL;
    }

    value = value * base + digit;
  }

  AstExpr *expr = ast_expr_new(&parser->comp_unit->arena, EXPR_LITERAL, token.loc);
  expr->literal.type = LITERAL_INT;
  expr->literal.int_value = value;

  return expr;
}

static AstExpr *parse_float_literal(Parser *parser) {
  AstExpr *expr = ast_expr_new(&parser->comp_unit->arena, EXPR_LITERAL, parser->current.loc);
  expr->literal.type = LITERAL_FLOAT;
  expr->literal.float_value = strtod(parser->current.lexeme, NULL);

  return expr;
}

static AstExpr *parse_identifier(Parser *parser) {
  const char *identifier_name = parse_identifier_name(parser, parser->current);
  if (!identifier_name) return NULL;

  AstExpr *expr = ast_expr_new(&parser->comp_unit->arena, EXPR_IDENTIFIER, parser->current.loc);
  expr->identifier.name = identifier_name;

  return expr;
}

static AstExpr *parse_grouping(Parser *parser) {
  AstExpr *expr = parse_expression(parser);

  consume(parser, TOKEN_CPAREN, "Expected ')' after expression");

  return expr;
}

static AstExpr *parse_assignment(Parser *parser, AstExpr *left) {
  if (!is_lvalue(left)) {
    error_at(parser, parser->current, "Invalid assignment target.");
    return NULL;
  }
  SourceLoc start_loc = left->loc;

  AstExpr *right = parse_precedence(parser, PREC_ASSIGNMENT);

  AstExpr *assign_expr = ast_expr_new(&parser->comp_unit->arena, EXPR_ASSIGN, start_loc);
  assign_expr->assignment.target = left;
  assign_expr->assignment.value = right;

  return assign_expr;
}

static AstExpr *parse_unary(Parser *parser) {
  SourceLoc start_loc = parser->current.loc;
  TokenKind operator = parser->current.kind;

  AstExpr *op_expr = parse_precedence(parser, PREC_UNARY);

  AstExpr *unary_expr = ast_expr_new(&parser->comp_unit->arena, EXPR_UNARY, start_loc);
  unary_expr->expr_unary.op = get_unary_op(operator);
  unary_expr->expr_unary.operand = op_expr;

  return unary_expr;
}

static AstExpr *parse_binary(Parser *parser, AstExpr *left) {
  SourceLoc start_loc = parser->current.loc;
  TokenKind operator = parser->current.kind;

  const ParseRule *rule = get_rule(operator);
  AstExpr *right = parse_precedence(parser, rule->precedence + 1);

  AstExpr *binary_expr = ast_expr_new(&parser->comp_unit->arena, EXPR_BINARY, start_loc);
  binary_expr->expr_binary.op = get_binary_op(operator);
  binary_expr->expr_binary.left = left;
  binary_expr->expr_binary.right = right;

  return binary_expr;
}

// Statement parsing functions

static AstStmt *parse_block(Parser *parser) {
  SourceLoc block_start = parser->current.loc;

  consume(parser, TOKEN_OBRACE, "Expected '{' to start block.");

  AstStmt **stmts = NULL;

  while (!check(parser, TOKEN_CBRACE) && !check(parser, TOKEN_EOF)) {
    AstStmt *stmt = parse_statement(parser);
    if (stmt) {
      da_append(stmts, stmt);
    }
  }

  consume(parser, TOKEN_CBRACE, "Expected '}' to end block.");

  AstStmt *block_stmt = ast_stmt_new(&parser->comp_unit->arena, STMT_BLOCK, block_start);
  AstStmtBlock *block = &block_stmt->stmt_block;

  block->count = da_len(stmts);
  block->statements = arena_alloc(&parser->comp_unit->arena, sizeof(AstStmt *) * block->count);
  memcpy(block->statements, stmts, sizeof(AstStmt *) * block->count);
  da_free(stmts);

  return block_stmt;
}

static AstStmt *parse_expression_statement(Parser *parser) {
  SourceLoc start_loc = parser->current.loc;

  AstExpr *expr = parse_expression(parser);

  consume(parser, TOKEN_SEMICOLON, "Expected ';' after expression");

  AstStmt *stmt = ast_stmt_new(&parser->comp_unit->arena, STMT_EXPRESSION, start_loc);
  stmt->stmt_expr.expression = expr;

  return stmt;
}

static AstStmt *parse_variable_declaration(Parser *parser) {
  consume(parser, TOKEN_VAR, "Expected 'var' keyword for variable declaration.");
  consume(parser, TOKEN_IDENT, "Expected variable name after 'var'.");

  Token name_token = parser->current;

  AstType *type_spec = NULL;
  if (match(parser, TOKEN_COLON)) {
    type_spec = parse_type(parser);
    if (!type_spec) {
      error_at(parser, parser->current, "Expected type after ':' in variable declaration.");
    }
  }

  AstExpr *init_expr = NULL;
  if (match(parser, TOKEN_EQUAL)) {
    init_expr = parse_expression(parser);
  }

  consume(parser, TOKEN_SEMICOLON, "Expected ';' after variable declaration.");

  if (!type_spec && !init_expr) {
    error_at(parser, name_token, "Variable declaration must have either a type annotation or an initializer.");
    return NULL;
  }

  const char *var_name = parse_identifier_name(parser, name_token);
  if (!var_name) return NULL;

  AstStmt *stmt = ast_stmt_new(&parser->comp_unit->arena, STMT_VAR_DECL, name_token.loc);
  stmt->stmt_var_decl.name = var_name;
  stmt->stmt_var_decl.type = type_spec;
  stmt->stmt_var_decl.init = init_expr;

  return stmt;
}

static AstStmt *parse_break_statement(Parser *parser) {
  SourceLoc start_loc = parser->current.loc;

  consume(parser, TOKEN_BREAK, "Expected 'break' keyword for break statement.");
  consume(parser, TOKEN_SEMICOLON, "Expected ';' after 'break' statement.");

  AstStmt *stmt = ast_stmt_new(&parser->comp_unit->arena, STMT_BREAK, start_loc);
  return stmt;
}

static AstStmt *parse_continue_statement(Parser *parser) {
  SourceLoc start_loc = parser->current.loc;

  consume(parser, TOKEN_CONTINUE, "Expected 'continue' keyword for continue statement.");
  consume(parser, TOKEN_SEMICOLON, "Expected ';' after 'continue' statement.");

  AstStmt *stmt = ast_stmt_new(&parser->comp_unit->arena, STMT_CONTINUE, start_loc);
  return stmt;
}

static AstStmt *parse_if_statement(Parser *parser) {
  SourceLoc start_loc = parser->current.loc;

  consume(parser, TOKEN_IF, "Expected 'if' keyword for if statement.");

  AstExpr *condition = parse_expression(parser);

  if (!check(parser, TOKEN_OBRACE)) {
    error_at(parser, parser->current, "Expected '{' after 'if' condition.");
  }

  AstStmt *then_branch = parse_block(parser);

  AstStmt *else_branch = NULL;
  if (match(parser, TOKEN_ELSE)) {
    else_branch = parse_statement(parser);
  }

  AstStmt *stmt = ast_stmt_new(&parser->comp_unit->arena, STMT_IF, start_loc);
  stmt->stmt_if.condition = condition;
  stmt->stmt_if.then_branch = then_branch;
  stmt->stmt_if.else_branch = else_branch;

  return stmt;
}

static AstStmt *parse_while_statement(Parser *parser) {
  SourceLoc start_loc = parser->current.loc;

  consume(parser, TOKEN_WHILE, "Expected 'while' keyword for while statement.");

  AstExpr *condition = parse_expression(parser);

  if (!check(parser, TOKEN_OBRACE)) {
    error_at(parser, parser->current, "Expected '{' after 'while' condition.");
  }

  AstStmt *body = parse_block(parser);

  AstStmt *stmt = ast_stmt_new(&parser->comp_unit->arena, STMT_WHILE, start_loc);
  stmt->stmt_while.condition = condition;
  stmt->stmt_while.body = body;

  return stmt;
}

// Declaration parsing functions

// Type parsing functions

static AstType *parse_named_type(Parser *parser) {
  SourceLoc start_loc = parser->current.loc;

  consume(parser, TOKEN_IDENT, "Expected type name for named type.");

  const char *type_name = parse_identifier_name(parser, parser->current);
  if (!type_name) return NULL;

  AstType *type = ast_type_new(&parser->comp_unit->arena, TYPE_NAMED, start_loc);
  type->named = type_name;

  return type;
}

static AstType *parse_pointer_type(Parser *parser) {
  SourceLoc start_loc = parser->current.loc;

  consume(parser, TOKEN_STAR, "Expected '*' for pointer type.");

  AstType *base_type = parse_type(parser);
  if (!base_type) return NULL;

  AstType *type = ast_type_new(&parser->comp_unit->arena, TYPE_POINTER, start_loc);
  type->pointer.base = base_type;

  return type;
}

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
  switch (parser->next.kind) {
  case TOKEN_OBRACE:
    return parse_block(parser);
  case TOKEN_VAR:
    return parse_variable_declaration(parser);

  case TOKEN_BREAK:
    return parse_break_statement(parser);
  case TOKEN_CONTINUE:
    return parse_continue_statement(parser);

  case TOKEN_IF:
    return parse_if_statement(parser);
  case TOKEN_WHILE:
    return parse_while_statement(parser);

  default:
    return parse_expression_statement(parser);
  }
}

AstDecl *parse_declaration(Parser *parser) {
  // TODO
  return NULL;
}

AstType *parse_type(Parser *parser) {
  Token token = parser->next;

  switch (token.kind) {
  case TOKEN_IDENT:
    return parse_named_type(parser);
  case TOKEN_STAR:
    return parse_pointer_type(parser);

  default:
    return NULL;
  }

  return NULL;
}

// Parse rules and operator mappings

static const ParseRule rules[TOKEN_COUNT] = {
  [TOKEN_IDENT] = {PREC_NONE, parse_identifier, NULL},
  [TOKEN_INT] = {PREC_NONE, parse_int_literal, NULL},
  [TOKEN_FLOAT] = {PREC_NONE, parse_float_literal, NULL},

  [TOKEN_PLUS] = {PREC_TERM, NULL, parse_binary},
  [TOKEN_MINUS] = {PREC_TERM, parse_unary, parse_binary},
  [TOKEN_STAR] = {PREC_FACTOR, NULL, parse_binary},
  [TOKEN_SLASH] = {PREC_FACTOR, NULL, parse_binary},

  [TOKEN_LESS] = {PREC_TERM, NULL, parse_binary},
  [TOKEN_GREATER] = {PREC_TERM, NULL, parse_binary},
  [TOKEN_NOT_EQ] = {PREC_TERM, NULL, parse_binary},
  [TOKEN_EQEQ] = {PREC_TERM, NULL, parse_binary},
  [TOKEN_LESS_EQ] = {PREC_TERM, NULL, parse_binary},
  [TOKEN_GREATER_EQ] = {PREC_TERM, NULL, parse_binary},

  [TOKEN_EQUAL] = {PREC_ASSIGNMENT, NULL, parse_assignment},
  [TOKEN_BANG] = {PREC_NONE, parse_unary, NULL},

  [TOKEN_OPAREN] = {PREC_CALL, parse_grouping, NULL},
};
