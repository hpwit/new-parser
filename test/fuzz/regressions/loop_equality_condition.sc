// A for loop whose condition is != or == against a constant looped while it was false.
external void emit(int v);
external int ext1(int v);
external int ext2(int a, int b);
void main()
{
   for (int i = 0; i != 5; i++)
      emit(i);
   int n = 0;
   for (int j = 0; j == 0; j++)
      n = n + 1;
   emit(n);
}
