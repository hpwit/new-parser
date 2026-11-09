// __ASM__ functions (a label starting with b, which crashed the optimizer): integer arguments in a2..a7 and the result in a2, a float argument (its bits) and result (f2), a global through l32r @_name, a loop with a label, and min/max/minu/maxu, nsau, clamps, sext, mul16u/mul16s.
external void emit(int v);
int counter;

__ASM__ int addTwo(int a, int b)
{
"entry a1,32"
"add a2,a2,a3"
"retw.n"
}

__ASM__ void bump(int by)
{
"entry a1,32"
"l32r a4,@_counter"
"l32i a5,a4,0"
"add a5,a5,a2"
"s32i a5,a4,0"
"retw.n"
}

__ASM__ int sumTo(int n)
{
"entry a1,32"
"movi a3,0"
"back_sum:"
"add a3,a3,a2"
"addi a2,a2,-1"
"bnez a2,back_sum"
"mov a2,a3"
"retw.n"
}

__ASM__ float scale(float x, int k)
{
"entry a1,32"
"wfr f0,a2"
"float.s f1,a3,0"
"mul.s f2,f0,f1"
"retw.n"
}

__ASM__ int leadingZeros(uint32_t v)
{
"entry a1,32"
"nsau a2,a2"
"retw.n"
}

__ASM__ int smallest(int a, int b)
{
"entry a1,32"
"min a2,a2,a3"
"retw.n"
}

__ASM__ int largest(int a, int b)
{
"entry a1,32"
"max a2,a2,a3"
"retw.n"
}

__ASM__ uint32_t smallestU(uint32_t a, uint32_t b)
{
"entry a1,32"
"minu a2,a2,a3"
"retw.n"
}

__ASM__ uint32_t largestU(uint32_t a, uint32_t b)
{
"entry a1,32"
"maxu a2,a2,a3"
"retw.n"
}

__ASM__ int clampByte(int a)
{
"entry a1,32"
"clamps a2,a2,7"
"retw.n"
}

__ASM__ int asByte(int a)
{
"entry a1,32"
"sext a2,a2,7"
"retw.n"
}

__ASM__ uint32_t low16u(uint32_t a, uint32_t b)
{
"entry a1,32"
"mul16u a2,a2,a3"
"retw.n"
}

__ASM__ int low16s(int a, int b)
{
"entry a1,32"
"mul16s a2,a2,a3"
"retw.n"
}

void main()
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
   emit((int)(scale(1.25, 6) * 100));
   emit(leadingZeros(1));
   emit(leadingZeros(65535));
   emit(leadingZeros(0));
   emit(smallest(-5, 3));
   emit(largest(-5, 3));
   emit(smallestU(4000000000, 3));
   emit(largestU(4000000000, 3));
   emit(clampByte(300));
   emit(clampByte(-300));
   emit(clampByte(42));
   emit(asByte(200));
   emit(asByte(100));
   emit(low16u(70000, 3));
   emit(low16s(65535, 2));
}
