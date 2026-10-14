
#include <stdio.h>
#include <time.h>

#include "common.h"
#include "chunk.h"
#include "vm.h"

int main(int argc, const char* argv[]) {
  initVM();

  Chunk chunk;
  initChunk(&chunk);

  writeConstant(&chunk, 5.0, 123);

  // Negate the value 1 million times.
  for (int i = 0; i < 1000000; i++) {
    writeChunk(&chunk, OP_NEGATE, 123);
  }

  writeChunk(&chunk, OP_RETURN, 123);

  clock_t start = clock();
  InterpretResult result = interpret(&chunk);
  clock_t end = clock();

  printf("Execution time: %.6f seconds\n",
         (double)(end - start) / CLOCKS_PER_SEC);

  freeChunk(&chunk);
  freeVM();

  return result == INTERPRET_OK ? 0 : 1;
}
