// ffloat fast paths: division with divisors below/above 128.0 (two 8-bit quou rounds vs bit loop), by zero, by integer literals (quos), integer multipliers on either side (mull) -- raw Q16.16 bits against a C model.
#include <stdio.h>
#include <stdint.h>
typedef int32_t ff;
static ff lit(double v) { double f = v * 65536.0; f = f < 0 ? f - 0.5 : f + 0.5; return (ff)f; }
static ff fromInt(int x) { return (ff)((uint32_t)x << 16); }
static ff mul(ff a, ff b) { return (ff)(((int64_t)a * b) >> 16); }
static ff dv(ff a, ff b)
{
   if (b == 0) return ((a ^ b) < 0) ? -0x7fffffff : 0x7fffffff;
   return (ff)(((int64_t)a << 16) / b);
}
static void emitf(ff v) { printf("EMIT %d\n", v); }
int iarr[3];
int main()
{
   ff a = lit(1000.123);
   ff sizes[6] = {lit(0.003), lit(1.7), lit(127.99), fromInt(128), lit(5000.5), lit(-300.25)};
   for (int k = 0; k < 6; k++)
   {
      emitf(dv(a, sizes[k]));
      emitf(dv(-a, sizes[k]));
      emitf(dv(lit(0.5), sizes[k]));
   }
   emitf(dv(fromInt(30000), lit(-0.75)));
   int zero = 0, n = 7;
   emitf(dv(a, fromInt(zero)));
   emitf(dv(a, fromInt(n)));
   emitf(dv(a, fromInt(7)));
   emitf(dv(a, fromInt(-7)));
   emitf(dv(-a, fromInt(3)));
   emitf(mul(a, fromInt(n)));
   emitf(mul(fromInt(n), a));
   emitf(mul(a, fromInt(-3)));
   emitf(mul(fromInt(-2), a));
   iarr[1] = 9;
   emitf(mul(a, fromInt(iarr[1])));
   emitf(dv(a, fromInt(iarr[1])));
   for (int i = 0; i < 3; i++)
   {
      ff t = mul(fromInt(i), lit(0.5)) + fromInt(1);
      emitf(dv(mul(t, fromInt(i)), fromInt(3)));
   }
   printf("DONE\n");
   return 0;
}
