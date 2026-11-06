// switch: fallthrough between cases, default in the middle falling into later cases, beqi and beq (large/negative) case values.
#include <stdio.h>
#include <stdint.h>
// Must match runner_fuzz.c's host functions.
static void emit(int v) { printf("EMIT %d\n", v); }
int classify(int v)
{
   int r = 0;
   switch (v)
   {
   case 1:
      r = 10;
      break;
   case 2:
   case 3:
      r = 20;
      break;
   default:
      r = 99;
   case -5:
      r = r + 30;
   case 1000:
      r = r + 1;
      break;
   case 70000:
      r = 7;
   }
   return r;
}
int main()
{
   for (int i = -6; i < 5; i++)
      emit(classify(i));
   emit(classify(1000));
   emit(classify(70000));
   emit(classify(-70000));
   printf("DONE\n");
   return 0;
}
