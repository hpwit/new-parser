// ffloat / +-2^k literal is a shift truncated toward zero (srai, extui ..,0,k or srli for k > 16, add, srai, neg for a negative divisor) instead of quos -- 2..2^30, 1 and -1, negative, tiny (raw -1) and extreme dividends, raw Q16.16 bits against a C model.
#include <stdio.h>
#include <stdint.h>
typedef int32_t ff;
static ff lit(double v) { double f = v * 65536.0; f = f < 0 ? f - 0.5 : f + 0.5; if (f <= -2147483648.0) return (ff)0x80000000; if (f >= 2147483647.0) return 0x7fffffff; return (ff)f; }
static ff fromInt(int x) { return (ff)((uint32_t)x << 16); }
static ff dvi(ff a, int n) { return (ff)((int64_t)a / n); }
static void emitf(ff v) { printf("EMIT %d\n", v); }
int main()
{
   ff vals[8] = {lit(1000.123), lit(-1000.123), lit(-0.00002), lit(0.00002), lit(-32768), lit(32767.99), lit(-3.5), 0};
   for (int i = 0; i < 8; i++)
   {
      ff a = vals[i];
      emitf(dvi(a, 1));
      emitf((ff)(0u - (uint32_t)a));
      emitf(dvi(a, 2));
      emitf(dvi(a, -2));
      emitf(dvi(a, 8));
      emitf(dvi(a, 128));
      emitf(dvi(a, -128));
      emitf(dvi(a, 1024));
      emitf(dvi(a, 65536));
      emitf(dvi(a, -65536));
      emitf(dvi(a, 131072));
      emitf(dvi(a, 1048576));
      emitf(dvi(a, 1073741824));
      emitf(dvi(a, 0x4000));
      emitf(dvi(a + fromInt(1), 4) - dvi(a, 16));
   }
   printf("DONE\n");
   return 0;
}
