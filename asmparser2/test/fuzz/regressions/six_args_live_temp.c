// A temporary in a15 (y) was overwritten by the sixth argument of a call in the same expression.
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
int y;
int main()
{
   y = 1000;
   emit(y + f(1, 2, 3, 4, 5, 6));
   printf("DONE\n");
   return 0;
}
