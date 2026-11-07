#include "debug.h"

#include "lib/array.h"

#include <stdio.h>

static const char *token_strings[TOKEN_COUNT];
static const char *unop_strings[UNOP_COUNT];
static const char *binop_strings[BINOP_COUNT];
static const char *logicop_strings[LOGIC_COUNT];

// Token printing functions

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

// AST printing functions

void debug_print_ast_sexpr(Arena *arena, AstExpr *expr) {
  if (!expr) {
    printf("<null expr>\n");
    return;
  }

  switch (expr->kind) {
  case AST_EXPR_LITERAL:
    switch (expr->expr_literal.kind) {
    case AST_LITERAL_INT:
      printf("%ld", expr->expr_literal.int_value);
      return;
    case AST_LITERAL_FLOAT:
      printf("%f", expr->expr_literal.float_value);
      return;
    case AST_LITERAL_NULL:
      printf("null");
      return;
    case AST_LITERAL_TRUE:
      printf("true");
      return;
    case AST_LITERAL_FALSE:
      printf("false");
      return;
    case AST_LITERAL_CHAR:
      printf("'%c'", expr->expr_literal.char_value);
      return;
    case AST_LITERAL_STRING:
      printf("\"%s\"", expr->expr_literal.string_value);
      return;
    }
    printf("<unknown literal type>");
    return;

  case AST_EXPR_IDENTIFIER:
    printf("%s", expr->expr_identifier.name);
    return;

  case AST_EXPR_ARRAY_LITERAL:
    printf("(array-literal ");
    if (expr->expr_array_literal.is_repeat) {
      printf("(repeat ");
      debug_print_ast_sexpr(arena, expr->expr_array_literal.repeat.value);
      printf(" ");
      debug_print_ast_sexpr(arena, expr->expr_array_literal.repeat.length_expr);
      printf(")");
    } else {
      printf("(list ");
      for (uint32_t i = 0; i < expr->expr_array_literal.list.element_count; i++) {
        AstArrayElement *elem = &expr->expr_array_literal.list.elements[i];
        if (elem->index_expr) {
          printf("(sparse ");
          debug_print_ast_sexpr(arena, elem->index_expr);
          printf(" ");
          debug_print_ast_sexpr(arena, elem->value);
          printf(")");
        } else {
          debug_print_ast_sexpr(arena, elem->value);
        }
        if (i < expr->expr_array_literal.list.element_count - 1) {
          printf(" ");
        }
      }
      printf(")");
    }
    printf(")");
    return;

  case AST_EXPR_ADDR_OF:
    printf("(& ");
    debug_print_ast_sexpr(arena, expr->expr_addr_of.operand);
    printf(")");
    return;

  case AST_EXPR_DEREF:
    printf("(.* ");
    debug_print_ast_sexpr(arena, expr->expr_deref.operand);
    printf(")");
    return;

  case AST_EXPR_SUBSCRIPT:
    printf("([] ");
    debug_print_ast_sexpr(arena, expr->expr_subscript.target);
    printf(" ");
    debug_print_ast_sexpr(arena, expr->expr_subscript.index);
    printf(")");
    return;

  case AST_EXPR_CALL:
    printf("(call ");
    debug_print_ast_sexpr(arena, expr->expr_call.callee);
    for (uint32_t i = 0; i < expr->expr_call.argument_count; i++) {
      printf(" ");
      debug_print_ast_sexpr(arena, expr->expr_call.arguments[i]);
    }
    printf(")");
    return;

  case AST_EXPR_UNARY:
    printf("(%s ", unop_strings[expr->expr_unary.op]);
    debug_print_ast_sexpr(arena, expr->expr_unary.operand);
    printf(")");
    return;

  case AST_EXPR_BINARY:
    printf("(%s ", binop_strings[expr->expr_binary.op]);
    debug_print_ast_sexpr(arena, expr->expr_binary.left);
    printf(" ");
    debug_print_ast_sexpr(arena, expr->expr_binary.right);
    printf(")");
    return;

  case AST_EXPR_LOGICAL:
    printf("(%s ", logicop_strings[expr->expr_logical.op]);
    debug_print_ast_sexpr(arena, expr->expr_logical.left);
    printf(" ");
    debug_print_ast_sexpr(arena, expr->expr_logical.right);
    printf(")");
    return;

  case AST_EXPR_CAST:
    printf("(cast ");
    debug_print_ast_sexpr(arena, expr->expr_cast.operand);
    printf(")");
    return;

  case AST_EXPR_ASSIGN:
    printf("(= ");
    debug_print_ast_sexpr(arena, expr->expr_assign.target);
    printf(" ");
    debug_print_ast_sexpr(arena, expr->expr_assign.value);
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
    for (uint32_t i = 0; i < stmt->stmt_block.statement_count; i++) {
      print_indent(1);
      printf("Statement %u:\n", i);
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

// IR printing functions

// The name of an IR type such as `i32`, `ptr` or `agg0`; the result is valid until a few more calls are made.
static const char *ir_type_name(IrType type) {
  static char buffers[4][16];
  static unsigned next_buffer;
  char *buf = buffers[next_buffer++ % 4];

  switch (type.kind) {
  case IR_TYPE_NONE:
    return "void";
  case IR_TYPE_INT:
    snprintf(buf, 16, "%c%u", type.is_signed ? 'i' : 'u', type.size * 8);
    break;
  case IR_TYPE_FLOAT:
    snprintf(buf, 16, "f%u", type.size * 8);
    break;
  case IR_TYPE_PTR:
    return "ptr";
  case IR_TYPE_AGG:
    snprintf(buf, 16, "agg%u", type.agg_id);
    break;
  }

  return buf;
}

static const char *reg_type_name(const IrFunction *func, IrReg reg) {
  return reg.id < da_len(func->reg_types) ? ir_type_name(func->reg_types[reg.id]) : "?";
}

void debug_print_ir_function(IrFunction *func) {
  for (size_t i = 0; i < da_len(func->buffer); i++) {
    IrInstr *inst = &func->buffer[i];

    switch (inst->op) {
    case IR_OP_NONE:
      printf("%zu: <none>\n", i);
      continue;

    // Define a register from a constant or another register
    case IR_OP_LOAD_INT:
      printf("%zu: r%u:%s = %ld\n", i, inst->res.id, reg_type_name(func, inst->res), inst->int_lit);
      continue;

    case IR_OP_LOAD_FLOAT:
      printf("%zu: r%u:%s = %g\n", i, inst->res.id, reg_type_name(func, inst->res), inst->float_lit);
      continue;

    case IR_OP_COPY:
      printf("%zu: r%u:%s = r%u\n", i, inst->res.id, reg_type_name(func, inst->res), inst->arg1.id);
      continue;

    // Memory
    case IR_OP_ALLOC:
      printf("%zu: r%u:%s = ALLOC %ld\n", i, inst->res.id, reg_type_name(func, inst->res), inst->int_lit);
      continue;
    case IR_OP_LOAD:
      printf("%zu: r%u:%s = LOAD [r%u]\n", i, inst->res.id, reg_type_name(func, inst->res), inst->arg1.id);
      continue;
    case IR_OP_STORE:
      printf("%zu: STORE [r%u] = r%u:%s\n", i, inst->arg2.id, inst->arg1.id, reg_type_name(func, inst->arg1));
      continue;
    case IR_OP_MEMCPY:
      printf("%zu: MEMCPY src=[r%u], dst=[r%u], size=%ld\n", i, inst->arg1.id, inst->arg2.id, inst->int_lit);
      continue;
    case IR_OP_MEMSET:
      printf("%zu: MEMSET r%u, dst=[r%u], size=%ld\n", i, inst->arg1.id, inst->arg2.id, inst->int_lit);
      continue;

    // Unary arithmetic and logic
    case IR_OP_NEGATE:
      printf("%zu: r%u:%s = -r%u\n", i, inst->res.id, reg_type_name(func, inst->res), inst->arg1.id);
      continue;
    case IR_OP_NOT:
      printf("%zu: r%u:%s = !r%u\n", i, inst->res.id, reg_type_name(func, inst->res), inst->arg1.id);
      continue;

    // Binary arithmetic
    case IR_OP_ADD:
      printf(
        "%zu: r%u:%s = r%u + r%u\n", i, inst->res.id, reg_type_name(func, inst->res), inst->arg1.id, inst->arg2.id);
      continue;
    case IR_OP_SUB:
      printf(
        "%zu: r%u:%s = r%u - r%u\n", i, inst->res.id, reg_type_name(func, inst->res), inst->arg1.id, inst->arg2.id);
      continue;
    case IR_OP_MUL:
      printf(
        "%zu: r%u:%s = r%u * r%u\n", i, inst->res.id, reg_type_name(func, inst->res), inst->arg1.id, inst->arg2.id);
      continue;
    case IR_OP_DIV:
      printf(
        "%zu: r%u:%s = r%u / r%u\n", i, inst->res.id, reg_type_name(func, inst->res), inst->arg1.id, inst->arg2.id);
      continue;

    // Comparisons; the result is a bool
    case IR_OP_CMP_EQ:
      printf(
        "%zu: r%u:%s = r%u == r%u\n", i, inst->res.id, reg_type_name(func, inst->res), inst->arg1.id, inst->arg2.id);
      continue;
    case IR_OP_CMP_NE:
      printf(
        "%zu: r%u:%s = r%u != r%u\n", i, inst->res.id, reg_type_name(func, inst->res), inst->arg1.id, inst->arg2.id);
      continue;
    case IR_OP_CMP_LT:
      printf(
        "%zu: r%u:%s = r%u < r%u\n", i, inst->res.id, reg_type_name(func, inst->res), inst->arg1.id, inst->arg2.id);
      continue;
    case IR_OP_CMP_GT:
      printf(
        "%zu: r%u:%s = r%u > r%u\n", i, inst->res.id, reg_type_name(func, inst->res), inst->arg1.id, inst->arg2.id);
      continue;
    case IR_OP_CMP_LE:
      printf(
        "%zu: r%u:%s = r%u <= r%u\n", i, inst->res.id, reg_type_name(func, inst->res), inst->arg1.id, inst->arg2.id);
      continue;
    case IR_OP_CMP_GE:
      printf(
        "%zu: r%u:%s = r%u >= r%u\n", i, inst->res.id, reg_type_name(func, inst->res), inst->arg1.id, inst->arg2.id);
      continue;

    // Bitwise operations
    case IR_OP_BIT_NOT:
      printf("%zu: r%u:%s = ~r%u\n", i, inst->res.id, reg_type_name(func, inst->res), inst->arg1.id);
      continue;
    case IR_OP_BIT_OR:
      printf(
        "%zu: r%u:%s = r%u | r%u\n", i, inst->res.id, reg_type_name(func, inst->res), inst->arg1.id, inst->arg2.id);
      continue;
    case IR_OP_BIT_XOR:
      printf(
        "%zu: r%u:%s = r%u ^ r%u\n", i, inst->res.id, reg_type_name(func, inst->res), inst->arg1.id, inst->arg2.id);
      continue;
    case IR_OP_BIT_AND:
      printf(
        "%zu: r%u:%s = r%u & r%u\n", i, inst->res.id, reg_type_name(func, inst->res), inst->arg1.id, inst->arg2.id);
      continue;
    case IR_OP_SHL:
      printf(
        "%zu: r%u:%s = r%u << r%u\n", i, inst->res.id, reg_type_name(func, inst->res), inst->arg1.id, inst->arg2.id);
      continue;
    case IR_OP_SHR:
      printf(
        "%zu: r%u:%s = r%u >> r%u\n", i, inst->res.id, reg_type_name(func, inst->res), inst->arg1.id, inst->arg2.id);
      continue;

    // Conversions between integer and float
    case IR_OP_ITOF:
      printf("%zu: r%u:%s = ITOF r%u\n", i, inst->res.id, reg_type_name(func, inst->res), inst->arg1.id);
      continue;
    case IR_OP_FTOI:
      printf("%zu: r%u:%s = FTOI r%u\n", i, inst->res.id, reg_type_name(func, inst->res), inst->arg1.id);
      continue;

    // Conversions that change the width of an integer
    case IR_OP_SEXT:
      printf("%zu: r%u:%s = SEXT r%u\n", i, inst->res.id, reg_type_name(func, inst->res), inst->arg1.id);
      continue;
    case IR_OP_ZEXT:
      printf("%zu: r%u:%s = ZEXT r%u\n", i, inst->res.id, reg_type_name(func, inst->res), inst->arg1.id);
      continue;
    case IR_OP_TRUNC:
      printf("%zu: r%u:%s = TRUNC r%u\n", i, inst->res.id, reg_type_name(func, inst->res), inst->arg1.id);
      continue;
    // Conversions that change the width of a float
    case IR_OP_FEXT:
      printf("%zu: r%u:%s = FEXT r%u\n", i, inst->res.id, reg_type_name(func, inst->res), inst->arg1.id);
      continue;
    case IR_OP_FTRUNC:
      printf("%zu: r%u:%s = FTRUNC r%u\n", i, inst->res.id, reg_type_name(func, inst->res), inst->arg1.id);
      continue;

    // Control flow within a function
    case IR_OP_LABEL:
      printf("%zu: @l%u:\n", i, inst->target.id);
      continue;
    case IR_OP_JUMP:
      printf("%zu: JUMP @l%u\n", i, inst->target.id);
      continue;
    case IR_OP_JZ:
      printf("%zu: JZ r%u, @l%u, @l%u\n", i, inst->arg1.id, inst->target_true.id, inst->target_false.id);
      continue;
    case IR_OP_JNZ:
      printf("%zu: JNZ r%u, @l%u, @l%u\n", i, inst->arg1.id, inst->target_true.id, inst->target_false.id);
      continue;

    // Calls and returns
    case IR_OP_CALL:
      printf("%zu: CALL r%u:%s, %s(", i, inst->res.id, reg_type_name(func, inst->res), inst->call.callee);
      for (uint32_t j = 0; j < inst->call.arg_count; j++) {
        printf("r%u:%s", inst->call.args[j].id, reg_type_name(func, inst->call.args[j]));
        if (j < inst->call.arg_count - 1) {
          printf(", ");
        }
      }
      printf(")\n");
      continue;
    case IR_OP_RETURN:
      if (inst->arg1.id == NULL_REG.id) {
        printf("%zu: RETURN\n", i);
      } else {
        printf("%zu: RETURN r%u\n", i, inst->arg1.id);
      }
      continue;
    }

    printf("%zu: Unknown instruction\n", i);
  }
}

void debug_print_ir_module(IrModule *module) {
  for (size_t i = 0; i < da_len(module->aggregates); i++) {
    IrAggregate *agg = &module->aggregates[i];

    printf("Aggregate: %s = align %u {", agg->name, agg->alignment);
    for (size_t j = 0; j < da_len(agg->fields); j++) {
      printf("%s %s x%u", j > 0 ? "," : "", ir_type_name(agg->fields[j].type), agg->fields[j].count);
    }
    printf(" }\n");
  }

  for (size_t i = 0; i < da_len(module->functions); i++) {
    IrFunction *func = &module->functions[i];

    printf("Function: %s(", func->name);
    for (uint32_t j = 0; j < func->param_count; j++) {
      printf("%sr%u:%s", j > 0 ? ", " : "", func->params[j].id, reg_type_name(func, func->params[j]));
    }
    printf(") -> %s\n", ir_type_name(func->return_type));

    debug_print_ir_function(func);
  }
}

// String tables

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

  [BINOP_BIT_AND] = "&",
  [BINOP_BIT_XOR] = "^",
  [BINOP_BIT_OR] = "|",
};

static const char *logicop_strings[LOGIC_COUNT] = {
  [LOGIC_OR] = "||",
  [LOGIC_AND] = "&&",
};