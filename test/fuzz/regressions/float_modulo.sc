// % between integers in an expression stored in a float: it gave the left value (float f = 57 % 13 was 57); now the integer remainder, then a float, as in C.
external void emit(int v);
float g;
float ga[3];
uint32_t u = 4000000001;

float half(float x)
{
   return x / 2;
}

void main()
{
   int i = 57;
   int k = 13;
   float f = i % 13;
   emit((int)(f * 1000));
   g = i % k;
   emit((int)(g * 1000));
   ga[1] = 100 % (k - 6);
   emit((int)(ga[1] * 1000));
   float s = 40 + (i % k) * 8 + 4;
   emit((int)(s * 1000));
   float n = (i + 9) % (k * 2);
   emit((int)(n * 1000));
   float neg = -i % k;
   emit((int)(neg * 1000));
   float big = u % 1000;
   emit((int)(big * 1000));
   float mix = 1.5 + i % 10;
   emit((int)(mix * 1000));
   emit((int)(half(i % 7) * 1000));
   int plain = i % k;
   emit(plain);
   for (int t = 0; t < 20; t++)
   {
      float w = (t * 7) % 5;
      emit((int)(w * 10));
   }
}
