// ffloat: for/while with ffloat conditions, += -= *= /= ++ --, 2D arrays, abs, a float function, a division between calls -- raw Q16.16 bits against a C model.
external void emit(int v);
external void emitf(ffloat v);
ffloat grid[3, 4];

float twice(float x)
{
   return x * 2;
}

ffloat scale(ffloat v, int n)
{
   return v * n + 0.125;
}

void main()
{
   ffloat x;
   int steps = 0;
   for (x = 0; x < 2; x = x + 0.25)
      steps++;
   emit(steps);
   emitf(x);
   ffloat y = 10;
   while (y > 0.5)
      y = y / 3;
   emitf(y);
   ffloat a = 1.5;
   a += 0.25;
   emitf(a);
   a -= 3;
   emitf(a);
   a *= -2.5;
   emitf(a);
   a /= 4;
   emitf(a);
   a++;
   emitf(a);
   a--;
   a--;
   emitf(a);
   for (int r = 0; r < 3; r++)
      for (int c = 0; c < 4; c++)
         grid[r, c] = r * 1.5 - c * 0.25;
   emitf(grid[2, 3]);
   emitf(grid[1, 0] + grid[0, 2]);
   ffloat n = -3.75;
   emitf(abs(n));
   float t = twice(n);
   emit((int)(t * 100));
   emitf(0x10 / -0.25);
   emitf(scale(a, 3) / scale(1.25, 2) + scale(0.5, 4) * 2);
   int k = 7;
   ffloat w = k / 2 + n;
   emitf(w);
}
