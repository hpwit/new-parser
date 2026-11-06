// Fifth arguments of an outer and a nested five-argument call shared one stack slot.
#include <stdio.h>
#include <stdint.h>
// Must match runner_fuzz.c's host functions.
static void emit(int v) { printf("EMIT %d\n", v); }
static int ext1(int v) { return v * 3 + 1; }
static int ext2(int a, int b) { return a - 2 * b; }
int f(int a, int b, int c, int d, int e)
{
   return a + 2 * b + 3 * c + 4 * d + 5 * e;
}
int main()
{
   emit(f(f(ext1(1), 2, 3, 4, ext2(1, 2)), ext1(3), 4, f(1, 2, 3, 4, 5), ext1(9)));
   printf("DONE\n");
   return 0;
}
