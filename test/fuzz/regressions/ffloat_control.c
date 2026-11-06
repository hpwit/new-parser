// ffloat: for/while with ffloat conditions, += -= *= /= ++ --, 2D arrays, abs, a float function, a division between calls -- raw Q16.16 bits against a C model.
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
typedef int32_t ff;
static ff lit(double v) { double f = v * 65536.0; f = f < 0 ? f - 0.5 : f + 0.5; return (ff)f; }
static ff fromInt(int x) { return (ff)((uint32_t)x << 16); }
static ff mul(ff a, ff b) { return (ff)(((int64_t)a * b) >> 16); }
static ff dv(ff a, ff b)
{
   if (b == 0) return ((a ^ b) < 0) ? -0x7fffffff : 0x7fffffff;
   return (ff)(((int64_t)a << 16) / b);
}
static float toFloatC(ff x) { return (float)(x >> 1) * (1.0f / 32768.0f); }
static void emit(int v) { printf("EMIT %d\n", v); }
static void emitf(ff v) { printf("EMIT %d\n", v); }
ff grid[3][4];
static float twice(float x) { return x * 2; }
static ff scale(ff v, int n) { return mul(v, fromInt(n)) + lit(0.125); }
int main()
{
   ff x;
   int steps = 0;
   for (x = 0; x < fromInt(2); x = x + lit(0.25))
      steps++;
   emit(steps);
   emitf(x);
   ff y = fromInt(10);
   while (y > lit(0.5))
      y = dv(y, fromInt(3));
   emitf(y);
   ff a = lit(1.5);
   a += lit(0.25);
   emitf(a);
   a -= fromInt(3);
   emitf(a);
   a = mul(a, lit(-2.5));
   emitf(a);
   a = dv(a, fromInt(4));
   emitf(a);
   a += fromInt(1);
   emitf(a);
   a -= fromInt(1);
   a -= fromInt(1);
   emitf(a);
   for (int r = 0; r < 3; r++)
      for (int c = 0; c < 4; c++)
         grid[r][c] = mul(fromInt(r), lit(1.5)) - mul(fromInt(c), lit(0.25));
   emitf(grid[2][3]);
   emitf(grid[1][0] + grid[0][2]);
   ff n = lit(-3.75);
   emitf(abs(n));
   float t = twice(toFloatC(n));
   emit((int)(t * 100));
   emitf(lit(16 / -0.25));
   emitf(dv(scale(a, 3), scale(lit(1.25), 2)) + mul(scale(lit(0.5), 4), fromInt(2)));
   int k = 7;
   ff w = dv(fromInt(k), fromInt(2)) + n;
   emitf(w);
   printf("DONE\n");
   return 0;
}
