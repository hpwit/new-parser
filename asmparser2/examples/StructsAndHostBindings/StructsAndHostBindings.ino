// Demonstrates structs with member functions, plus binding.h's
// bindFunction()/bindVariable() -- registering native C++ functions/
// variables from the host sketch before parsing lets the assembler's
// loader resolve the script's calls to real host pointers at load time.
//
// The script below also declares println/brightness `external` itself.
// bindFunction()/bindVariable() alone are enough for *parsing* -- the
// parser looks the name up in binded_assets and synthesizes the
// declaration automatically (see parser.cpp, ~line 2444) -- but that
// auto-declare path has a known gap where the assembler's jump-table
// reservation for the call never gets emitted, so *assembling* needs the
// explicit declaration too (see asm_parser.cpp's "generates executable
// binary: external function call" test in test/host/test_parser.cpp).
#include "parser.h"
#include "asm_parser.h"
#include "asm_execute.h"

int brightnessPercent = 100;

void scriptPrintln(int v)
{
   printf("script says: %d\n", v);
}

char script[] = R"EOF(
external void println(int v);
external int brightness;

struct pixel
{
   int r;
   int g;
   int b;
   void scale()
   {
      r = r * brightness / 100;
      g = g * brightness / 100;
      b = b * brightness / 100;
   }
}

pixel px;

void main()
{
   px.r = 255;
   px.g = 128;
   px.b = 0;
   px.scale();
   println(px.r);
}
)EOF";

void setup()
{
   Serial.begin(115200);

   // Registers the real host pointers the loader resolves the script's
   // println()/brightness references to at load time.
   bindFunction((char *)"void", (char *)"println", (char *)"int", (void *)scriptPrintln);
   bindVariable((char *)"int", (char *)"brightness", NULL, (void *)&brightnessPercent);

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
