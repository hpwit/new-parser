// A six-argument call nested in another overwrote the outer call's arguments held in a14/a15.
external void emit(int v);
external int ext1(int v);
external int ext2(int a, int b);
int f(int a, int b, int c, int d, int e, int g)
{
   return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * g;
}
void main()
{
   emit(f(f(1, 2, 3, 4, 5, 6), 7, 8, 9, 10, 11));
}
