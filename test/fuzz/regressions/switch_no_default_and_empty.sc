// switch: no case matching and no default, and an empty switch body.
external void emit(int v);
int f(int v)
{
   int r = 5;
   switch (v)
   {
   case 4:
      r = 1;
   }
   switch (v)
   {
   }
   return r;
}
void main()
{
   emit(f(4));
   emit(f(3));
}
