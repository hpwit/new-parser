// Compiles a small ESPLiveScript program exercising the core language
// features -- variables, arithmetic, if/else, for, while, and the ternary
// operator -- prints the resulting AST and generated Xtensa assembly, then
// assembles it to real machine code and executes it on-device.
//
// NOTE: `break;`/`continue;` are intentionally not used here -- the current
// parser crashes on them (NodeToken::NodeToken(char*, nodeType) in
// nodetoken.cpp never initializes _c_size). See test/host/test_parser.cpp
// for a standalone regression test that documents this.
//
// NOTE: printnum() is declared `external` explicitly rather than only
// registered via bindFunction() -- bindFunction()-only registration has a
// known gap where the assembler's jump-table reservation for the call
// never gets emitted (see asm_parser.cpp's "generates executable binary:
// external function call" test in test/host/test_parser.cpp).
#include "parser.h"
#include "asm_parser.h"
#include "asm_execute.h"

void printnum(int n)
{
   printf("printnum: %d\n", n);
}

char script[] = R"EOF(
external void printnum(int n);

void main()
{
   int a;
   int b;
   int sum;
   a = 3;
   b = 4;
   sum = a + b * 2;
   printnum(sum);

   int i;
   for (i = 0; i < 5; i++)
   {
      if (i < 2)
      {
         printnum(i);
      }
      else
      {
         printnum(i * 10);
      }
   }

   int w;
   w = 0;
   while (w < 3)
   {
      printnum(w);
      w = w + 1;
   }

   int t;
   t = (sum > 10) ? 1 : 0;
   printnum(t);
}
)EOF";

void setup()
{
   Serial.begin(115200);

   // Registers the real host pointer the loader resolves the external
   // call's jump-table slot to at load time.
   bindFunction((char *)"void", (char *)"printnum", (char *)"int", (void *)printnum);

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

   printf("********** AST **********\n");
   program.prettyPrint(0);
   printf("********** variables **********\n");
   main_context.prettyPrint(0);
   printf("********** functions **********\n");
   functions.prettyPrint(0);

   printf("********** generated code **********\n");
   content.display();
   header.display();
   footer.display();

   // createBinary() consumes (clears) footer/header/content, so it has to
   // run after the printing above.
   printf("********** assembling **********\n");
   Binary bin = createBinary(&footer, &header, &content, false);
   if (bin.error.error)
   {
      printf("assembler error: %s\n", bin.error.error_message ? bin.error.error_message : "?");
      return;
   }
   printf("%d bytes of instructions, %d bytes of relocation header\n", bin.instruction_size, bin.function_size);

   printf("********** loading and executing **********\n");
   executable exe = createExecutableFromBinary(&bin);
   if (exe.error.error)
   {
      printf("loader error: %s\n", exe.error.error_message ? exe.error.error_message : "?");
      return;
   }
   runExecutable(&exe);
   freeExecutable(&exe);
   printf("********** done **********\n");
}

void loop()
{
}
