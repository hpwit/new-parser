// `%` was always unsigned: -7 % 3 gave 0 instead of -1.
#include <stdio.h>
#include <stdint.h>
// Must match runner_fuzz.c's host functions.
static void emit(int v) { printf("EMIT %d\n", v); }
static int ext1(int v) { return v * 3 + 1; }
static int ext2(int a, int b) { return a - 2 * b; }
int a;
int b;
int main()
{
   a = 0 - 7;
   b = 3;
   emit(a % b);
   emit(7 % (0 - 3));
   emit((0 - 7) % (0 - 3));
   emit(a / b);
   printf("DONE\n");
   return 0;
}
