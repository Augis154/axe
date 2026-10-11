#include "ast.h"
#include "backend.h"
#include "debug.h"
#include "frontend.h"
#include "ir.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  char *source = "(1+2)*3--4";

  Lexer lexer;
  lexer_init(&lexer, source, strlen(source));

  Arena ast_arena = {0};

  Parser parser;
  parser_init(&parser, &lexer, &ast_arena);

  AstExpr *root = parse_expression(&parser);

  // debug_print_ast_tree(&ast_arena, root);

  debug_print_ast_sexpr(&ast_arena, root);
  printf("\n");

  printf("Parser had error: %s\n", parser.had_error ? "true" : "false");

  if (parser.had_error) {
    ast_arena_free(&ast_arena);
    return 1;
  }

  IrContext ir_context;
  ir_gen_init(&ir_context, &ast_arena);

  emit_ir(&ir_context, root);

  debug_print_ir_buffer(&ir_context);

  generate_qbe("out.ssa", &ir_context);

  ir_buffer_free(&ir_context);

  // while (1) {
  //   Token token = lexer_get_token(&lexer);
  //   debug_print_token(token);

  //   if (token.type == TOKEN_EOF) {
  //     break;
  //   }
  // }

  ast_arena_free(&ast_arena);
}
