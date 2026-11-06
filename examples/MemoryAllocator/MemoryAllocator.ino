// Demonstrates a simple dynamic-memory design for scripts: this DSL has
// no pointer/dereference syntax of its own, so a heap allocation is
// exposed as four always-available built-ins -- alloc()/free() wrapping
// real malloc()/free(), plus writeInt()/readInt() accessor functions
// standing in for the `*(int*)(buf+offset) = v` a script can't write
// directly. A script-held uint32_t is just the real heap address,
// reinterpreted -- the same trick every other external pointer in this
// repo already uses (an internal global's address, an external array's
// base -- see StructsAndHostBindings.ino).
//
// Unlike that example's println()/brightness, none of alloc()/free()/
// writeInt()/readInt() need an `external` declaration in the script or a
// bindFunction() call here -- they're registered the same way printf()/
// printfln() already are (runtime_functions.cpp's
// registerBuiltinRuntimeFunctions(), called automatically before any
// script compiles).
//
// The script allocates a 10-int block, writes i*i into each slot via
// writeInt(), sums them back via readInt(), and frees the block --
// exercising the full alloc -> write -> read -> free round trip under
// real Xtensa calling-convention rules, not just a host-side structural
// check. Expected output: sum of squares 0..9 = 285.
#include "script_executable.h"

char script[] = R"EOF(
void main()
{
   uint32_t b;
   b = alloc(40);
   printfln("b=%u", b);
   int i;
   for (i = 0; i < 10; i++)
      writeInt(b, i*4, i*i);
   int sum;
   sum = 0;
   for (i = 0; i < 10; i++)
      sum = sum + readInt(b, i*4);
   printfln("sum of squares 0..9 = %d", sum);
   free(b);
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

   exec.execute("main");
   printf("********** done **********\n");
}

void loop()
{
}
