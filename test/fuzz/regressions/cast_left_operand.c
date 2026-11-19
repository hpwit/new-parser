// a cast as the left operand of an operator: ((int)(a * 64.0)) & 63 lost the & (gave 672), and the same with + | >> << % - * / and comparisons.
#include <stdio.h>
static void emit(int v) { printf("EMIT %d\n", v); }
int tab[8] = {10, 11, 12, 13, 14, 15, 16, 17};
float gf;
int main()
{
   float a = 10.5f;
   int k = 3;
   int v = ((int)(a * 64.0f)) & 63;
   emit(v);
   emit(((int)(a * 64.0f)) & 63);
   emit(((int)(a * 64.0f)) + k);
   emit(((int)(a * 64.0f)) | 1);
   emit(((int)(a * 64.0f)) >> 2);
   emit(((int)(a * 64.0f)) << 1);
   emit(((int)(a * 64.0f)) % 64);
   emit(((int)(a * 64.0f)) - 600);
   emit(((int)(a * 4.0f)) * 3);
   emit(((int)(a * 4.0f)) / 5);
   emit(k + (int)(a * 4.0f));
   emit(((int)(a)) + ((int)(a * 2.0f)));
   if (((int)(a * 4.0f)) > 40) emit(1);
   if (((int)(a * 4.0f)) == 42) emit(2);
   if (((int)(a * 4.0f)) < 40) emit(3);
   emit(tab[((int)(a * 4.0f)) & 7]);
   gf = 2.25f;
   int w = ((int)(gf * 8.0f)) & 15;
   emit(w);
   int s = 0;
   for (int i = 0; i < 4; i++)
      s = s + (((int)(gf * i)) & 3);
   emit(s);
   printf("DONE\n");
   return 0;
}
