// Temporaries ran out into a10: -120 waited there while dv(g0) loaded its argument over it.
#include <stdio.h>
#include <stdint.h>
// Must match runner_fuzz.c's host functions.
static void emit(int v) { printf("EMIT %d\n", v); }
int g0;
int g2;
int nn(int v)
{
   if (v < 0)
      v = 0 - v;
   return v;
}
int dv(int v)
{
   return nn(v) % 7 + 1;
}
int f2(int p0, int p1, int p2, int p3, int p4, int p5)
{
   return p0 + 2 * p1 + 3 * p2 + 4 * p3 + 5 * p4 + 6 * p5;
}
int main()
{
   g0 = 9;
   g2 = 4;
   emit(f2(1, 1, f2(((0 - 120) / dv(g0)), 15, (3 / dv(g2)), (nn(g0) % dv(g2)), g0, (nn(g0) % dv(607))), ((493 % (0 - (dv(g2) + 1))) / dv((210 / dv(1721)))), 1, 1));
   printf("DONE\n");
   return 0;
}
