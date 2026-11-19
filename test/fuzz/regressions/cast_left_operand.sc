// a cast as the left operand of an operator: ((int)(a * 64.0)) & 63 lost the & (gave 672), and the same with + | >> << % - * / and comparisons.
external void emit(int v);
int tab[8] = {10, 11, 12, 13, 14, 15, 16, 17};
float gf;

void main()
{
   float a = 10.5;
   int k = 3;
   int v = ((int)(a * 64.0)) & 63;
   emit(v);
   emit(((int)(a * 64.0)) & 63);
   emit(((int)(a * 64.0)) + k);
   emit(((int)(a * 64.0)) | 1);
   emit(((int)(a * 64.0)) >> 2);
   emit(((int)(a * 64.0)) << 1);
   emit(((int)(a * 64.0)) % 64);
   emit(((int)(a * 64.0)) - 600);
   emit(((int)(a * 4.0)) * 3);
   emit(((int)(a * 4.0)) / 5);
   emit(k + (int)(a * 4.0));
   emit(((int)(a)) + ((int)(a * 2.0)));
   if (((int)(a * 4.0)) > 40)
      emit(1);
   if (((int)(a * 4.0)) == 42)
      emit(2);
   if (((int)(a * 4.0)) < 40)
      emit(3);
   emit(tab[((int)(a * 4.0)) & 7]);
   gf = 2.25;
   int w = ((int)(gf * 8.0)) & 15;
   emit(w);
   int s = 0;
   for (int i = 0; i < 4; i++)
      s = s + (((int)(gf * i)) & 3);
   emit(s);
}
