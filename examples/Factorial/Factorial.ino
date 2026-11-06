// v2 equivalent of a v1 (ESPLiveScript.h) example that recursively
// computes factorials and prints the result from *inside* the script,
// via printfln() and v1's Parser::parseScript()/Executable::execute()
// convenience wrappers:
//
//   int fact(int h) { if (h==1) return 1; return h*fact(h-1); }
//   void main(int g) { printfln("factorial of %d is %d", g, fact(g)); }
//   ... Arguments args; args.add(5); exec.execute("main", args); ...
//
// v2's parseScript()/ScriptExecutable (script_executable.h) now provide
// the same convenience -- one deliberate behavioral change from the v1
// original remains, though: main(int g) just returns fact(g) instead of
// calling printfln() itself. Reason: printfln is a variadic, Args-typed
// external, and no example (or anything else) in this library binds it
// to a *real* host implementation -- every one of them binds it to NULL
// ("left unresolved", see BouncingBalls.ino's setup()) because correctly
// decoding a variadic Args call's mixed register/stack layout
// (visitnode.cpp's _visitdefInputArgumentsNode) is a real, unimplemented
// feature of this port, not something to paper over in an example.
// Calling it here would crash the moment the script reached that line.
// fact()'s return value is used directly instead, printed from the .ino
// side -- same observable output ("factorial of N is F(N)"), just
// printed by the host instead of the script.
//
// This calls fact() *directly* by name via execute()'s Arguments
// overload (ScriptExecutable::execute(), backed by callFunction()'s
// Arguments overload -- arguments.h, ported from upstream's
// ESPLivescriptRuntime.h), which is the v2 mechanism closest to v1's
// Arguments/exec.execute(name, args) pattern and gets a real return
// value back -- unlike calling main() itself would (see
// CallScriptFunction.ino's header comment on why main()'s own call path
// never surfaces one).
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
   if (!exec.isExeExists())
   {
      return;
   }

   Arguments args;
   int32_t result;

   args.add(5);
   result = 0;
   if (exec.execute("fact", &args, &result))
      printf("factorial of %d is %d\n", 5, result);
   args.clear();

   args.add(6);
   result = 0;
   if (exec.execute("fact", &args, &result))
      printf("factorial of %d is %d\n", 6, result);
   args.clear();

   args.add(7);
   result = 0;
   if (exec.execute("fact", &args, &result))
      printf("factorial of %d is %d\n", 7, result);
   args.clear();
}

void loop()
{
}
