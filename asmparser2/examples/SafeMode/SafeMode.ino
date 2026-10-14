// safe_mode: bounds-checked array writes.
//
// The script below has an off-by-one bug: drawBar() fills `height + 1`
// cells of a column that only has `height` rows. With `safe_mode` at the
// top of the script, the first write past the end of `bars` is caught
// before it happens:
//
//  - the error is reported (here through a handler installed with
//    onScriptRuntimeError; without one it is printed to the console),
//  - the whole script is abandoned -- drawBar() and main() alike -- and
//    execute() returns false,
//  - exec.runtimeError() says which array, its size and the index.
//
// The first bad write is column 5's bar, one row past the top: row -1,
// which for this 2D array is the flattened index -1 * 8 + 5 = -3, so the
// report reads "bars[-3] ... 40 elements".
//
// Remove `safe_mode` from the script and the same run silently writes
// one element past `bars`, into whatever the compiler placed after it.
//
// See README.md's "Safe mode and arrays" for what is and isn't checked.
#include "script_executable.h"
#include "runtime_functions.h"

char script[] = R"EOF(
safe_mode
#define width 8
#define height 5

uint8_t bars[height, width];

void drawBar(int x, int h)
{
   for (int y = 0; y <= h; y++)
   {
      bars[height - 1 - y, x] = 1;
   }
}

void main()
{
   for (int x = 0; x < width; x++)
   {
      drawBar(x, x % (height + 1));
   }
   printfln("all bars drawn");
}
)EOF";

void reportBoundsError(const runtime_error_struct *e)
{
   printf("script error: %s[%d] is out of range (the array has %u elements)\n",
          e->array, (int)e->index, (unsigned)e->size);
}

void setup()
{
   Serial.begin(115200);

   onScriptRuntimeError = reportBoundsError;

   ScriptExecutable exec = parseScript(script);
   if (!exec.isExeExists())
   {
      printf("script failed to compile/load\n");
      return;
   }

   if (exec.execute("main"))
   {
      printf("main() finished normally\n");
   }
   else if (exec.hasRuntimeError())
   {
      const runtime_error_struct &e = exec.runtimeError();
      printf("main() was stopped by safe_mode (array '%s', index %d, size %u)\n",
             e.array, (int)e.index, (unsigned)e.size);
   }
}

void loop()
{
}
