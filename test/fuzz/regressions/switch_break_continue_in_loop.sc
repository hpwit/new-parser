// switch in loops: break leaves the switch, continue goes to the enclosing loop.
external void emit(int v);
void main()
{
   int sum = 0;
   for (int i = 0; i < 12; i++)
   {
      switch (i % 4)
      {
      case 0:
         continue;
      case 1:
         sum = sum + 1;
         break;
      case 2:
         if (i > 8)
            break;
         sum = sum + 100;
      default:
         sum = sum + 1000;
      }
      sum = sum + 10000;
   }
   emit(sum);
   int n = 0;
   while (n < 100)
   {
      switch (n)
      {
      case 7:
         n = 1000;
         break;
      default:
         n = n + 1;
      }
   }
   emit(n);
}
