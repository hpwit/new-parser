// for (x in a) / for (int x : a) over arrays sized by their initializer (int a[] = {...}), break, continue, nested, existing variable
external void emit(int v);
int arr[] = {5, 6, 7, 8};
uint8_t small[] = {200, 100, 3};
float fl[] = {1.5, -2.25, 4.0};
int tbl[6] = {1, 2, 3};
int g;
int sum(int k)
{
   int s = 0;
   for (v in arr)
      s = s + v * k;
   return s;
}
void main()
{
   for (i in arr)
      emit(i);
   for (int x : arr) { emit(x * 10); }
   for (int b in small) emit(b);
   for (f in fl) { emit(f * 4); }
   for (x in arr)
   {
      if (x == 6) continue;
      if (x == 8) break;
      emit(x + 100);
   }
   for (a in arr)
      for (b in small)
         emit(a * 1000 + b);
   g = 0;
   for (g in tbl) emit(g);
   emit(g);
   emit(sum(3));
   int n = 0;
   for (y in tbl) { n = n + y; }
   emit(n);
}
