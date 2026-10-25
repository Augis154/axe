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
  case EXPR_LITERAL:
    switch (node->literal.type) {
    case LITERAL_INT:
      printf("%ld", node->literal.int_value);
      return;
    case LITERAL_FLOAT:
      printf("%f", node->literal.float_value);
      return;
    case LITERAL_CHAR:
      printf("'%c'", node->literal.char_value);
      return;
    case LITERAL_STRING:
      printf("\"%s\"", node->literal.string_value);
      return;
    }
    printf("<unknown literal type>");
    return;

  case EXPR_IDENTIFIER:
    printf("%s", node->identifier.name);
    return;

  case EXPR_ASSIGN:
    printf("(= ");
    debug_print_ast_sexpr(arena, node->assignment.target);
    printf(" ");
    debug_print_ast_sexpr(arena, node->assignment.value);
    printf(")");
    return;

  case EXPR_UNARY:
    printf("(%s ", unop_strings[node->expr_unary.op]);
    debug_print_ast_sexpr(arena, node->expr_unary.operand);
    printf(")");
    return;

  case EXPR_BINARY:
    printf("(%s ", binop_strings[node->expr_binary.op]);
    debug_print_ast_sexpr(arena, node->expr_binary.left);
    printf(" ");
    debug_print_ast_sexpr(arena, node->expr_binary.right);
    printf(")");
    return;

  case EXPR_INVALID:
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
  case STMT_BLOCK:
    printf("Block Statement:\n");
    for (size_t i = 0; i < stmt->stmt_block.count; i++) {
      print_indent(1);
      printf("Statement %zu:\n", i);
      print_indent(2);
      debug_print_ast_stmt(arena, stmt->stmt_block.statements[i]);
    }
    return;

  case STMT_EXPRESSION:
    printf("Expression Statement:\n");
    print_indent(1);
    debug_print_ast_sexpr(arena, stmt->stmt_expr.expression);
    printf("\n");
    return;

  case STMT_VAR_DECL:
    printf("Variable Declaration: %s\n", stmt->stmt_var_decl.name);
    // if (stmt->stmt_var_decl.type) {
    //   print_indent(1);
    //   printf("Type: %s\n", stmt->stmt_var_decl.type);
    // }
    if (stmt->stmt_var_decl.init) {
      print_indent(1);
      printf("Init: ");
      debug_print_ast_sexpr(arena, stmt->stmt_var_decl.init);
      printf("\n");
    }
    return;

  case STMT_BREAK:
    printf("Break Statement\n");
    return;

  case STMT_CONTINUE:
    printf("Continue Statement\n");
    return;

  case STMT_IF:
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

  case STMT_WHILE:
    printf("While Statement:\n");
    print_indent(1);
    printf("Condition: ");
    debug_print_ast_sexpr(arena, stmt->stmt_while.condition);
    printf("\n");
    print_indent(1);
    printf("Body:\n");
    debug_print_ast_stmt(arena, stmt->stmt_while.body);
    return;

  case STMT_INVALID:
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
  case DECL_INVALID:
    printf("<invalid declaration node>\n");
    return;
  }

  printf("<unknown declaration type>\n");
}

void debug_print_ir_buffer(IrContext *ir_ctx) {
  for (size_t i = 0; i < da_len(ir_ctx->buffer); i++) {
    IrInstr *inst = &ir_ctx->buffer[i];

    switch (inst->op) {
    case IR_INST_NONE:
      printf("%zu: <none>\n", i);
      continue;

    case IR_INST_LOAD_LITERAL:
      printf("%zu: r%u = %ld\n", i, inst->res.id, inst->literal);
      continue;

    case IR_INST_LOAD_VAR:
      printf("%zu: r%u = LOAD_VAR %s\n", i, inst->res.id, inst->var_name);
      continue;
    case IR_INST_STORE_VAR:
      printf("%zu: STORE_VAR %s = r%u\n", i, inst->var_name, inst->res.id);
      continue;

      // Unary operations
    case IR_INST_NEGATE:
      printf("%zu: r%u = -r%u\n", i, inst->res.id, inst->arg1.id);
      continue;
    case IR_INST_NOT:
      printf("%zu: r%u = !r%u\n", i, inst->res.id, inst->arg1.id);
      continue;

      // Binary operations
    case IR_INST_ADD:
      printf("%zu: r%u = r%u + r%u\n", i, inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;
    case IR_INST_SUB:
      printf("%zu: r%u = r%u - r%u\n", i, inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;
    case IR_INST_MUL:
      printf("%zu: r%u = r%u * r%u\n", i, inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;
    case IR_INST_DIV:
      printf("%zu: r%u = r%u / r%u\n", i, inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;

    case IR_INST_CMP_EQ:
      printf("%zu: r%u = r%u == r%u\n", i, inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;
    case IR_INST_CMP_NE:
      printf("%zu: r%u = r%u != r%u\n", i, inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;
    case IR_INST_CMP_LT:
      printf("%zu: r%u = r%u < r%u\n", i, inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;
    case IR_INST_CMP_GT:
      printf("%zu: r%u = r%u > r%u\n", i, inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;
    case IR_INST_CMP_LE:
      printf("%zu: r%u = r%u <= r%u\n", i, inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;
    case IR_INST_CMP_GE:
      printf("%zu: r%u = r%u >= r%u\n", i, inst->res.id, inst->arg1.id, inst->arg2.id);
      continue;

    case IR_INST_LABEL:
      printf("%zu: @l%u:\n", i, inst->target.id);
      continue;
    case IR_INST_JUMP:
      printf("%zu: JUMP @l%u\n", i, inst->target.id);
      continue;
    case IR_INST_JZ:
      printf("%zu: JZ r%u, @l%u\n", i, inst->arg1.id, inst->target.id);
      continue;
    case IR_INST_JNZ:
      printf("%zu: JNZ r%u, @l%u\n", i, inst->arg1.id, inst->target.id);
      continue;
    }

    printf("%zu: Unknown instruction\n", i);
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

  [BINOP_EQEQ] = "==",
  [BINOP_NOT_EQ] = "!=",
  [BINOP_LESS] = "<",
  [BINOP_GREATER] = ">",
  [BINOP_LESS_EQ] = "<=",
  [BINOP_GREATER_EQ] = ">=",
};
