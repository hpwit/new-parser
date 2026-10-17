// A for loop whose condition is != or == against a constant looped while it was false.
#include <stdio.h>
#include <stdint.h>
// Must match runner_fuzz.c's host functions.
static void emit(int v) { printf("EMIT %d\n", v); }
static int ext1(int v) { return v * 3 + 1; }
static int ext2(int a, int b) { return a - 2 * b; }
int main()
{
   for (int i = 0; i != 5; i++)
      emit(i);
   int n = 0;
   for (int j = 0; j == 0; j++)
      n = n + 1;
   emit(n);
   printf("DONE\n");
   return 0;
}
