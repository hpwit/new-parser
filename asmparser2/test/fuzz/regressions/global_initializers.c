// The first global initializer was stored before its address was loaded, and initializers were not protected across calls.
#include <stdio.h>
#include <stdint.h>
// Must match runner_fuzz.c's host functions.
static void emit(int v) { printf("EMIT %d\n", v); }
static int ext1(int v) { return v * 3 + 1; }
static int ext2(int a, int b) { return a - 2 * b; }
int g;
int h;
int k = 7;
int main()
{
   g = 3 + ext1(4);
   h = ext1(1) * 10 + ext1(2);
   emit(g);
   emit(h);
   emit(k);
   printf("DONE\n");
   return 0;
}
