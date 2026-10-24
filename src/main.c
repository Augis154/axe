#include "ast.h"
#include "backend.h"
#include "compiler.h"
#include "debug.h"
#include "frontend.h"
#include "ir.h"

#include "lib/arena.h"
#include "lib/array.h"
#include "lib/str_table.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  CompilationUnit comp_unit = {0};
  cu_init(&comp_unit);

  cu_read_source_file(&comp_unit, "test.axe");

  // Lexer lexer;
  // lexer_init(&lexer, &comp_unit);

  // while (1) {
  //   Token token = lexer_get_token(&lexer);
  //   debug_print_token(token);

  //   if (token.type == TOKEN_EOF) {
  //     break;
  //   }
  // }

  // return 0;

  Parser parser;
  parser_init(&parser, &comp_unit);

  AstStmt *statements = NULL;

  while (true) {
    AstStmt *stmt = parse_statement(&parser);
    if (!stmt) {
      break;
    }
    da_append(statements, *stmt);

    if (parser.next.type == TOKEN_EOF) {
      break;
    }
  }
  // AstStmt *root = parse_statement(&parser);

  // debug_print_ast_tree(&ast_arena, root);

  // debug_print_ast_stmt(&ast_arena, root);
  // printf("\n");

  printf("Had error: %s\n", comp_unit.had_error ? "true" : "false");

  if (comp_unit.had_error) {
    da_free(statements);
    cu_free(&comp_unit);
    return 1;
  }

  IrContext ir_context;
  ir_gen_init(&ir_context, &comp_unit);

  for (size_t i = 0; i < da_len(statements); i++) {
    AstStmt *stmt = &statements[i];
    emit_ir(&ir_context, stmt);
  }

  // emit_ir(&ir_context, root);

  debug_print_ir_buffer(&ir_context);

  generate_qbe("out.ssa", &ir_context);

  ir_buffer_free(&ir_context);

  da_free(statements);
  cu_free(&comp_unit);

  return 0;
}
