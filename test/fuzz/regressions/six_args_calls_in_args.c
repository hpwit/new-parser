// An argument kept in a15 across a call made for another argument was not saved: calls were not seen as reading their argument registers.
#include <stdio.h>
#include <stdint.h>
// Must match runner_fuzz.c's host functions.
static void emit(int v) { printf("EMIT %d\n", v); }
static int ext1(int v) { return v * 3 + 1; }
static int ext2(int a, int b) { return a - 2 * b; }
int f(int a, int b, int c, int d, int e, int g)
{
   return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * g;
}
int main()
{
   emit(f(ext1(1), 2, 3, 4, f(1, 1, 1, 1, 1, ext1(2)), ext2(5, 1)));
   printf("DONE\n");
   return 0;
}
