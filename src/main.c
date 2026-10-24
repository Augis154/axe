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
  // char *source = "1+2;";
  // char *source = "(1+2)*3--4;";
  char *source = "var x: int = 1_0+02; x + 0x0F; 0b101 + x;";

  CompilationUnit comp_unit = {0};

  st_init(&comp_unit.symbol_table, 1024);
  st_init(&comp_unit.intern_table, 1024);

  comp_unit.source = source;
  comp_unit.source_len = strlen(source);

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
    ast_arena_free(&comp_unit.ast_arena);
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
  ast_arena_free(&comp_unit.ast_arena);
}
