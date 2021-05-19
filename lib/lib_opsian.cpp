#include <stdio.h>

extern "C" void start_opsian_native();

void start_opsian_native() {
  printf("Hello world, native!\n");
  // TODO: call into native agent here
}

