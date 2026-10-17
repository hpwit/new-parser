// `%` was always unsigned: -7 % 3 gave 0 instead of -1.
external void emit(int v);
external int ext1(int v);
external int ext2(int a, int b);
int a;
int b;
void main()
{
   a = 0 - 7;
   b = 3;
   emit(a % b);
   emit(7 % (0 - 3));
   emit((0 - 7) % (0 - 3));
   emit(a / b);
}
