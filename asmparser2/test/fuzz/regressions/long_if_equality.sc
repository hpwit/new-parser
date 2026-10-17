// An if with == or != against a constant and a long body (the far-branch form) ran its body exactly when the condition was false.
external void emit(int v);
external int ext1(int v);
external int ext2(int a, int b);
int A[8];
int a;
void main()
{
   a = 3;
   if (a != 1)
   {
      A[0] = A[3] + ext1(0) * 2;
      A[1] = A[4] + ext1(1) * 3;
      A[2] = A[5] + ext1(2) * 4;
      A[3] = A[6] + ext1(3) * 5;
      A[4] = A[7] + ext1(4) * 6;
      A[5] = A[0] + ext1(5) * 7;
      A[6] = A[1] + ext1(6) * 8;
      A[7] = A[2] + ext1(7) * 9;
      A[0] = A[3] + ext1(8) * 10;
      A[1] = A[4] + ext1(9) * 11;
      emit(1);
   }
   if (a == 1)
   {
      A[0] = A[3] + ext1(0) * 2;
      A[1] = A[4] + ext1(1) * 3;
      A[2] = A[5] + ext1(2) * 4;
      A[3] = A[6] + ext1(3) * 5;
      A[4] = A[7] + ext1(4) * 6;
      A[5] = A[0] + ext1(5) * 7;
      A[6] = A[1] + ext1(6) * 8;
      A[7] = A[2] + ext1(7) * 9;
      A[0] = A[3] + ext1(8) * 10;
      A[1] = A[4] + ext1(9) * 11;
      emit(2);
   }
   if (a == 3)
   {
      A[0] = A[3] + ext1(0) * 2;
      A[1] = A[4] + ext1(1) * 3;
      A[2] = A[5] + ext1(2) * 4;
      A[3] = A[6] + ext1(3) * 5;
      A[4] = A[7] + ext1(4) * 6;
      A[5] = A[0] + ext1(5) * 7;
      A[6] = A[1] + ext1(6) * 8;
      A[7] = A[2] + ext1(7) * 9;
      A[0] = A[3] + ext1(8) * 10;
      A[1] = A[4] + ext1(9) * 11;
      emit(3);
   }
   for (int i = 0; i < 8; i++)
      emit(A[i]);
}
