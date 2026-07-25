#include "debug.h"

#include <stdio.h>

void debug_print_token(Token token) {
  switch (token.type) {
  case TOKEN_ERROR:
    printf("Error: %.*s\n", (int)token.lexeme_len, token.lexeme);
    break;
  case TOKEN_NUMBER:
  case TOKEN_IDENT:
    printf("Token: %s %.*s\n", lexer_render_token_type(token.type), (int)token.lexeme_len, token.lexeme);
    break;
  default:
    printf("Token: %s\n", lexer_render_token_type(token.type));
  }
}
