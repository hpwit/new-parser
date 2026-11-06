// ffloat / int divisor (variable, array element, call, uint8_t) is a quos with a check for 0 (saturates like ___ffdiv); ffloat -> int truncates toward zero with srai + extui ..,0,16 -- raw Q16.16 bits and ints against a C model.
#include <stdio.h>
#include <stdint.h>
typedef int32_t ff;
static ff lit(double v) { double f = v * 65536.0; f = f < 0 ? f - 0.5 : f + 0.5; if (f <= -2147483648.0) return (ff)0x80000000; return (ff)f; }
static ff fromInt(int x) { return (ff)((uint32_t)x << 16); }
static ff mul(ff a, ff b) { return (ff)(((int64_t)a * b) >> 16); }
static ff dvi(ff a, int n)
{
   if (n == 0) return a < 0 ? -0x7fffffff : 0x7fffffff;
   return a / n;
}
static int toInt(ff a) { return a / 65536; }
static void emit(int v) { printf("EMIT %d\n", v); }
static void emitf(ff v) { printf("EMIT %d\n", v); }
int iarr[4];
uint8_t small;
static int pick(int k) { return iarr[k]; }
int main()
{
   iarr[0] = 0; iarr[1] = 7; iarr[2] = -3; iarr[3] = 144;
   small = 12;
   ff vals[5] = {lit(1000.123), lit(-0.75), 0, lit(-31000.5), lit(2.5)};
   for (int i = 0; i < 5; i++)
   {
      ff a = vals[i];
      for (int k = 0; k < 4; k++)
      {
         int n = iarr[k];
         emitf(dvi(a, n));
         emitf(dvi(a, iarr[k]));
         emitf(dvi(a, pick(k)));
         emitf(dvi(-a, n) + fromInt(1));
      }
      emitf(dvi(a, small));
      emit(toInt(a));
      emit(toInt(mul(a, fromInt(3)) - lit(0.5)));
   }
   ff neg[6] = {lit(-0.00002), fromInt(-1), lit(-1.5), lit(-32768), lit(-32767.99), lit(32767.99)};
   for (int k = 0; k < 6; k++)
      emit(toInt(neg[k]));
   int zero = 0;
   ff q = fromInt(-5);
   emitf(dvi(q, zero));
   emitf(dvi(0, zero));
   printf("DONE\n");
   return 0;
}
