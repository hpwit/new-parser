// An argument kept in a15 across a call made for another argument was not saved: calls were not seen as reading their argument registers.
external void emit(int v);
external int ext1(int v);
external int ext2(int a, int b);
int f(int a, int b, int c, int d, int e, int g)
{
   return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * g;
}
void main()
{
   emit(f(ext1(1), 2, 3, 4, f(1, 1, 1, 1, 1, ext1(2)), ext2(5, 1)));
}
