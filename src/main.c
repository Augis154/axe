#include "debug.h"
#include "lexer.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  char *source = "1+2*a;";

  Lexer lexer;
  lexer_init(&lexer, source, strlen(source));

  while (1) {
    Token token = lexer_get_token(&lexer);
    debug_print_token(token);

    if (token.type == TOKEN_EOF) {
      break;
    }
  }
}