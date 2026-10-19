// Strings (char *) going in and out of functions:
//   - a script function returning char * (dayName());
//   - a host function returning char * (colorName(), bound below);
//   - char * parameters, passed on and returned (evenOdd());
//   - a char * handed to a host function (showText());
//   - printfln's %s, fed directly by a call returning char *.
//
// Expected output:
//   day 0 is Sunday
//   day 1 is a weekday
//   ...
//   day 6 is Saturday
//   host colors: blue and red
//   showText: odd
//   showText: even
//   showText: odd
//
// A char * is just the address of the characters; nothing is copied. A
// string literal returned by a script function lives in the script's
// data for as long as the script is loaded; a host function must return
// characters that stay valid too (here: string literals in the sketch).
#include "script_executable.h"
#include "binding.h"

const char *colors[] = {"red", "green", "blue"};

const char *colorName(int i)
{
   return colors[i % 3];
}

void showText(const char *text)
{
   printf("showText: %s\n", text);
}

char script[] = R"EOF(
external char *colorName(int i);
external void showText(char *t);

char *dayName(int d)
{
   if (d == 0)
      return "Sunday";
   if (d == 6)
      return "Saturday";
   return "a weekday";
}

char *evenOdd(int n, char *even, char *odd)
{
   if (n % 2 == 0)
      return even;
   return odd;
}

void main()
{
   for (int d = 0; d < 7; d++)
      printfln("day %d is %s", d, dayName(d));

   char *c = colorName(2);
   printfln("host colors: %s and %s", c, colorName(0));

   for (int n = 1; n <= 3; n++)
      showText(evenOdd(n, "even", "odd"));
}
)EOF";

void setup()
{
   Serial.begin(115200);
   bindFunction((char *)"char*", (char *)"colorName", (char *)"int", (void *)colorName);
   bindFunction((char *)"void", (char *)"showText", (char *)"char*", (void *)showText);

   ScriptExecutable exec = parseScript(script);
   if (exec.isExeExists())
   {
      exec.execute("main");
   }
   else
   {
      printf("script failed to compile/load\n");
   }
}

void loop()
{
}
