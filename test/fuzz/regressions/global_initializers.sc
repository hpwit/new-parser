// The first global initializer was stored before its address was loaded, and initializers were not protected across calls.
external void emit(int v);
external int ext1(int v);
external int ext2(int a, int b);
int g = 3 + ext1(4);
int h = ext1(1) * 10 + ext1(2);
int k = 7;
void main()
{
   emit(g);
   emit(h);
   emit(k);
}
