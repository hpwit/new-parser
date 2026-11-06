// switch inside a case of another switch; break and return from the inner one.
#include <stdio.h>
#include <stdint.h>
// Must match runner_fuzz.c's host functions.
static void emit(int v) { printf("EMIT %d\n", v); }
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
int main()
{
   for (int a = 0; a < 3; a++)
      for (int b = 0; b < 3; b++)
         emit(g(a, b));
   printf("DONE\n");
   return 0;
}
