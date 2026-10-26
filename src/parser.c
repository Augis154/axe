#include "parser.h"

#include "ast.h"
#include "common.h"
#include "lib/array.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef enum {
  PREC_NONE = 0,
  PREC_ASSIGNMENT, // =
  PREC_EQUALITY,   // == !=
  PREC_COMPARISON, // < > <= >=
  PREC_TERM,       // + -
  PREC_FACTOR,     // * /
  PREC_CAST,       // as
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

// Reports a parse error at `token` and enters panic mode; further errors are suppressed until synchronize.
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

// Moves to the next token, reporting and skipping lexer error tokens.
static void advance(Parser *parser) {
  parser->previous = parser->current;

  for (;;) {
    parser->current = lexer_get_token(&parser->lexer);

    if (parser->current.kind != TOKEN_ERROR) {
      break;
    }

    error_at(parser, parser->current, parser->current.lexeme);
    free((void *)parser->current.lexeme);
  }
}

static inline bool check(Parser *parser, TokenKind kind) {
  return parser->current.kind == kind;
}

static bool match(Parser *parser, TokenKind kind) {
  if (!check(parser, kind)) return false;
  advance(parser);
  return true;
}

static bool consume(Parser *parser, TokenKind kind, const char *message) {
  if (match(parser, kind)) return true;
  error_at(parser, parser->previous, message);
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
  case AST_EXPR_IDENTIFIER:
    return true;
  default:
    return false;
  }
}

// Error recovery

// Skips to a likely statement boundary (after ';' or before a statement keyword or '}') and leaves panic mode.
static void synchronize(Parser *parser) {
  parser->panic_mode = false;

  while (!check(parser, TOKEN_EOF)) {
    if (parser->previous.kind == TOKEN_SEMICOLON) return;

    switch (parser->current.kind) {
    case TOKEN_RBRACE:
    case TOKEN_FN:
    case TOKEN_VAR:
    case TOKEN_RETURN:
    case TOKEN_BREAK:
    case TOKEN_CONTINUE:
    case TOKEN_IF:
    case TOKEN_WHILE:
      return;

    default:
      break;
    }

    advance(parser);
  }
}

// Skips to the next top-level 'fn' and leaves panic mode.
static void synchronize_top_level(Parser *parser) {
  parser->panic_mode = false;

  while (!check(parser, TOKEN_EOF) && !check(parser, TOKEN_FN)) {
    advance(parser);
  }
}

// Expression parsing functions

// expression := prefix { infix expression }  -- keeps parsing infix operators of at least `precedence`.
static AstExpr *parse_precedence(Parser *parser, Precedence precedence) {
  advance(parser);
  ParsePrefixFn prefix_rule = get_rule(parser->previous.kind)->prefix;

  if (!prefix_rule) {
    // Handle error: no prefix rule for this token
    error_at(parser, parser->previous, "Expected expression.");
    return NULL;
  }

  AstExpr *left = prefix_rule(parser);

  while (precedence <= get_rule(parser->current.kind)->precedence) {
    advance(parser);
    ParseInfixFn infix_rule = get_rule(parser->previous.kind)->infix;

    if (!infix_rule) {
      // Handle error: no infix rule for this token
      return left;
    }

    left = infix_rule(parser, left);
  }

  return left;
}

static UnaryOp get_unary_op(TokenKind kind) {
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

static BinaryOp get_binary_op(TokenKind kind) {
  switch (kind) {
  case TOKEN_PLUS:
    return BINOP_ADD;
  case TOKEN_MINUS:
    return BINOP_SUB;
  case TOKEN_STAR:
    return BINOP_MUL;
  case TOKEN_SLASH:
    return BINOP_DIV;

  case TOKEN_EQ:
    return BINOP_EQ;
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

// INT  -- decimal, 0xHEX or 0bBINARY digits, with optional '_' separators.
static AstExpr *parse_int_literal(Parser *parser) {
  uint8_t base = 10;

  Token token = parser->previous;
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

  AstExpr *expr = ast_expr_new(&parser->comp_unit->arena, AST_EXPR_LITERAL, token.loc);
  expr->expr_literal.kind = AST_LITERAL_INT;
  expr->expr_literal.int_value = value;

  return expr;
}

// FLOAT  -- digits '.' digits, with an optional 'f' suffix.
static AstExpr *parse_float_literal(Parser *parser) {
  AstExpr *expr = ast_expr_new(&parser->comp_unit->arena, AST_EXPR_LITERAL, parser->previous.loc);
  expr->expr_literal.kind = AST_LITERAL_FLOAT;
  expr->expr_literal.float_value = strtod(parser->previous.lexeme, NULL);

  return expr;
}

// IDENT
static AstExpr *parse_identifier(Parser *parser) {
  const char *identifier_name = parse_identifier_name(parser, parser->previous);
  if (!identifier_name) return NULL;

  AstExpr *expr = ast_expr_new(&parser->comp_unit->arena, AST_EXPR_IDENTIFIER, parser->previous.loc);
  expr->expr_identifier.name = identifier_name;

  return expr;
}

// '(' expression ')'  -- the '(' is already consumed.
static AstExpr *parse_grouping(Parser *parser) {
  AstExpr *expr = parse_expression(parser);

  consume(parser, TOKEN_RPAREN, "Expected ')' after expression");

  return expr;
}

// target '=' expression  -- target must be an identifier; right-associative.
static AstExpr *parse_assignment(Parser *parser, AstExpr *left) {
  if (!is_lvalue(left)) {
    error_at(parser, parser->previous, "Invalid assignment target.");
    return NULL;
  }
  SourceLoc start_loc = left->loc;

  AstExpr *right = parse_precedence(parser, PREC_ASSIGNMENT);
  if (!right) return NULL;

  AstExpr *assign_expr = ast_expr_new(&parser->comp_unit->arena, AST_EXPR_ASSIGN, start_loc);
  assign_expr->expr_assign.target = left;
  assign_expr->expr_assign.value = right;

  return assign_expr;
}

// callee '(' [ expression { ',' expression } [ ',' ] ] ')'  -- the '(' is already consumed.
static AstExpr *parse_call(Parser *parser, AstExpr *left) {
  if (!left) return NULL;

  AstExpr **args = NULL;
  while (!check(parser, TOKEN_RPAREN) && !check(parser, TOKEN_EOF)) {
    AstExpr *arg = parse_expression(parser);
    if (arg) {
      da_append(args, arg);
    }

    if (!match(parser, TOKEN_COMMA)) {
      break;
    }
  }

  consume(parser, TOKEN_RPAREN, "Expected ')' after function arguments.");

  AstExpr *call_expr = ast_expr_new(&parser->comp_unit->arena, AST_EXPR_CALL, left->loc);
  call_expr->expr_call.callee = left;

  AstExpr **arg_array = arena_copy_array(&parser->comp_unit->arena, args, da_len(args), sizeof(AstExpr *));

  call_expr->expr_call.arguments = arg_array;
  call_expr->expr_call.argument_count = da_len(args);

  da_free(args);

  return call_expr;
}

// expression 'as' type  -- the 'as' is already consumed.
static AstExpr *parse_cast(Parser *parser, AstExpr *left) {
  if (!left) return NULL;
  SourceLoc start_loc = parser->previous.loc;

  AstType *type = parse_type(parser);
  if (!type) {
    error_at(parser, parser->current, "Expected type after 'as'.");
    return NULL;
  }

  AstExpr *cast_expr = ast_expr_new(&parser->comp_unit->arena, AST_EXPR_CAST, start_loc);
  cast_expr->expr_cast.operand = left;
  cast_expr->expr_cast.target = type;

  return cast_expr;
}

// ( '-' | '!' ) expression
static AstExpr *parse_unary(Parser *parser) {
  SourceLoc start_loc = parser->previous.loc;
  TokenKind operator = parser->previous.kind;

  AstExpr *op_expr = parse_precedence(parser, PREC_UNARY);
  if (!op_expr) return NULL;

  AstExpr *unary_expr = ast_expr_new(&parser->comp_unit->arena, AST_EXPR_UNARY, start_loc);
  unary_expr->expr_unary.op = get_unary_op(operator);
  unary_expr->expr_unary.operand = op_expr;

  return unary_expr;
}

// left ( '+' | '-' | '*' | '/' | '==' | '!=' | '<' | '>' | '<=' | '>=' ) right  -- left-associative.
static AstExpr *parse_binary(Parser *parser, AstExpr *left) {
  SourceLoc start_loc = parser->previous.loc;
  TokenKind operator = parser->previous.kind;

  const ParseRule *rule = get_rule(operator);
  AstExpr *right = parse_precedence(parser, rule->precedence + 1);
  if (!left || !right) return NULL;

  AstExpr *binary_expr = ast_expr_new(&parser->comp_unit->arena, AST_EXPR_BINARY, start_loc);
  binary_expr->expr_binary.op = get_binary_op(operator);
  binary_expr->expr_binary.left = left;
  binary_expr->expr_binary.right = right;

  return binary_expr;
}

// Statement parsing functions

// '{' { statement } '}'
static AstStmt *parse_block(Parser *parser) {
  SourceLoc block_start = parser->current.loc;

  consume(parser, TOKEN_LBRACE, "Expected '{' to start block.");

  AstStmt **stmts = NULL;

  while (!check(parser, TOKEN_RBRACE) && !check(parser, TOKEN_EOF)) {
    AstStmt *stmt = parse_statement(parser);
    if (stmt) {
      da_append(stmts, stmt);
    }

    if (parser->panic_mode) {
      synchronize(parser);
    }
  }

  consume(parser, TOKEN_RBRACE, "Expected '}' to end block.");

  AstStmt *block_stmt = ast_stmt_new(&parser->comp_unit->arena, AST_STMT_BLOCK, block_start);

  AstStmt **stmt_array = arena_copy_array(&parser->comp_unit->arena, stmts, da_len(stmts), sizeof(AstStmt *));

  block_stmt->stmt_block.statements = stmt_array;
  block_stmt->stmt_block.statement_count = da_len(stmts);
  da_free(stmts);

  return block_stmt;
}

// expression ';'
static AstStmt *parse_expression_statement(Parser *parser) {
  SourceLoc start_loc = parser->current.loc;

  AstExpr *expr = parse_expression(parser);

  consume(parser, TOKEN_SEMICOLON, "Expected ';' after expression");

  AstStmt *stmt = ast_stmt_new(&parser->comp_unit->arena, AST_STMT_EXPRESSION, start_loc);
  stmt->stmt_expression.expression = expr;

  return stmt;
}

// 'var' IDENT [ ':' type ] [ '=' expression ] ';'  -- needs a type or an initializer.
static AstStmt *parse_variable_declaration(Parser *parser) {
  consume(parser, TOKEN_VAR, "Expected 'var' keyword for variable declaration.");
  consume(parser, TOKEN_IDENT, "Expected variable name after 'var'.");

  Token name_token = parser->previous;

  AstType *type_spec = NULL;
  if (match(parser, TOKEN_COLON)) {
    type_spec = parse_type(parser);
    if (!type_spec) {
      error_at(parser, parser->previous, "Expected type after ':' in variable declaration.");
    }
  }

  AstExpr *init_expr = NULL;
  if (match(parser, TOKEN_ASSIGN)) {
    init_expr = parse_expression(parser);
  }

  consume(parser, TOKEN_SEMICOLON, "Expected ';' after variable declaration.");

  if (!type_spec && !init_expr) {
    error_at(parser, name_token, "Variable declaration must have either a type annotation or an initializer.");
    return NULL;
  }

  const char *var_name = parse_identifier_name(parser, name_token);
  if (!var_name) return NULL;

  AstStmt *stmt = ast_stmt_new(&parser->comp_unit->arena, AST_STMT_LOCAL_VAR, name_token.loc);
  stmt->stmt_local_var.name = var_name;
  stmt->stmt_local_var.declared_type = type_spec;
  stmt->stmt_local_var.init = init_expr;

  return stmt;
}

// 'return' [ expression ] ';'
static AstStmt *parse_return_statement(Parser *parser) {
  SourceLoc start_loc = parser->current.loc;

  consume(parser, TOKEN_RETURN, "Expected 'return' keyword for return statement.");

  AstExpr *return_expr = NULL;
  if (!check(parser, TOKEN_SEMICOLON)) {
    return_expr = parse_expression(parser);
  }

  consume(parser, TOKEN_SEMICOLON, "Expected ';' after return statement.");

  AstStmt *stmt = ast_stmt_new(&parser->comp_unit->arena, AST_STMT_RETURN, start_loc);
  stmt->stmt_return.value = return_expr;

  return stmt;
}

// 'break' ';'
static AstStmt *parse_break_statement(Parser *parser) {
  SourceLoc start_loc = parser->current.loc;

  consume(parser, TOKEN_BREAK, "Expected 'break' keyword for break statement.");
  consume(parser, TOKEN_SEMICOLON, "Expected ';' after 'break' statement.");

  AstStmt *stmt = ast_stmt_new(&parser->comp_unit->arena, AST_STMT_BREAK, start_loc);
  return stmt;
}

// 'continue' ';'
static AstStmt *parse_continue_statement(Parser *parser) {
  SourceLoc start_loc = parser->current.loc;

  consume(parser, TOKEN_CONTINUE, "Expected 'continue' keyword for continue statement.");
  consume(parser, TOKEN_SEMICOLON, "Expected ';' after 'continue' statement.");

  AstStmt *stmt = ast_stmt_new(&parser->comp_unit->arena, AST_STMT_CONTINUE, start_loc);
  return stmt;
}

// 'if' expression block [ 'else' statement ]  -- the else branch is usually a block or another if.
static AstStmt *parse_if_statement(Parser *parser) {
  SourceLoc start_loc = parser->current.loc;

  consume(parser, TOKEN_IF, "Expected 'if' keyword for if statement.");

  AstExpr *condition = parse_expression(parser);

  if (!check(parser, TOKEN_LBRACE)) {
    error_at(parser, parser->previous, "Expected '{' after 'if' condition.");
  }

  AstStmt *then_branch = parse_block(parser);

  AstStmt *else_branch = NULL;
  if (match(parser, TOKEN_ELSE)) {
    else_branch = parse_statement(parser);
  }

  AstStmt *stmt = ast_stmt_new(&parser->comp_unit->arena, AST_STMT_IF, start_loc);
  stmt->stmt_if.condition = condition;
  stmt->stmt_if.then_branch = then_branch;
  stmt->stmt_if.else_branch = else_branch;

  return stmt;
}

// 'while' expression block
static AstStmt *parse_while_statement(Parser *parser) {
  SourceLoc start_loc = parser->current.loc;

  consume(parser, TOKEN_WHILE, "Expected 'while' keyword for while statement.");

  AstExpr *condition = parse_expression(parser);

  if (!check(parser, TOKEN_LBRACE)) {
    error_at(parser, parser->previous, "Expected '{' after 'while' condition.");
  }

  AstStmt *body = parse_block(parser);

  AstStmt *stmt = ast_stmt_new(&parser->comp_unit->arena, AST_STMT_WHILE, start_loc);
  stmt->stmt_while.condition = condition;
  stmt->stmt_while.body = body;

  return stmt;
}

// Declaration parsing functions

// 'fn' IDENT '(' [ IDENT ':' type { ',' IDENT ':' type } [ ',' ] ] ')' [ ':' type ] block
static AstDecl *parse_function_declaration(Parser *parser) {
  SourceLoc start_loc = parser->current.loc;

  consume(parser, TOKEN_FN, "Expected 'fn' keyword for function declaration.");
  consume(parser, TOKEN_IDENT, "Expected function name after 'fn'.");

  const char *func_name = parse_identifier_name(parser, parser->previous);
  if (!func_name) return NULL;

  consume(parser, TOKEN_LPAREN, "Expected '(' after function name.");

  AstParam *params = NULL;

  while (!check(parser, TOKEN_RPAREN) && !check(parser, TOKEN_EOF)) {
    SourceLoc param_loc = parser->current.loc;

    consume(parser, TOKEN_IDENT, "Expected parameter name in function declaration.");
    const char *param_name = parse_identifier_name(parser, parser->previous);
    if (!param_name) return NULL;

    consume(parser, TOKEN_COLON, "Expected ':' after parameter name.");
    AstType *param_type = parse_type(parser);
    if (!param_type) {
      error_at(parser, parser->previous, "Expected type for parameter.");
      return NULL;
    }

    AstParam param = {
      .name = param_name,
      .declared_type = param_type,
      .loc = param_loc,
    };
    da_append(params, param);

    if (!match(parser, TOKEN_COMMA)) {
      break;
    }
  }

  consume(parser, TOKEN_RPAREN, "Expected ')' after function parameters.");

  AstType *return_type = NULL;
  if (match(parser, TOKEN_COLON)) {
    return_type = parse_type(parser);
    if (!return_type) {
      error_at(parser, parser->previous, "Expected return type after ':' in function declaration.");
    }
  }

  AstStmt *func_body = parse_block(parser);
  if (!func_body) return NULL;

  AstDecl *decl = ast_decl_new(&parser->comp_unit->arena, AST_DECL_FUNCTION, start_loc);
  decl->decl_function.name = func_name;
  decl->decl_function.body = func_body;
  decl->decl_function.declared_return_type = return_type;

  AstParam *param_array = arena_copy_array(&parser->comp_unit->arena, params, da_len(params), sizeof(AstParam));

  decl->decl_function.params = param_array;
  decl->decl_function.param_count = da_len(params);
  da_free(params);

  return decl;
}

// Type parsing functions

// IDENT  -- a named type such as i32.
static AstType *parse_named_type(Parser *parser) {
  SourceLoc start_loc = parser->current.loc;

  consume(parser, TOKEN_IDENT, "Expected type name for named type.");

  const char *type_name = parse_identifier_name(parser, parser->previous);
  if (!type_name) return NULL;

  AstType *type = ast_type_new(&parser->comp_unit->arena, AST_TYPE_NAMED, start_loc);
  type->named = type_name;

  return type;
}

// '*' type
static AstType *parse_pointer_type(Parser *parser) {
  SourceLoc start_loc = parser->current.loc;

  consume(parser, TOKEN_STAR, "Expected '*' for pointer type.");

  AstType *base_type = parse_type(parser);
  if (!base_type) return NULL;

  AstType *type = ast_type_new(&parser->comp_unit->arena, AST_TYPE_POINTER, start_loc);
  type->pointer.base = base_type;

  return type;
}

// Parser public functions

// Sets up the lexer and reads the first token into the lookahead.
void parser_init(Parser *parser, CompilationUnit *comp_unit) {
  parser->comp_unit = comp_unit;
  lexer_init(&parser->lexer, comp_unit);

  parser->panic_mode = false;

  advance(parser);
}

// expression := assignment  -- the lowest precedence level.
AstExpr *parse_expression(Parser *parser) {
  return parse_precedence(parser, PREC_ASSIGNMENT);
}

// statement := block | variable_declaration | return | break | continue | if | while | expression_statement
AstStmt *parse_statement(Parser *parser) {
  switch (parser->current.kind) {
  case TOKEN_LBRACE:
    return parse_block(parser);
  case TOKEN_VAR:
    return parse_variable_declaration(parser);
  case TOKEN_FN:
    error_at(parser, parser->current, "Nested functions are not supported.");
    parse_function_declaration(parser);
    return NULL;

  case TOKEN_RETURN:
    return parse_return_statement(parser);
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

// declaration := function_declaration  -- recovers to the next 'fn' on anything else.
AstDecl *parse_declaration(Parser *parser) {
  switch (parser->current.kind) {
  case TOKEN_FN:
    return parse_function_declaration(parser);

  default:
    error_at(parser, parser->current, "Expected declaration.");
    synchronize_top_level(parser);
  }

  return NULL;
}

// type := IDENT | '*' type
AstType *parse_type(Parser *parser) {
  Token token = parser->current;

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

// program := { declaration } EOF
AstProgram *parse_program(Parser *parser) {
  AstDecl **decls = NULL;

  while (!check(parser, TOKEN_EOF)) {
    AstDecl *decl = parse_declaration(parser);
    if (decl) {
      da_append(decls, decl);
    }

    if (parser->panic_mode) {
      synchronize_top_level(parser);
    }
  }

  AstProgram *program = arena_alloc(&parser->comp_unit->arena, sizeof(AstProgram));

  AstDecl **decl_array = arena_copy_array(&parser->comp_unit->arena, decls, da_len(decls), sizeof(AstDecl *));

  program->decls = decl_array;
  program->decl_count = da_len(decls);

  da_free(decls);

  return program;
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

  [TOKEN_LESS] = {PREC_COMPARISON, NULL, parse_binary},
  [TOKEN_GREATER] = {PREC_COMPARISON, NULL, parse_binary},
  [TOKEN_LESS_EQ] = {PREC_COMPARISON, NULL, parse_binary},
  [TOKEN_GREATER_EQ] = {PREC_COMPARISON, NULL, parse_binary},
  [TOKEN_EQ] = {PREC_EQUALITY, NULL, parse_binary},
  [TOKEN_NOT_EQ] = {PREC_EQUALITY, NULL, parse_binary},
  [TOKEN_BANG] = {PREC_NONE, parse_unary, NULL},

  [TOKEN_ASSIGN] = {PREC_ASSIGNMENT, NULL, parse_assignment},
  [TOKEN_AS] = {PREC_CAST, NULL, parse_cast},

  [TOKEN_LPAREN] = {PREC_CALL, parse_grouping, parse_call},
};
