// ESP-IDF port of examples/Factorial/Factorial.ino -- calls fact()
// directly by name via execute()'s Arguments overload
// (ScriptExecutable::execute(), backed by callFunction()'s Arguments
// overload -- arguments.h) and prints the real return value from the
// host side.
//
// One deliberate behavioral note carried over from the .ino: main()
// just returns instead of calling printfln() itself. printfln is a
// variadic, Args-typed external, and correctly decoding a variadic
// Args call's mixed register/stack layout
// (visitnode.cpp's _visitdefInputArgumentsNode) is a real, unimplemented
// feature of this port -- fact()'s return value is used directly
// instead, printed from here.
#include "script_executable.h"

static char script[] = R"EOF(
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

extern "C" void app_main(void)
{
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
      printf("factorial of %d is %d\n", 5, (int)result);
   args.clear();

   args.add(6);
   result = 0;
   if (exec.execute("fact", &args, &result))
      printf("factorial of %d is %d\n", 6, (int)result);
   args.clear();

   args.add(7);
   result = 0;
   if (exec.execute("fact", &args, &result))
      printf("factorial of %d is %d\n", 7, (int)result);
   args.clear();
}
