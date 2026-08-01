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
//
// Uses parseScript()/ScriptExecutable (script_executable.h) rather than
// spelling out parse -> createBinary -> createExecutableFromBinary by
// hand -- see LanguageBasics.ino instead if you want to see those
// individual steps (and the AST/generated-assembly output) spelled out;
// that's specifically what that example is for.
#include "script_executable.h"
#include "binding.h"

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

   ScriptExecutable exec = parseScript(script);
   if (!exec.isExeExists())
   {
      return;
   }

   // Equivalent to runExecutable(&exe) here -- see BouncingBalls.ino's
   // setup() for the one difference (irrelevant to this script, which
   // doesn't call sync()).
   exec.execute("main");
   printf("********** done **********\n");
}

void loop()
{
}
