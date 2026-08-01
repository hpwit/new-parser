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
#include "parser.h"
#include "asm_parser.h"
#include "asm_execute.h"

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

   Script s;
   s.addContent(script);
   s.init();

   Parser p;
   p.clean();
   p.parse(&s, &__allTokens);

   if (Error.error)
   {
      display_error(&Error);
      return;
   }

   Binary bin = createBinary(&footer, &header, &content, false);
   if (bin.error.error)
   {
      printf("assembler error: %s\n", bin.error.error_message ? bin.error.error_message : "?");
      return;
   }

   executable exe = createExecutableFromBinary(&bin);
   if (exe.error.error)
   {
      printf("loader error: %s\n", exe.error.error_message ? exe.error.error_message : "?");
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
      if (!callFunction(&exe, "fibonacci", args, 1, &result))
      {
         printf("callFunction(\"fibonacci\") failed\n");
         break;
      }
      printf("fibonacci(%d) = %d\n", n, result);
   }

   freeExecutable(&exe);
}

void loop()
{
}
