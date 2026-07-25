#include "parser.h"
#include "ast.h"

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

typedef AstNode *(*ParsePrefixFn)(Parser *parser);
typedef AstNode *(*ParseInfixFn)(Parser *parser, AstNode *left);

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

static void advance(Parser *parser) {
  parser->current = parser->next;
  parser->next = lexer_get_token(parser->lexer);
}

static AstNode *parse_precedence(Parser *parser, Precedence precedence) {
  advance(parser);
  ParsePrefixFn prefix_rule = get_rule(parser->current.type)->prefix;

  if (!prefix_rule) {
    // Handle error: no prefix rule for this token
    return NULL;
  }

  AstNode *left = prefix_rule(parser);

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

static AstNode *parse_number(Parser *parser) {
  AstNode *node = ast_node_new(AST_LITERAL);
  node->literal.value = strtoll(parser->current.lexeme, NULL, 10);
  return node;
}

static AstNode *parse_identifier(Parser *parser) {
  AstNode *node = ast_node_new(AST_IDENTIFIER);

  size_t length = parser->current.lexeme_len;
  node->identifier.name = strndup(parser->current.lexeme, length);
  return node;
}

static AstNode *parse_unary(Parser *parser) {
  TokenType operator = parser->current.type;
  const ParseRule *rule = get_rule(operator);

  AstNode *operand = parse_precedence(parser, PREC_UNARY);

  AstNode *unary_expr = ast_node_new(AST_UNARY_OP);
  unary_expr->expr_unary.op = get_unary_op(operator);
  unary_expr->expr_unary.operand = operand;

  return unary_expr;
}

static AstNode *parse_binary(Parser *parser, AstNode *left) {
  TokenType operator = parser->current.type;
  const ParseRule *rule = get_rule(operator);

  AstNode *right = parse_precedence(parser, rule->precedence + 1);

  AstNode *binary_expr = ast_node_new(AST_BINARY_OP);
  binary_expr->expr_binary.op = get_binary_op(operator);
  binary_expr->expr_binary.left = left;
  binary_expr->expr_binary.right = right;

  return binary_expr;
}

void parser_init(Parser *parser, Lexer *lexer) {
  parser->lexer = lexer;
  parser->had_error = false;
  parser->panic_mode = false;

  advance(parser);
}

AstNode *parse_expression(Parser *parser) {
  return parse_precedence(parser, PREC_ASSIGNMENT);
}

static const ParseRule rules[TOKEN_COUNT] = {
  [TOKEN_NUMBER] = {PREC_NONE, parse_number, NULL},
  [TOKEN_IDENT] = {PREC_NONE, parse_identifier, NULL},

  [TOKEN_PLUS] = {PREC_TERM, NULL, parse_binary},
  [TOKEN_MINUS] = {PREC_TERM, parse_unary, parse_binary},
  [TOKEN_STAR] = {PREC_FACTOR, NULL, parse_binary},
  [TOKEN_SLASH] = {PREC_FACTOR, NULL, parse_binary},
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