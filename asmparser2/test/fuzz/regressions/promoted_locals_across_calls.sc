// Locals kept in registers by promoteFrameSlots(): float locals in f3..f12 saved/restored around every call (script calls, host calls, ___div) inside loops, int locals in a4..a7 across calls and recursion, more float locals than free registers, carry()'s m and k read only in an inner loop before the calls (only the save around each call keeps them, wobble() overwrites f3/f4), a float passed to a function -- results against a C model.
external void emit(int v);

float scale(float v, int k)
{
   float a = v * 2;
   float b = a / k;
   return b + 0.5;
}

int depth(int n, int acc)
{
   int x = n * 3;
   int y = acc + x;
   if (n == 0)
      return y;
   int r = depth(n - 1, y);
   return r + x - y;
}

float wobble(float v)
{
   float a = v;
   float b = 0.5;
   for (int i = 0; i < 4; i++)
      a = a * b + 1;
   return a;
}

float carry(int n)
{
   float m = 1.25;
   float k = 0.5;
   float r = 0;
   for (int i = 0; i < n; i++)
   {
      for (int j = 0; j < 4; j++)
         r = r * k + m;
      r = r + wobble(r);
      emit((int)(r * 1000));
   }
   return r;
}

void main()
{
   float p = 1.5;
   float q = -2.25;
   float r = 0;
   float s = 0.75;
   float t = 3;
   float u = 0;
   float v = 10;
   float w = -1;
   float x = 0.125;
   float y = 7;
   float z = 0;
   float m = 1.25;
   int count = 0;
   int total = 0;
   for (int i = 1; i < 12; i++)
   {
      r = p * q * m + s * m - m * m + m;
      u = scale(r, i);
      emit((int)(u * 1000));
      z = r / i + t;
      p = p + 0.25;
      q = q - z / 8;
      emit((int)(q * 1000));
      v = v / 2 + w * x;
      y = y - v;
      count++;
      total = total + depth(i % 4, count);
      emit(total);
      emit((int)((p + q + r + s + t + u + v + w + x + y + z) * 100));
   }
   emit(count);
   emit((int)(carry(5) * 1000));
}
