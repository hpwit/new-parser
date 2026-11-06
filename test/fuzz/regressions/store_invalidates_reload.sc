// l13 -= l13 stored with s32i a15,a1,N; the next l32i a14,a1,N was dropped as a repeat of the one before it
// Generated from store_invalidates_reload.json by fuzz.py; the .json is the test.

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
   return 1;
}

int f1(int p0, int p1, int p2, int p3, int p4)
{
   return 1;
}

int f2(int p0)
{
   return 1;
}

int f3(int p0, int p1)
{
   return 1;
}

int f4(int p0, int p1)
{
   return 1;
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
   if (1 <= 1)
   {
      if ((1 != 1) || (1 > 1))
      {
      }
      else
         for (int i2 = 0; i2 < 1; i2++)
         {
            int l3 = 1;
         }
   }
   else if (1 == 1)
   {
   }
   else if ((1 >= 1) || ((1 != 1) && (1 != 1)))
   {
   }
   else if (((1 != 1) || (1 == 1)) || (1 >= 1))
   {
      int l11 = 1;
   }
   int l13 = 1;
   l13 -= l13;
   A[1] = (1 * l13);
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
