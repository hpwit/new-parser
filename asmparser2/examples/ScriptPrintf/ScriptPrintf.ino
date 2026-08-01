// Demonstrates a script printing directly with printf()/printfln() --
// no bindFunction() call and no `external` declaration needed, unlike
// every other host call in this repo's examples (see
// StructsAndHostBindings). Both are always available once anything has
// been parsed at least once (see README.md's "Built-in printf /
// printfln" section and src/runtime_functions.h/.cpp).
//
// printfln() appends a trailing "\r\n"; printf() doesn't, so repeated
// calls can build up a single line (the "done." print below).
#include "script_executable.h"

char script[] = R"EOF(
void main()
{
   printfln("ESPLiveScript2 says hello!");

   for (int i = 1; i <= 5; i++)
   {
      printfln("square of %d is %d", i, i * i);
   }

   printf("done.");
   printfln("");
}
)EOF";

void setup()
{
   Serial.begin(115200);

   ScriptExecutable exec = parseScript(script);
   if (exec.isExeExists())
   {
      exec.execute("main");
   }
   else
   {
      printf("script failed to compile/load\n");
   }
}

void loop()
{
}
