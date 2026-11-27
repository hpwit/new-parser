external void emit(int v);
uint16_t a[400];
s_int b[200];
uint32_t seed = 99;

int rnd(int n)
{
   seed = seed * 1103515245 + 12345;
   return ((seed >> 16) & 32767) % n;
}

// a span of a, the shape of a polygon's span fill
void span(int o, int x0, int x1, int c)
{
   for (int x = x0; x < x1; x++) a[o + x] = c;
}

int sumA()
{
   int s = 0;
   for (int h = 0; h < 400; h++)
      s = s + a[h] * (h % 37 + 1);
   return s;
}

int main()
{
   int live = 7;
   for (int t = 0; t < 300; t++)
   {
      int o = rnd(100);
      int x0 = rnd(120) - 10;
      int x1 = x0 + rnd(80) - 10;
      if (o + x0 < 0) x0 = 0 - o;
      if (o + x1 > 400) x1 = 400 - o;
      int c = rnd(70000) - 3000;
      span(o, x0, x1, c);
      live = live + t;
      if (t % 30 == 29) emit(sumA() + live);
   }
   // rows of a 2-row image: the row offset and the column bounds vary
   for (int y = 0; y < 2; y++)
   {
      int o = y * 200;
      int lo = y + 3;
      int hi = 190 - y;
      int v = y + 1000;
      for (int x = lo; x < hi; x++) a[o + x] = v;
      for (int x = 0; x < 1; x++) a[x + o] = 5;
   }
   emit(sumA());
   // a signed 16-bit array, x as the whole index, a number as the value
   for (int k = 0; k < 13; k++)
   {
      int hi = 200 - k * 3;
      int v = 0 - k;
      v = v - 5;
      for (int i = k; i < hi; i++) b[i] = v;
      int s = 0;
      for (int h = 0; h < 200; h++) s = s + b[h] * (h + 1);
      emit(s);
   }
   emit(live);
}
