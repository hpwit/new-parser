// ffloat multiplies share SAR (ssai 16 dropped while SAR is known to be 16): variable shifts (ssl / wsr sar) and calls between multiplies must make the next multiply set it again; constants built as movi + slli (4.0, 0.75, -2048.0, -32768.0...) -- raw Q16.16 bits against a C model.
#include <stdio.h>
#include <stdint.h>
typedef int32_t ff;
static ff lit(double v) { double f = v * 65536.0; f = f < 0 ? f - 0.5 : f + 0.5; if (f <= -2147483648.0) return (ff)0x80000000; return (ff)f; }
static ff fromInt(int x) { return (ff)((uint32_t)x << 16); }
static ff mul(ff a, ff b) { return (ff)(((int64_t)a * b) >> 16); }
static void emit(int v) { printf("EMIT %d\n", v); }
static void emitf(ff v) { printf("EMIT %d\n", v); }
static int shifty(int v, int n) { return (int)((uint32_t)v << n) + (int)((uint32_t)v >> n); }
int main()
{
   ff x = lit(3.25), y = lit(-1.75);
   int n = 5, m = 3;
   for (int i = 0; i < 3; i++)
   {
      ff p = mul(x, y);
      int s = (int)((uint32_t)i << n);
      ff q = mul(p, x);
      int t = (int)(4000u >> m);
      ff r = mul(q, y);
      emit(s);
      emit(t);
      emitf(p);
      emitf(q);
      emitf(r);
      ff u = mul(x, y) + mul((ff)((uint32_t)x >> m), y);
      emitf(u);
      ff w = mul(x, x);
      int z = shifty(i + 1, m);
      w = mul(w, y);
      emit(z);
      emitf(w);
      ff chain = mul(mul(mul(mul(x, y), x), lit(0.5)), y);
      emitf(chain);
      x = x + lit(0.5);
   }
   ff c[10] = {fromInt(4), lit(0.75), lit(-1.5), fromInt(1024), fromInt(-2048), lit(-32768), lit(2047.5), lit(0.5), fromInt(3), lit(-0.25)};
   for (int k = 0; k < 10; k++)
   {
      emitf(c[k]);
      emitf(mul(c[k], x));
   }
   if (mul(x, y) <= fromInt(4))
      emit(1);
   else
      emit(0);
   printf("DONE\n");
   return 0;
}
