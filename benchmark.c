
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "src/table.h"
#include "src/vm.h"

#define SMALL 1000
#define LARGE 100000

static double elapsed(clock_t start) {
  return (double)(clock() - start) / CLOCKS_PER_SEC;
}

static void benchmark(int size, int mode) {
  Table table;
  initTable(&table);

  clock_t start = clock();

  if (mode == 1) {
    // Insert sequential number keys.
    for (int i = 0; i < size; i++) {
      tableSet(&table, NUMBER_VAL(i), NUMBER_VAL(i));
    }
  } else if (mode == 2) {
    // Insert and look up sequential keys.
    for (int i = 0; i < size; i++) {
      tableSet(&table, NUMBER_VAL(i), NUMBER_VAL(i));
    }

    Value result;
    for (int i = 0; i < size; i++) {
      tableGet(&table, NUMBER_VAL(i), &result);
    }
  } else if (mode == 3) {
    // Insert and delete half the keys.
    for (int i = 0; i < size; i++) {
      tableSet(&table, NUMBER_VAL(i), NUMBER_VAL(i));
    }

    for (int i = 0; i < size; i += 2) {
      tableDelete(&table, NUMBER_VAL(i));
    }
  } else if (mode == 4) {
    // Insert shuffled number keys.
    int* keys = malloc(sizeof(int) * size);

    for (int i = 0; i < size; i++) keys[i] = i;

    for (int i = size - 1; i > 0; i--) {
      int j = rand() % (i + 1);
      int temp = keys[i];
      keys[i] = keys[j];
      keys[j] = temp;
    }

    for (int i = 0; i < size; i++) {
      tableSet(&table, NUMBER_VAL(keys[i]), NUMBER_VAL(i));
    }

    free(keys);
  }

  double seconds = elapsed(start);

  printf("%-23s | %7d keys | %.6f seconds\n",
         mode == 1 ? "Sequential insertion" :
         mode == 2 ? "Insertion + lookup" :
         mode == 3 ? "Insertion + deletion" :
                     "Shuffled insertion",
         size, seconds);

  freeTable(&table);
}

int main(void) {
  initVM();
  srand(12345);

  printf("Chapter 20 Hash Table Benchmarks\n\n");

  for (int mode = 1; mode <= 4; mode++) {
    benchmark(SMALL, mode);
    benchmark(LARGE, mode);
    printf("\n");
  }

  freeVM();
  return 0;
}
