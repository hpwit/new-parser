// A six-argument call nested in another overwrote the outer call's arguments held in a14/a15.
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
   emit(f(f(1, 2, 3, 4, 5, 6), 7, 8, 9, 10, 11));
   printf("DONE\n");
   return 0;
}
