// ffloat: literals and folded constants, * / % ^, negatives, division by zero, int/float conversions, functions, globals, arrays, comparisons -- raw Q16.16 bits against a C model.
#include <stdio.h>
#include <stdint.h>
#include <math.h>
typedef int32_t ff;
static ff lit(double v) { double f = v * 65536.0; f = f < 0 ? f - 0.5 : f + 0.5; return (ff)f; }
static ff fromInt(int x) { return (ff)((uint32_t)x << 16); }
static int toIntC(ff x) { return x / 65536; }
static ff mul(ff a, ff b) { return (ff)(((int64_t)a * b) >> 16); }
static ff dv(ff a, ff b)
{
   if (b == 0) return ((a ^ b) < 0) ? -0x7fffffff : 0x7fffffff;
   return (ff)(((int64_t)a << 16) / b);
}
static ff fromFloat(float f) { return (ff)(int32_t)(f * 32768.0f) * 2; }
static float toFloatC(ff x) { return (float)(x >> 1) * (1.0f / 32768.0f); }
static void emit(int v) { printf("EMIT %d\n", v); }
static void emitf(ff v) { printf("EMIT %d\n", v); }
ff g; ff arr[4];
static ff half(ff x) { return dv(x, fromInt(2)); }
static ff lerp(ff a, ff b, ff t) { return a + mul(b - a, t); }
int main()
{
   ff m = lit(2.3 / 4 + 2);
   emitf(m);
   ff k = mul(m, lit(0.5));
   emitf(k);
   emitf(dv(k, m));
   emitf(mul(m, m));
   emitf(mul(mul(m, m), m));
   ff neg = lit(-7.25);
   emitf(neg);
   emitf(mul(neg, lit(3)));
   emitf(dv(neg, lit(3)));
   emitf(dv(neg, lit(-0.5)));
   emit(toIntC(neg));
   emit(toIntC(m));
   int i = 5;
   emitf(m + fromInt(i));
   emitf(dv(fromInt(i), lit(2)));
   ff z = lit(1 / 3.0);
   emitf(z);
   emitf(half(m));
   emitf(lerp(lit(1), lit(3), lit(0.25)));
   g = m - lit(0.75);
   emitf(g);
   for (int j = 0; j < 4; j++)
      arr[j] = mul(fromInt(j), lit(0.5)) + m;
   emitf(arr[3] - arr[1]);
   if (m > lit(2.5)) emit(1);
   if (m < lit(2.5)) emit(2);
   if (lit(2.6) > m) emit(3);
   if (neg <= lit(-7.25)) emit(4);
   int r = fromInt(i) < m ? 10 : 20;
   emit(r);
   float f = toFloatC(m);
   emit((int)(f * 1000));
   ff back = fromFloat(f);
   emitf(back);
   ff acc = 0;
   for (int j = 1; j <= 10; j++)
      acc = acc + dv(lit(1.0), fromInt(j));
   emitf(acc);
   emitf(m % lit(1));
   emitf(neg % lit(2));
   ff big = lit(30000.5);
   emitf(big);
   emitf(dv(big, lit(1000)));
   emitf(dv(m, 0));
   printf("DONE\n");
   return 0;
}
