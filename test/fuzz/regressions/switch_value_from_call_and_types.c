// switch on a call's result, on a uint8_t global, with #define'd and hex case values.
#include <stdio.h>
#include <stdint.h>
// Must match runner_fuzz.c's host functions.
static void emit(int v) { printf("EMIT %d\n", v); }
#define MODE_A 3
#define MODE_B 12
int pick(int x)
{
   return x * 3;
}
uint8_t small;
int gl;
int main()
{
   for (int x = 0; x < 6; x++)
   {
      switch (pick(x) + 1)
      {
      case 1:
         emit(100);
         break;
      case 4:
         emit(400);
         break;
      case 16:
         emit(1600);
         break;
      default:
         emit(pick(x));
      }
   }
   small = 255;
   switch (small)
   {
   case 255:
      emit(1);
      break;
   case -1:
      emit(2);
      break;
   }
   gl = MODE_B;
   switch (gl)
   {
   case MODE_A:
      emit(30);
      break;
   case MODE_B:
      emit(120);
      break;
   }
   int y = 0x10;
   switch (y)
   {
   case 0x10:
      emit(16);
      break;
   }
   printf("DONE\n");
   return 0;
}
