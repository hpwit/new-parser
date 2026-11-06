// ESP-IDF port of examples/TwoScripts/TwoScripts.ino -- compiles and
// runs two different scripts one after another, freeing the first one's
// compiled binary explicitly (ScriptExecutable::free()) before compiling
// the second, instead of waiting for scope exit to release it.
//
// ScriptExecutable already frees itself automatically when it goes out
// of scope -- free() exists for the case that isn't scope-shaped: you're
// not done with the *variable*, you're done with the *compiled script it
// currently holds*, and you want that memory back right now, before
// compiling the next one. Both scripts declared in the same app_main()
// the way this example does would stay resident until app_main()
// returns either way -- free() is what actually reclaims script1's
// memory in between.
//
// ScriptExecutable can't be copied, moved, or reassigned (see its class
// comment in script_executable.h) -- so "reusing" it across two scripts
// means calling free() on the existing object, then compiling a
// *second* ScriptExecutable variable for the next script, not assigning
// a fresh parseScript() result back into the first one.
#include "script_executable.h"

static char script1[] = R"EOF(
void main()
{
   for (int i = 0; i < 3; i++)
   {
      printfln("script1 i:%d", i);
   }
}
)EOF";

static char script2[] = R"EOF(
void main()
{
   for (int i = 0; i < 3; i++)
   {
      printfln("script2 i:%d", i * i);
   }
}
)EOF";

extern "C" void app_main(void)
{
   ScriptExecutable exec1 = parseScript(script1);
   if (exec1.isExeExists())
   {
      exec1.execute("main");
   }
   // Done with script1 -- release its compiled binary right now instead
   // of holding onto it until app_main() returns.
   exec1.free();

   ScriptExecutable exec2 = parseScript(script2);
   if (exec2.isExeExists())
   {
      exec2.execute("main");
   }
   exec2.free();
}
