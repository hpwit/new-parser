// ffloat fast paths: division with divisors below/above 128.0 (two 8-bit quou rounds vs bit loop), by zero, by integer literals (quos), integer multipliers on either side (mull) -- raw Q16.16 bits against a C model.
external void emit(int v);
external void emitf(ffloat v);
int iarr[3];

ffloat divide(ffloat a, ffloat b)
{
   return a / b;
}

void main()
{
   ffloat a = 1000.123;
   ffloat sizes[6];
   sizes[0] = 0.003;
   sizes[1] = 1.7;
   sizes[2] = 127.99;
   sizes[3] = 128;
   sizes[4] = 5000.5;
   sizes[5] = -300.25;
   for (int k = 0; k < 6; k++)
   {
      emitf(divide(a, sizes[k]));
      emitf(divide(-a, sizes[k]));
      emitf(divide(0.5, sizes[k]));
   }
   emitf(divide(30000, -0.75));
   int zero = 0;
   int n = 7;
   emitf(a / zero);
   emitf(a / n);
   emitf(a / 7);
   emitf(a / -7);
   emitf(-a / 3);
   emitf(a * n);
   emitf(n * a);
   emitf(a * -3);
   emitf(-2 * a);
   iarr[1] = 9;
   emitf(a * iarr[1]);
   emitf(a / iarr[1]);
   for (int i = 0; i < 3; i++)
   {
      ffloat t = i * 0.5 + 1;
      emitf(t * i / 3);
   }
}
