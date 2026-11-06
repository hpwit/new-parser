// A temporary in a15 (y) was overwritten by the sixth argument of a call in the same expression.
external void emit(int v);
external int ext1(int v);
external int ext2(int a, int b);
int f(int a, int b, int c, int d, int e, int g)
{
   return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * g;
}
int y;
void main()
{
   y = 1000;
   emit(y + f(1, 2, 3, 4, 5, 6));
}
