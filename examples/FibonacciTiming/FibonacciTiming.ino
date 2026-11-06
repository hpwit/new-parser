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
// Measured on an ESP32 at 240 MHz: 21.7 s, about 15.7 cycles per call.
// The same function compiled natively by the Arduino-ESP32 toolchain
// takes 17.0 s -- gcc turns the second recursive call into a loop, so it
// makes half as many calls. At this recursion depth most of each call's
// cost is the register-window overflow/underflow exceptions, which QEMU
// does not charge for: test/qemu's fib_timing reads ~5 cycles per call
// there, so don't project real-hardware time from it.
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
