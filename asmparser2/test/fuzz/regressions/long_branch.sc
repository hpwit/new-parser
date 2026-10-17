// A loop body longer than 128 bytes: its closing short branch (blti) was encoded with a truncated offset and jumped into the middle of another function. Reduced from seed 2032.
// Generated from long_branch.json by fuzz.py; the .json is the test.

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
   for (int i1 = 0; i1 < 2; i1++)
   {
      M[ix((1 % dv((nn(1) % dv(1)))), 1), 1] = 1;
   }
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
   f0(1, 1, 1);
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
