// switch with braced case bodies declaring their own locals, and a bare { } block.
#include <stdio.h>
#include <stdint.h>
// Must match runner_fuzz.c's host functions.
static void emit(int v) { printf("EMIT %d\n", v); }
int h(int v)
{
   int x = 1;
   switch (v)
   {
   case 1:
   {
      int t = v * 7;
      x = t + 1;
      break;
   }
   case 2:
   {
      int t = 100;
      {
         int u = t + v;
         x = u;
      }
      break;
   }
   default:
   {
      int t = -1;
      x = t;
   }
   }
   return x;
}
int main()
{
   for (int v = 0; v < 4; v++)
      emit(h(v));
   int a = 5;
   {
      int a2 = a * 2;
      a = a2 + 1;
   }
   emit(a);
   printf("DONE\n");
   return 0;
}
