// Compiles a naive recursive fib(n) script and times fib(40) on-device,
// following the same pattern as Factorial.ino (parseScript()/
// ScriptExecutable -- script_executable.h -- and execute()'s Arguments
// overload).
//
// fib(40) with this naive (no memoization) recursion makes 2*fib(41)-1 =
// 331,160,281 calls -- a real stress test of this compiler's call/return
// overhead, not just a quick sanity check. Timed with micros() around the
// callFunction() call, the same way you'd time any other on-device work.
//
// Measured (not guessed): reading Xtensa's CCOUNT special register
// (`rsr.ccount`, the same technique the sc_examples corpus's own
// millis()/elapseMillis() __ASM__ functions use) around calls to the
// *actual* compiled fib() bytes under QEMU gives ~6.1 cycles/call,
// confirmed consistent between fib(25) (242,785 calls, 6.13 cycles/call)
// and fib(30) (2,692,537 calls -- 11x more -- 6.10 cycles/call). At
// 331,160,281 calls for fib(40) and the ESP32's default 240 MHz clock,
// that projects to (331,160,281 * 6.1) / 240,000,000 ~= 8.4 seconds on
// real hardware -- expect single-digit seconds, not milliseconds.
#include "script_executable.h"

char script[] = R"EOF(
int fib(int n)
{
   if (n < 2)
   {
      return n;
   }
   return fib(n - 1) + fib(n - 2);
}

void main()
{
}
)EOF";

void setup()
{
   Serial.begin(115200);

   ScriptExecutable exec = parseScript(script);
   if (!exec.isExeExists())
   {
      return;
   }

   Arguments args;
   args.add(40);
   int32_t result = 0;

   unsigned long startMicros = micros();
   bool ok = exec.execute("fib", &args, &result);
   unsigned long elapsedMicros = micros() - startMicros;

   if (ok)
   {
      printf("fib(40) = %d, took %lu us (%.3f s)\n",
             result, elapsedMicros, elapsedMicros / 1000000.0);
   }
   else
   {
      printf("execute(\"fib\") failed\n");
   }
}

void loop()
{
}
