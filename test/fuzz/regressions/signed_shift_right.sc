// >> on a negative int keeps the sign, as in C (-54 >> 1 is -27, it gave 2147483621); uint32_t still shifts in zeros.
external void emit(int v);
int g[4] = {-54, 54, -1, -1000000};
uint32_t ug;
int gi;

int half(int v)
{
   return v >> 1;
}

void main()
{
   int m = -54;
   int n = 3;
   emit(m >> 1);
   int r = m >> 2;
   emit(r);
   emit(m >> n);
   emit((m - 10) >> 2);
   emit((m * 3) >> 3);
   emit(half(-7));
   emit(g[0] >> 1);
   emit(g[3] >> 10);
   emit(g[2] >> 31);
   emit((g[3] >> 4) & 255);
   gi = -100;
   gi = gi >> 2;
   emit(gi);
   if ((m >> 1) < 0)
      emit(1);
   emit(54 >> 2);
   uint32_t u = 4000000000;
   emit(u >> 4);
   emit((u + 1) >> 8);
   ug = 3000000000;
   ug = ug >> 1;
   emit(ug);
   emit((ug * 2) >> 28);
   uint8_t b = 200;
   emit(b >> 3);
   uint16_t w = 60000;
   emit(w >> 4);
   int s = 0;
   for (int i = -8; i < 8; i++)
      s = s * 3 + (i >> 1);
   emit(s);
}
