// What happens when a script doesn't compile, and how to show the error.
//
// Two broken scripts: one uses a variable that was never declared, the
// other forgets a `;` at the end of a line. For each:
//   - parseScript() prints the error to the console by itself (the
//     message, then the line with a ^ under the place it was noticed);
//   - isExeExists() is false, so the sketch knows not to run it;
//   - showCompileError() prints it again as a formatted report, whenever
//     the sketch wants;
//   - lastCompileError() gives the same information to the sketch --
//     message, line, position and the line itself -- to show it its own
//     way (on a display, a web page...).
//
// A missing `;` is noticed at the start of the next statement, so the
// error points at the line after the one that lacks it.
#include "script_executable.h"

// `b` is never declared.
char undefinedVariable[] = R"EOF(
void main()
{
   int a = 3;
   b = a + 1;
   printfln("b = %d", b);
}
)EOF";

// No `;` after `int a = 3`.
char missingSemicolon[] = R"EOF(
void main()
{
   int a = 3
   a = a + 1;
   printfln("a = %d", a);
}
)EOF";

// A report of your own, from lastCompileError() -- here in the console
// too, on a real project on a display or a web page.
static void myOwnReport(const char *name)
{
   const CompileError &e = lastCompileError();
   printf("[%s] line %d, position %d: %s\n", name, e.line, e.position, e.message);
}

static void tryScript(const char *name, const char *script)
{
   printf("\n===== compiling %s =====\n", name);
   ScriptExecutable exec = parseScript(script); // prints the error itself
   if (exec.isExeExists())
   {
      exec.execute("main");
   }
   else
   {
      showCompileError(); // the library's formatted report
      myOwnReport(name);
   }
}

void setup()
{
   Serial.begin(115200);
   delay(500);
   tryScript("undefinedVariable", undefinedVariable);
   tryScript("missingSemicolon", missingSemicolon);
}

void loop()
{
}
