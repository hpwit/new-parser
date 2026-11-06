// Fifth arguments of an outer and a nested five-argument call shared one stack slot.
external void emit(int v);
external int ext1(int v);
external int ext2(int a, int b);
int f(int a, int b, int c, int d, int e)
{
   return a + 2 * b + 3 * c + 4 * d + 5 * e;
}
void main()
{
   emit(f(f(ext1(1), 2, 3, 4, ext2(1, 2)), ext1(3), 4, f(1, 2, 3, 4, 5), ext1(9)));
}
