// Compiles and runs two different scripts one after another, freeing
// the first one's compiled binary explicitly (ScriptExecutable::free())
// before compiling the second, instead of waiting for scope exit to
// release it.
//
// ScriptExecutable already frees itself automatically when it goes out
// of scope (see SimpleScript.ino) -- free() exists for the case that
// isn't scope-shaped: you're not done with the *variable*, you're done
// with the *compiled script it's currently holding*, and you want that
// memory back right now, before compiling the next one, not whenever
// the enclosing function eventually returns. Two scripts declared in the
// same setup() the way this example does would both stay resident until
// setup() returns either way -- free() is what actually reclaims
// script1's memory in between.
//
// ScriptExecutable can't be copied, moved, or reassigned (see its class
// comment in script_executable.h) -- so "reusing" it across two scripts
// means calling free() on the existing object, then compiling a *second*
// ScriptExecutable variable for the next script, not assigning a fresh
// parseScript() result back into the first one.
#include "script_executable.h"

char script1[] = R"EOF(
void main()
{
   for (int i = 0; i < 3; i++)
   {
      printfln("script1 i:%d", i);
   }
}
)EOF";

char script2[] = R"EOF(
void main()
{
   for (int i = 0; i < 3; i++)
   {
      printfln("script2 i:%d", i * i);
   }
}
)EOF";

void setup()
{
   Serial.begin(115200);

   ScriptExecutable exec1 = parseScript(script1);
   if (exec1.isExeExists())
   {
      exec1.execute("main");
   }
   // Done with script1 -- release its compiled binary right now instead
   // of holding onto it until setup() returns.
   exec1.free();

   ScriptExecutable exec2 = parseScript(script2);
   if (exec2.isExeExists())
   {
      exec2.execute("main");
   }
   exec2.free();
}

void loop()
{
}
