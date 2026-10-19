// ffloat: literals and folded constants, * / % ^, negatives, division by zero, int/float conversions, functions, globals, arrays, comparisons -- raw Q16.16 bits against a C model.
external void emit(int v);
external void emitf(ffloat v);
ffloat g;
ffloat arr[4];

ffloat half(ffloat x)
{
   return x / 2;
}

ffloat lerp(ffloat a, ffloat b, ffloat t)
{
   return a + (b - a) * t;
}

int toInt(ffloat x)
{
   return x;
}

float toFloat(ffloat x)
{
   return x;
}

void main()
{
   ffloat m = 2.3/4 + 2;
   emitf(m);
   ffloat k = m * 0.5;
   emitf(k);
   emitf(k / m);
   emitf(m * m);
   emitf(m ^ 3);
   ffloat neg = -7.25;
   emitf(neg);
   emitf(neg * 3);
   emitf(neg / 3);
   emitf(neg / -0.5);
   emit(toInt(neg));
   emit(toInt(m));
   int i = 5;
   emitf(m + i);
   emitf(i / 2);
   ffloat z = 1 / 3.0;
   emitf(z);
   emitf(half(m));
   emitf(lerp(1, 3, 0.25));
   g = m - 0.75;
   emitf(g);
   for (int j = 0; j < 4; j++)
      arr[j] = j * 0.5 + m;
   emitf(arr[3] - arr[1]);
   if (m > 2.5)
      emit(1);
   if (m < 2.5)
      emit(2);
   if (2.6 > m)
      emit(3);
   if (neg <= -7.25)
      emit(4);
   int r = (i < m) ? 10 : 20;
   emit(r);
   float f = toFloat(m);
   emit((int)(f * 1000));
   ffloat back = f;
   emitf(back);
   ffloat acc = 0;
   for (int j = 1; j <= 10; j++)
      acc = acc + 1.0 / j;
   emitf(acc);
   emitf(m % 1);
   emitf(neg % 2);
   ffloat big = 30000.5;
   emitf(big);
   emitf(big / 1000);
   emitf(m / 0);
}
