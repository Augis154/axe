#include "debug.h"

#include "lib/array.h"

#include <stdio.h>

static const char *token_strings[TOKEN_COUNT];
static const char *unop_strings[UNOP_COUNT];
static const char *binop_strings[BINOP_COUNT];

static void print_indent(int indent) {
  printf("%*s", indent * 2, "");
}

static const char *render_token_kind(TokenKind kind) {
  if (kind < 0 || kind >= TOKEN_COUNT) {
    return "Unknown";
  }
  return token_strings[kind];
}

void debug_print_token(Token token) {
  switch (token.kind) {
  case TOKEN_ERROR:
    printf("Error: %.*s\n", (int)token.lexeme_len, token.lexeme);
    break;
  case TOKEN_INT:
  case TOKEN_FLOAT:
  case TOKEN_IDENT:
    printf("Token: %s %.*s\n", render_token_kind(token.kind), (int)token.lexeme_len, token.lexeme);
    break;
  default:
    printf("Token: %s\n", render_token_kind(token.kind));
  }
}

void debug_print_ast_sexpr(Arena *arena, AstExpr *node) {
  if (!node) {
    printf("<null expr>\n");
    return;
  }

  switch (node->kind) {
  case AST_EXPR_LITERAL:
    switch (node->expr_literal.kind) {
    case AST_LITERAL_INT:
      printf("%ld", node->expr_literal.int_value);
      return;
    case AST_LITERAL_FLOAT:
      printf("%f", node->expr_literal.float_value);
      return;
    case AST_LITERAL_CHAR:
      printf("'%c'", node->expr_literal.char_value);
      return;
    case AST_LITERAL_STRING:
      printf("\"%s\"", node->expr_literal.string_value);
      return;
    }
    printf("<unknown literal type>");
    return;

  case AST_EXPR_IDENTIFIER:
    printf("%s", node->expr_identifier.name);
    return;

  case AST_EXPR_ASSIGN:
    printf("(= ");
    debug_print_ast_sexpr(arena, node->expr_assign.target);
    printf(" ");
    debug_print_ast_sexpr(arena, node->expr_assign.value);
    printf(")");
    return;
  case AST_EXPR_CALL:
    printf("(call ");
    debug_print_ast_sexpr(arena, node->expr_call.callee);
    for (size_t i = 0; i < node->expr_call.argument_count; i++) {
      printf(" ");
      debug_print_ast_sexpr(arena, node->expr_call.arguments[i]);
    }
    printf(")");
    return;

  case AST_EXPR_UNARY:
    printf("(%s ", unop_strings[node->expr_unary.op]);
    debug_print_ast_sexpr(arena, node->expr_unary.operand);
    printf(")");
    return;

  case AST_EXPR_BINARY:
    printf("(%s ", binop_strings[node->expr_binary.op]);
    debug_print_ast_sexpr(arena, node->expr_binary.left);
    printf(" ");
    debug_print_ast_sexpr(arena, node->expr_binary.right);
    printf(")");
    return;

  case AST_EXPR_CAST:
    printf("(cast ");
    debug_print_ast_sexpr(arena, node->expr_cast.operand);
    printf(")");
    return;

  case AST_EXPR_INVALID:
    printf("<invalid expr node>");
    return;
  }

  printf("<unknown expr node type>");
}

void debug_print_ast_stmt(Arena *arena, AstStmt *stmt) {
  if (!stmt) {
    printf("<null stmt>\n");
    return;
  }

  switch (stmt->kind) {
  case AST_STMT_BLOCK:
    printf("Block Statement:\n");
    for (size_t i = 0; i < stmt->stmt_block.statement_count; i++) {
      print_indent(1);
      printf("Statement %zu:\n", i);
      print_indent(2);
      debug_print_ast_stmt(arena, stmt->stmt_block.statements[i]);
    }
    return;

  case AST_STMT_EXPRESSION:
    printf("Expression Statement:\n");
    print_indent(1);
    debug_print_ast_sexpr(arena, stmt->stmt_expression.expression);
    printf("\n");
    return;

  case AST_STMT_LOCAL_VAR:
    printf("Variable Declaration: %s\n", stmt->stmt_local_var.name);
    // if (stmt->stmt_local_var.declared_type) {
    //   print_indent(1);
    //   printf("Type: %s\n", stmt->stmt_local_var.declared_type);
    // }
    if (stmt->stmt_local_var.init) {
      print_indent(1);
      printf("Init: ");
      debug_print_ast_sexpr(arena, stmt->stmt_local_var.init);
      printf("\n");
    }
    return;

  case AST_STMT_RETURN:
    printf("Return Statement:\n");
    if (stmt->stmt_return.value) {
      print_indent(1);
      printf("Value: ");
      debug_print_ast_sexpr(arena, stmt->stmt_return.value);
      printf("\n");
    }
    return;
  case AST_STMT_BREAK:
    printf("Break Statement\n");
    return;
  case AST_STMT_CONTINUE:
    printf("Continue Statement\n");
    return;

  case AST_STMT_IF:
    printf("If Statement:\n");
    print_indent(1);
    printf("Condition: ");
    debug_print_ast_sexpr(arena, stmt->stmt_if.condition);
    printf("\n");
    print_indent(1);
    printf("Then Branch:\n");
    debug_print_ast_stmt(arena, stmt->stmt_if.then_branch);
    if (stmt->stmt_if.else_branch) {
      print_indent(1);
      printf("Else Branch:\n");
      debug_print_ast_stmt(arena, stmt->stmt_if.else_branch);
    }
    return;

  case AST_STMT_WHILE:
    printf("While Statement:\n");
    print_indent(1);
    printf("Condition: ");
    debug_print_ast_sexpr(arena, stmt->stmt_while.condition);
    printf("\n");
    print_indent(1);
    printf("Body:\n");
    debug_print_ast_stmt(arena, stmt->stmt_while.body);
    return;

  case AST_STMT_INVALID:
    printf("<invalid statement node>\n");
    return;
  }

  printf("<unknown statement type>\n");
}

void debug_print_ast_decl(Arena *arena, AstDecl *decl) {
  if (!decl) {
    printf("<null decl>\n");
    return;
  }

  switch (decl->kind) {
  case AST_DECL_FUNCTION:
    printf("Function Declaration: %s\n", decl->decl_function.name);
    print_indent(1);
    printf("Body:\n");
    debug_print_ast_stmt(arena, decl->decl_function.body);
    return;

  case AST_DECL_INVALID:
    printf("<invalid declaration node>\n");
    return;
  }

  printf("<unknown declaration type>\n");
}

void debug_print_ir_function(IrFunction *ir_func) {
  for (size_t i = 0; i < da_len(ir_func->buffer); i++) {
    IrInstr *inst = &ir_func->buffer[i];

    switch (inst->op) {
    case IR_OP_NONE:
      printf("%zu: <none>\n", i);
      continue;

    case IR_OP_LOAD_LITERAL:
      printf("%zu: r%u = %ld\n", i, inst->res.id, inst->literal);
      continue;

    case IR_OP_LOAD_FLOAT:
      printf("%zu: r%u = %g\n", i, inst->res.id, inst->float_literal);
      continue;

    case IR_OP_LOAD_VAR:
      printf("%zu: r%u = LOAD_VAR %s\n", i, inst->res.id, inst->var_name);
      continue;
    case IR_OP_STORE_VAR:
      printf("%zu: STORE_VAR %s = r%u\n", i, inst->var_name, inst->res.id);
      continue;

      // Unary operations
    case IR_OP_NEGATE:
      printf("%zu: r%u = -r%u\n", i, inst->res.id, inst->arg1.id);
      continue;
    case IR_OP_NOT:
      printf("%zu: r%u = !r%u\n", i, inst->res.id, inst->arg1.id);
      continue;

      // Binary operations
    case IR_OP_ADD:
      printf("%zu: r%u = r%u + r%u\n", i, inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;
    case IR_OP_SUB:
      printf("%zu: r%u = r%u - r%u\n", i, inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;
    case IR_OP_MUL:
      printf("%zu: r%u = r%u * r%u\n", i, inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;
    case IR_OP_DIV:
      printf("%zu: r%u = r%u / r%u\n", i, inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;

    case IR_OP_CMP_EQ:
      printf("%zu: r%u = r%u == r%u\n", i, inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;
    case IR_OP_CMP_NE:
      printf("%zu: r%u = r%u != r%u\n", i, inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;
    case IR_OP_CMP_LT:
      printf("%zu: r%u = r%u < r%u\n", i, inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;
    case IR_OP_CMP_GT:
      printf("%zu: r%u = r%u > r%u\n", i, inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;
    case IR_OP_CMP_LE:
      printf("%zu: r%u = r%u <= r%u\n", i, inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;
    case IR_OP_CMP_GE:
      printf("%zu: r%u = r%u >= r%u\n", i, inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;

    case IR_OP_LABEL:
      printf("%zu: @l%u:\n", i, inst->target.id);
      continue;
    case IR_OP_JUMP:
      printf("%zu: JUMP @l%u\n", i, inst->target.id);
      continue;
    case IR_OP_JZ:
      printf("%zu: JZ r%u, @l%u\n", i, inst->arg1.id, inst->target.id);
      continue;
    case IR_OP_JNZ:
      printf("%zu: JNZ r%u, @l%u\n", i, inst->arg1.id, inst->target.id);
      continue;

    case IR_OP_CALL:
      printf("%zu: CALL r%u, %s(", i, inst->res.id, inst->var_name);
      for (size_t j = 0; j < inst->call.arg_count; j++) {
        printf("r%u", inst->call.args[j].reg.id);
        if (j < inst->call.arg_count - 1) {
          printf(", ");
        }
      }
      printf(")\n");
      continue;
    case IR_OP_RETURN:
      printf("%zu: RETURN r%u\n", i, inst->arg1.id);
      continue;

    case IR_OP_ITOF:
      printf("%zu: r%u = ITOF r%u\n", i, inst->res.id, inst->arg1.id);
      continue;
    case IR_OP_FTOI:
      printf("%zu: r%u = FTOI r%u\n", i, inst->res.id, inst->arg1.id);
      continue;

    case IR_OP_SEXT:
      printf("%zu: r%u = SEXT r%u\n", i, inst->res.id, inst->arg1.id);
      continue;
    case IR_OP_ZEXT:
      printf("%zu: r%u = ZEXT r%u\n", i, inst->res.id, inst->arg1.id);
      continue;
    case IR_OP_TRUNC:
      printf("%zu: r%u = TRUNC r%u\n", i, inst->res.id, inst->arg1.id);
      continue;
    case IR_OP_FEXT:
      printf("%zu: r%u = FEXT r%u\n", i, inst->res.id, inst->arg1.id);
      continue;
    case IR_OP_FTRUNC:
      printf("%zu: r%u = FTRUNC r%u\n", i, inst->res.id, inst->arg1.id);
      continue;
    }

    printf("%zu: Unknown instruction\n", i);
  }
}

void debug_print_ir_module(IrModule *module) {
  for (size_t i = 0; i < da_len(module->functions); i++) {
    IrFunction *func = &module->functions[i];
    printf("Function: %s\n", func->name);
    debug_print_ir_function(func);
  }
}

static const char *token_strings[TOKEN_COUNT] = {
#define TOKEN_NAME(token, name) [token] = name,
  TOKEN_LIST(TOKEN_NAME)
#undef TOKEN_NAME
};

static const char *unop_strings[UNOP_COUNT] = {
  [UNOP_NEGATE] = "-",
};

static const char *binop_strings[BINOP_COUNT] = {
  [BINOP_ADD] = "+",
  [BINOP_SUB] = "-",
  [BINOP_MUL] = "*",
  [BINOP_DIV] = "/",

  [BINOP_EQ] = "==",
  [BINOP_NOT_EQ] = "!=",
  [BINOP_LESS] = "<",
  [BINOP_GREATER] = ">",
  [BINOP_LESS_EQ] = "<=",
  [BINOP_GREATER_EQ] = ">=",
};
