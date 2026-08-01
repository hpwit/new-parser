// The v2 equivalent of upstream ESPLiveScript's high-level convenience
// API -- avoids spelling out the individual parse -> createBinary ->
// createExecutableFromBinary steps every other example in this repo
// does explicitly (see e.g. Factorial.ino):
//
//   Parser p;
//   Executable exec = p.parseScript(&script);
//   if (exec.isExeExists())
//   {
//      exec.execute("main");
//   }
//
// becomes, in v2 (script_executable.h):
//
//   ScriptExecutable exec = parseScript(script);
//   if (exec.isExeExists())
//   {
//      exec.execute("main");
//   }
//
// parseScript() is a free function rather than a Parser method (so
// script_executable.h doesn't have to pull asm_parser.h/asm_execute.h
// into parser.h for every other file that includes it) -- it builds and
// owns its own Parser internally. No manual cleanup needed either way:
// ~ScriptExecutable() frees the loaded executable when `exec` goes out
// of scope, and parseScript() itself already frees every intermediate
// buffer the pipeline allocates that isn't part of the final result
// (including Binary's binary_data/function_data, which none of this
// repo's other examples free -- see script_executable.h's comment).
//
// execute() calls by name via callFunction() (works for any declared
// function, main() included -- see its own doc comment for why), so
// unlike upstream's execute("main") it also gets a *real* return value
// back, not just a fire-and-forget call.
#include "script_executable.h"

char script[] = R"EOF(
int fact(int h)
{
   if (h == 1)
   {
      return 1;
   }
   return h * fact(h - 1);
}

void main()
{
}
)EOF";

void setup()
{
   Serial.begin(115200);

   ScriptExecutable exec = parseScript(script);
   if (exec.isExeExists())
   {
      // main() itself, no return value used -- matches upstream's
      // exec.execute("main") exactly.
      exec.execute("main");

      // fact() directly, with a real argument and a real result back.
      Arguments args;
      args.add(6);
      int32_t result = 0;
      if (exec.execute("fact", &args, &result))
      {
         printf("fact(6) = %d\n", result);
      }
   }
   else
   {
      printf("script failed to compile/load\n");
   }
}

void loop()
{
}
