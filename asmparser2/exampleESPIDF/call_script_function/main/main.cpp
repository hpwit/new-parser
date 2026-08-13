// ESP-IDF port of examples/CallScriptFunction/CallScriptFunction.ino --
// calls a *named* function declared in the script (not just main()) and
// uses its return value directly.
//
// asm_execute.h's callFunction() finds the function by name and calls it
// through direct register passing (the same calling convention compiled
// script code itself uses), reading the result back from the Xtensa C
// ABI's return-value register. Verified under QEMU: this exact script's
// fibonacci(10) call, through this exact mechanism, returns 55 on a real
// Xtensa CPU emulation -- see test/qemu/gen_named_function.cpp.
#include "script_executable.h"

static char script[] = R"EOF(
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

extern "C" void app_main(void)
{
   ScriptExecutable exec = parseScript(script);
   if (!exec.isExeExists())
   {
      return;
   }

   for (int n = 0; n <= 10; n++)
   {
      int32_t args[1] = {n};
      int32_t result = 0;
      if (!exec.execute("fibonacci", args, 1, &result))
      {
         printf("execute(\"fibonacci\") failed\n");
         break;
      }
      printf("fibonacci(%d) = %d\n", n, (int)result);
   }
}
