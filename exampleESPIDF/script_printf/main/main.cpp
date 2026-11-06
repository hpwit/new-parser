// ESP-IDF port of examples/ScriptPrintf/ScriptPrintf.ino -- demonstrates
// a script printing directly with printf()/printfln(), no bindFunction()
// call and no `external` declaration needed (see README.md's "Built-in
// printf / printfln" section and src/runtime_functions.h/.cpp).
#include "script_executable.h"

static char script[] = R"EOF(
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

extern "C" void app_main(void)
{
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
