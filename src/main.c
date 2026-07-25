#include "debug.h"
#include "lexer.h"
#include "parser.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  char *source = "(1+2)*3";

  Lexer lexer;
  lexer_init(&lexer, source, strlen(source));

  Parser parser;
  parser_init(&parser, &lexer);

  AstNode *expr = parse_expression(&parser);

  debug_print_ast(expr);

  debug_print_ast_sexpr(expr);
  printf("\n");

  printf("Parser had error: %s\n", parser.had_error ? "true" : "false");

  // while (1) {
  //   Token token = lexer_get_token(&lexer);
  //   debug_print_token(token);

  //   if (token.type == TOKEN_EOF) {
  //     break;
  //   }
  // }
}