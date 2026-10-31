// same a10 temporary clobber, unreduced (f0's return expression and main's l1 updates)
// Generated from nested_call_a10_temp_11082.json by fuzz.py; the .json is the test.

external void emit(int v);
external int ext1(int v);
external int ext2(int a, int b);

int A[8];
uint8_t B[6];
int M[3, 4];
uint8_t N[4, 5];
int g0;
int g1;
int g2;

int nn(int v)
{
   if (v < 0)
      v = 0 - v;
   if (v < 0)
      v = 0;
   return v;
}

int ix(int v, int n)
{
   return nn(v) % n;
}

int dv(int v)
{
   return nn(v) % 7 + 1;
}

int f0(int p0)
{
   int l1 = B[2];
   l1 += (nn(g0) % dv(M[ix(M[1, 0], 3), 2]));
   if (g2 == M[0, ix(768, 4)])
   {
      int l2 = g2;
   }
   l1 = p0;
   return (nn(N[0, ix(A[ix(g2, 8)], 5)]) % (0 - (dv(p0) + 1)));
}

void main()
{
   g0 = 11;
   g1 = 22;
   g2 = 33;
   for (int z0 = 0; z0 < 8; z0++)
      A[z0] = ((z0 * 7) + 4);
   for (int z0 = 0; z0 < 6; z0++)
      B[z0] = ((z0 * 7) + 4);
   for (int z0 = 0; z0 < 3; z0++)
      for (int z1 = 0; z1 < 4; z1++)
         M[z0, z1] = ((z1 * 7) + 4);
   for (int z0 = 0; z0 < 4; z0++)
      for (int z1 = 0; z1 < 5; z1++)
         N[z0, z1] = ((z1 * 7) + 4);
   int l1 = ((g0 + (nn(16) % dv(432))) + A[3]);
   emit((l1 - (0 - 219)));
   l1 += (((l1 == 1475 ? 5 : (0 - 130)) % dv((nn(g1) % dv(l1)))) - (g0 - (nn(l1) % (0 - (dv(l1) + 1)))));
   l1 -= g0;
   emit(((f0((0 - 91)) + A[3]) - (g2 - (l1 + 4))));
   for (int i2 = 0; i2 < 5; i2++)
   {
      l1 = (((i2 < 4 ? 6 : l1) * (i2 / dv(l1))) % (0 - (dv(2) + 1)));
      emit(g0);
   }
   emit(g0);
   emit(g1);
   emit(g2);
   for (int z0 = 0; z0 < 8; z0++)
      emit(A[z0]);
   for (int z0 = 0; z0 < 6; z0++)
      emit(B[z0]);
   for (int z0 = 0; z0 < 3; z0++)
      for (int z1 = 0; z1 < 4; z1++)
         emit(M[z0, z1]);
   for (int z0 = 0; z0 < 4; z0++)
      for (int z1 = 0; z1 < 5; z1++)
         emit(N[z0, z1]);
}
