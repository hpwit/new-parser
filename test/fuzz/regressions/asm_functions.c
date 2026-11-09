// __ASM__ functions (a label starting with b, which crashed the optimizer): integer arguments in a2..a7 and the result in a2, a float argument (its bits) and result (f2), a global through l32r @_name, a loop with a label, and min/max/minu/maxu, nsau, clamps, sext, mul16u/mul16s.
#include <stdio.h>
#include <stdint.h>
static void emit(int v) { printf("EMIT %d\n", v); }
int counter;
static int addTwo(int a, int b) { return a + b; }
static void bump(int by) { counter += by; }
static int sumTo(int n) { int s = 0; while (n) { s += n; n--; } return s; }
static float scale(float x, int k) { return x * (float)k; }
static int leadingZeros(uint32_t v) { return v ? __builtin_clz(v) : 32; }
static int smallest(int a, int b) { return a < b ? a : b; }
static int largest(int a, int b) { return a > b ? a : b; }
static uint32_t smallestU(uint32_t a, uint32_t b) { return a < b ? a : b; }
static uint32_t largestU(uint32_t a, uint32_t b) { return a > b ? a : b; }
static int clampByte(int a) { return a > 127 ? 127 : a < -128 ? -128 : a; }
static int asByte(int a) { return (int8_t)a; }
static uint32_t low16u(uint32_t a, uint32_t b) { return (a & 0xFFFF) * (b & 0xFFFF); }
static int low16s(int a, int b) { return (int16_t)a * (int16_t)b; }
int main()
{
   emit(addTwo(40, 2));
   counter = 5;
   bump(10);
   bump(7);
   emit(counter);
   emit(sumTo(10));
   int t = 0;
   for (int i = 0; i < 5; i++)
      t = t + addTwo(i, i * 10);
   emit(t);
   emit((int)(scale(1.25f, 6) * 100));
   emit(leadingZeros(1));
   emit(leadingZeros(65535));
   emit(leadingZeros(0));
   emit(smallest(-5, 3));
   emit(largest(-5, 3));
   emit((int)smallestU(4000000000u, 3));
   emit((int)largestU(4000000000u, 3));
   emit(clampByte(300));
   emit(clampByte(-300));
   emit(clampByte(42));
   emit(asByte(200));
   emit(asByte(100));
   emit((int)low16u(70000, 3));
   emit(low16s(65535, 2));
   printf("DONE\n");
   return 0;
}
