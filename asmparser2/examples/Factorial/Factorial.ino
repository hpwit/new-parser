// v2 equivalent of a v1 (ESPLiveScript.h) example that recursively
// computes factorials and prints the result from *inside* the script,
// via printfln() and v1's Parser::parseScript()/Executable::execute()
// convenience wrappers:
//
//   int fact(int h) { if (h==1) return 1; return h*fact(h-1); }
//   void main(int g) { printfln("factorial of %d is %d", g, fact(g)); }
//   ... Arguments args; args.add(5); exec.execute("main", args); ...
//
// v2 has no Parser::parseScript()/Executable wrapper yet -- every example
// here uses the same lower-level parse -> createBinary ->
// createExecutableFromBinary pipeline directly (see parser.h/asm_parser.h/
// asm_execute.h). One deliberate behavioral change from the v1 original:
// main(int g) just returns fact(g) instead of calling printfln() itself.
// Reason: printfln is a variadic, Args-typed external, and no example (or
// anything else) in this library binds it to a *real* host implementation
// -- every one of them binds it to NULL ("left unresolved", see
// BouncingBalls.ino's setup()) because correctly decoding a variadic
// Args call's mixed register/stack layout (visitnode.cpp's
// _visitdefInputArgumentsNode) is a real, unimplemented feature of this
// port, not something to paper over in an example. Calling it here would
// crash the moment the script reached that line. fact()'s return value is
// used directly instead, printed from the .ino side -- same observable
// output ("factorial of N is F(N)"), just printed by the host instead of
// the script.
//
// Also: main() itself can't be called this way regardless -- main()'s own
// call path (runExecutableWithArgs()) goes through an argument-marshaling
// wrapper that never surfaces a return value (see CallScriptFunction.ino's
// header comment on why). So this calls fact() *directly* by name via
// callFunction()'s Arguments overload (ported from upstream's
// ESPLivescriptRuntime.h -- see arguments.h), which is the v2 mechanism
// closest to v1's Arguments/exec.execute(name, args) pattern and is what
// actually gets a real return value back.
#include "parser.h"
#include "asm_parser.h"
#include "asm_execute.h"

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

   Arguments args;
   int32_t result;

   args.add(5);
   result = 0;
   if (callFunction(&exe, "fact", &args, &result))
      printf("factorial of %d is %d\n", 5, result);
   args.clear();

   args.add(6);
   result = 0;
   if (callFunction(&exe, "fact", &args, &result))
      printf("factorial of %d is %d\n", 6, result);
   args.clear();

   args.add(7);
   result = 0;
   if (callFunction(&exe, "fact", &args, &result))
      printf("factorial of %d is %d\n", 7, result);
   args.clear();

   freeExecutable(&exe);
}

void loop()
{
}
