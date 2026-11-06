// Calls a *named* function declared in the script -- not just main() --
// and uses its return value in the .ino sketch itself.
//
// asm_execute.h's callFunction() finds the function by name and calls it
// through direct register passing (the same calling convention compiled
// script code itself uses to call other script functions), reading the
// result back from the Xtensa C ABI's return-value register -- exactly
// where the script's own `return expr;` leaves it. This is different
// from how main() gets called (runExecutable()/runExecutableWithArgs()
// go through an argument-marshaling wrapper instead) -- see
// asm_execute.h's comments on both for why the two need different
// mechanisms.
//
// Verified under QEMU (Espressif's esp32 machine model): this exact
// script's fibonacci(10) call, through this exact mechanism, returns 55
// on a real Xtensa CPU emulation -- see test/qemu/gen_named_function.cpp.
//
// Uses parseScript()/ScriptExecutable (script_executable.h) instead of
// spelling out parse -> createBinary -> createExecutableFromBinary by
// hand (compare SimpleScript.ino, or any earlier revision of this file
// in git history, for what that looks like) -- it also frees every
// intermediate buffer automatically, including Binary's own
// binary_data/function_data, which the hand-written version of this
// example (like most others before this pass) never did.
#include "script_executable.h"

char script[] = R"EOF(
int fibonacci(int n)
{
   int a;
   a = 0;
   int b;
   b = 1;
   int i;
   for (i = 0; i < n; i++)
   {
      int t;
      t = a + b;
      a = b;
      b = t;
   }
   return a;
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

   // Call the script's fibonacci(n) for a few values of n and use each
   // result directly in the .ino -- here just printed, but this is
   // exactly the pattern for e.g. sizing a delay, an LED count, a PWM
   // duty cycle, or anything else the sketch wants to drive from a
   // value the *script* computed.
   for (int n = 0; n <= 10; n++)
   {
      int32_t args[1] = {n};
      int32_t result = 0;
      if (!exec.execute("fibonacci", args, 1, &result))
      {
         printf("execute(\"fibonacci\") failed\n");
         break;
      }
      printf("fibonacci(%d) = %d\n", n, result);
   }
}

void loop()
{
}
