#include "compiler.h"

#include <stdio.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <input.mh> [output.ssa]\n", argv[0]);
    return 2;
  }

  const char *input_path = argv[1];
  const char *output_path = argc > 2 ? argv[2] : "out.ssa";

  return compile_file(input_path, output_path) ? 0 : 1;
}
