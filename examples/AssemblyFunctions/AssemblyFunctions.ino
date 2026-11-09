// Functions written in Xtensa assembly inside a script, next to C ones:
// `__ASM__` before a function's type, and its body is one quoted instruction
// per line. The script calls them like any function.
//
// - cycles() reads the CPU's cycle counter (rsr a2,ccount), to time code to
//   the cycle;
// - leadingZeros() and clampByte() use instructions C has no operator for
//   (nsau, clamps);
// - sumAsm() adds up a global array in a loop of its own, reached with
//   l32r @_data, timed against the same loop written in C.
//
// The calling convention is the one of the script's own functions (the
// windowed call8 ABI): the arguments arrive in a2, a3... (a float as its
// bits: wfr moves it into a float register), an int result goes in a2, a
// float one in f2. Start with "entry a1,32", end with "retw.n"; registers
// a2 to a15 are free to use. See the README's "Assembly functions".
#include "script_executable.h"

char script[] = R"EOF(
int data[64];

// the cycle counter: 240 per microsecond at 240 MHz
__ASM__ uint32_t cycles()
{
"entry a1,32"
"rsr a2,ccount"
"retw.n"
}

// how many zero bits before the first 1 (32 for 0)
__ASM__ int leadingZeros(uint32_t v)
{
"entry a1,32"
"nsau a2,a2"
"retw.n"
}

// held to -128..127
__ASM__ int clampByte(int v)
{
"entry a1,32"
"clamps a2,a2,7"
"retw.n"
}

// data[0] + ... + data[n - 1]
__ASM__ int sumAsm(int n)
{
"entry a1,32"
"l32r a4,@_data"
"movi a3,0"
"beqz a2,sum_end"
"sum_loop:"
"l32i a5,a4,0"
"add a3,a3,a5"
"addi a4,a4,4"
"addi a2,a2,-1"
"bnez a2,sum_loop"
"sum_end:"
"mov a2,a3"
"retw.n"
}

// the same in C
int sumC(int n)
{
   int s = 0;
   for (int i = 0; i < n; i++)
      s = s + data[i];
   return s;
}

void main()
{
   for (int i = 0; i < 64; i++)
      data[i] = i * 3 - 20;
   printfln("leading zeros: 1 -> %d, 255 -> %d, 65536 -> %d, 0 -> %d", leadingZeros(1), leadingZeros(255),
            leadingZeros(65536), leadingZeros(0));
   printfln("clamped to a byte: 300 -> %d, -300 -> %d, 42 -> %d", clampByte(300), clampByte(-300), clampByte(42));
   uint32_t t0 = cycles();
   int a = sumAsm(64);
   uint32_t t1 = cycles();
   int c = sumC(64);
   uint32_t t2 = cycles();
   printfln("sum of 64 values: assembly %d in %d cycles, C %d in %d cycles", a, t1 - t0, c, t2 - t1);
}
)EOF";

void setup()
{
   Serial.begin(115200);
   delay(1000);
   ScriptExecutable exec = parseScript(script);
   if (exec.isExeExists())
      exec.execute("main");
   else
      showCompileError();
}

void loop()
{
}
