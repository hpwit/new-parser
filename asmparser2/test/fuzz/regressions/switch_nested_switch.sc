// switch inside a case of another switch; break and return from the inner one.
external void emit(int v);
int g(int a, int b)
{
   switch (a)
   {
   case 0:
      switch (b)
      {
      case 0:
         return 1;
      case 1:
         break;
      default:
         return 3;
      }
      return 4;
   case 1:
      return 5 + b;
   }
   return -1;
}
void main()
{
   for (int a = 0; a < 3; a++)
      for (int b = 0; b < 3; b++)
         emit(g(a, b));
}
