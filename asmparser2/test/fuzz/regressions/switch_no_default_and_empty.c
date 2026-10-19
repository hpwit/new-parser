// switch: no case matching and no default, and an empty switch body.
#include <stdio.h>
#include <stdint.h>
// Must match runner_fuzz.c's host functions.
static void emit(int v) { printf("EMIT %d\n", v); }
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
int main()
{
   emit(f(4));
   emit(f(3));
   printf("DONE\n");
   return 0;
}
