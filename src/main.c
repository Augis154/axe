#include "ast.h"
#include "gen_ir.h"
#include "gen_qbe.h"
#include "compiler.h"
#include "debug.h"
#include "lexer.h"
#include "parser.h"

#include "lib/arena.h"
#include "lib/array.h"
#include "lib/str_table.h"
#include "sema.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  CompilationUnit comp_unit = {0};
  cu_init(&comp_unit);

  cu_read_source_file(&comp_unit, "test.axe");

  if (comp_unit.had_error) {
    cu_free(&comp_unit);
    return 1;
  }

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

  AstProgram *program = parse_program(&parser);

  if (comp_unit.had_error) {
    cu_free(&comp_unit);
    return 1;
  }

  SemaContext sema_ctx;
  sema_init(&sema_ctx, &comp_unit);

  sema_program(&sema_ctx, program);

  printf("Had error: %s\n", comp_unit.had_error ? "true" : "false");

  if (comp_unit.had_error) {
    cu_free(&comp_unit);
    return 1;
  }

  IrGenContext ir_context;
  gen_ir_init(&ir_context, &comp_unit, &sema_ctx);

  gen_ir(&ir_context, program);

  // debug_print_ir_module(ir_context.module);

  generate_qbe("out.ssa", ir_context.module);

  gen_ir_free(&ir_context);

  cu_free(&comp_unit);

  return 0;
}
