// ix(g2, 6)'s a11 renamed to a spare across the call that reads it (a11 spilled after the call); ix divided by a stale a11
// Generated from call_argument_not_renamed.json by fuzz.py; the .json is the test.

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

int f0(int p0, int p1, int p2)
{
   p2 += M[ix(ext1(p2), 3), 0];
   return 7;
}

int f1()
{
   int l1 = ext1((g2 == 235 ? (172820 % (0 - (dv(g2) + 1))) : 1204));
   if ((f0(4, g0, l1) >= B[ix(l1, 6)]) || (f0(g2, g2, 87564) > g0))
   {
      if ((18 + (1357 - l1)) != (ext1(g2) / dv((13 > g2 ? 3 : l1))))
         l1 = l1;
      else
      {
         l1 += (13 > ext2(g1, 610) ? ext1((5 + g2)) : f0(f0(163252, 3, 256), (g1 + (0 - 14)), M[ix(0, 3), ix(l1, 4)]));
         l1 = l1;
      }
      for (int i2 = 0; i2 < 3; i2++)
      {
         for (int i3 = 0; i3 < 5; i3++)
         {
            l1 = f0((A[6] - g2), A[i2], ((nn((0 - 290)) % dv(189644)) + (15 < i3 ? 9 : 16)));
            l1 = g0;
         }
         int l4 = l1;
         int l5 = l1;
      }
   }
   else if (((g1 > g0) && (g0 > g2)) || (l1 > (g2 + 161253)))
   {
      int l6 = f0(g2, N[ix(g0, 4), 2], g1);
      for (int i7 = 0; i7 < 2; i7++)
      {
         l1 -= l1;
      }
      l1 = 19;
   }
   else if ((A[ix(l1, 8)] <= (g0 + 1626)) || (A[2] <= (g1 < g1 ? l1 : (0 - 221))))
   {
      l1 = ((g2 / dv((16 <= (0 - 49) ? 970 : 1))) + g2);
      int l8 = (nn(3) % dv(M[ix(g2, 3), ix(ext2(g2, l1), 4)]));
      for (int i9 = 0; i9 < 5; i9++)
      {
         l8 = (M[ix(g0, 3), 0] * N[3, ix(g1, 5)]);
      }
   }
   else
   {
      l1 = M[ix(g0, 3), 3];
   }
   return (5 * M[ix(g2, 3), 0]);
}

int f2()
{
   emit(ext2((g2 >= g0 ? (nn(g2) % dv(g2)) : g0), ((g0 < g2 ? 18 : g0) + N[ix(14, 4), 2])));
   B[1] = B[5];
   return A[6];
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
   g2 -= (1887 - (B[ix(g2, 6)] % (0 - (dv(N[ix(5, 4), 3]) + 1))));
   for (int i1 = 0; i1 < 5; i1++)
   {
      emit(g0);
      emit(f1());
      B[ix(0, 6)] = g2;
   }
   N[ix(ext2(ext1(g2), A[4]), 4), 1] = 1811;
   for (int i2 = 0; i2 < 6; i2++)
   {
      int l3 = (0 - 285);
      g1 = i2;
      l3 -= g0;
   }
   N[3, 0] = A[ix(20, 8)];
   emit((N[0, 0] > A[5] ? (A[6] / dv(f0(1781, (0 - 200), 1285))) : M[1, ix(N[0, 1], 4)]));
   emit((0 - 9));
   A[ix(g1, 8)] = (0 - 137);
   emit(g2);
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
