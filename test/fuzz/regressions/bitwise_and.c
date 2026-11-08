// binary &: a condition on its own, in expressions, after a shift, on globals and arrays, mixed with | and with comparisons (it used to be read as an address-of and crash the compiler).
#include <stdio.h>
static void emit(int v) { printf("EMIT %d\n", v); }
int font[3] = {31599, 11415, 29671};
int g;
static int bit(int v, int n) { return (v >> n) & 1; }
int main()
{
   int bits = 45;
   if (bits & 1) emit(1);
   if (bits & 2) emit(2);
   while (bits & 1) bits = bits >> 1;
   emit(bits);
   emit(bits & 6);
   g = font[1] & 255;
   emit(g);
   int count = 0;
   for (int r = 0; r < 5; r++)
      for (int b = 0; b < 3; b++)
         if ((font[0] >> ((4 - r) * 3 + 2 - b)) & 1)
            count++;
   emit(count);
   emit(bit(font[2], 4) + bit(font[2], 5));
   emit((bits & 3) | 8);
   int a = 7;
   if ((a > 3) & (a < 9)) emit(3);
   if ((a > 3) & (a > 9)) emit(4);
   _Bool q = a & 4;
   if (q) emit(5);
   if ((a & 8) == 0) emit(6);
   printf("DONE\n");
   return 0;
}
