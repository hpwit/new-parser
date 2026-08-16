// Compiles the same naive recursive fib(n) script as FibonacciTiming.ino,
// but stops to print the generated Xtensa assembly (header/content/footer
// -- see LanguageBasics.ino for the same technique) before handing it off
// to createBinary()/createExecutableFromBinary(), then actually runs
// fib(10) to confirm the printed assembly is what got executed.
//
// Uses the manual parse -> createBinary() -> createExecutableFromBinary()
// pipeline (PrintBinaryHex.ino's style), not parseScript()/
// ScriptExecutable -- the latter never exposes the intermediate assembly
// text this example exists to print.
#include "parser.h"
#include "compiler_error.h"
#include "asm_parser.h"
#include "asm_execute.h"

char script[] = R"EOF(
int fib(int n)
{
   if (n < 2)
   {
      return n;
   }
   return fib(n - 1) + fib(n - 2);
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

   printf("********** generated assembly **********\n");
   printf("---- header ----\n");
   header.display();
   printf("---- content ----\n");
   content.display();
   printf("---- footer ----\n");
   footer.display();

   // createBinary() consumes (clears) footer/header/content, so it has to
   // run after the printing above.
   Binary bin = createBinary(&footer, &header, &content, false);
   if (bin.error.error)
   {
      printf("assembler error: %s\n", bin.error.error_message ? bin.error.error_message : "?");
      return;
   }
   printf("\n%d bytes of instructions, %d bytes of relocation header\n", bin.instruction_size, bin.function_size);

   executable exe = createExecutableFromBinary(&bin);
   if (exe.error.error)
   {
      printf("loader error: %s\n", exe.error.error_message ? exe.error.error_message : "?");
      return;
   }

   Arguments args;
   args.add(10);
   int32_t result = 0;
   if (callFunction(&exe, "fib", &args, &result))
   {
      printf("fib(10) = %d\n", result);
   }
   else
   {
      printf("callFunction(\"fib\") failed\n");
   }

   freeExecutable(&exe);
}

void loop()
{
}
