// safe_mode end-to-end test, meant to run as a real ESP32 application
// under QEMU's esp32 machine (see ../run.sh) -- or on a board. Everything
// here is the real library: the script is compiled on the (emulated)
// chip, loaded into executable memory, and a failed bounds check unwinds
// through the real setjmp/longjmp path in asm_execute.cpp.
//
// Prints one "ok"/"FAIL" line per check, then "RESULT PASS" or
// "RESULT FAIL".
#include "script_executable.h"
#include "runtime_functions.h"
#include "binding.h"
#include <string.h>

// Written by the scripts at checkpoints, so the host can see exactly how
// far a script got before a failed check stopped it.
static int g_progress = 0;

// Host buffer bound to the script's `external uint8_t ext[6];`, with
// guard bytes on both sides to catch a write that gets through.
static uint8_t g_extBacking[4 + 6 + 4];
static uint8_t *g_ext = g_extBacking + 4;

char safeScript[] = R"EOF(
safe_mode
external int progress;
external uint8_t ext[6];
int buf[10];
int canary;
uint8_t grid[3, 4];

int fillOk()
{
   canary = 1234;
   for (int i = 0; i < 10; i++)
      buf[i] = i * i;
   int s = 0;
   for (int i = 0; i < 10; i++)
      s = s + buf[i];
   return s;
}

int overflow()
{
   progress = 1;
   for (int i = 0; i < 12; i++)
      buf[i] = 7;
   progress = 3;
   return 1;
}

void deepWrite(int n)
{
   buf[n] = 99;
   progress = 2;
}

int nested()
{
   progress = 1;
   deepWrite(10);
   progress = 3;
   return 1;
}

int negative()
{
   progress = 1;
   int k = 0 - 1;
   buf[k] = 5;
   progress = 3;
   return 1;
}

int grid2d()
{
   progress = 1;
   grid[2, 3] = 1;
   progress = 2;
   grid[3, 0] = 1;
   progress = 3;
   return 1;
}

int extOk()
{
   for (int i = 0; i < 6; i++)
      ext[i] = i + 1;
   return ext[5];
}

int extOver()
{
   progress = 1;
   ext[6] = 238;
   progress = 3;
   return 1;
}

int readCanary()
{
   return canary;
}

int bufAt(int i)
{
   return buf[i];
}
)EOF";

// Compiled after safeScript, without the keyword: safe mode must not carry
// over from one compiled script to the next. The out-of-bounds store must
// simply happen (reads are never checked, so reading it back shows it did).
char plainScript[] = R"EOF(
int arr[4];
int pad[4];

int poke()
{
   arr[4] = 42;
   return arr[4];
}
)EOF";

static int g_failures = 0;
static void check(bool ok, const char *what)
{
   printf("%s %s\n", ok ? "ok  " : "FAIL", what);
   if (!ok)
      g_failures++;
}

static bool reports(ScriptExecutable &exec, const char *array, uint32_t size, int32_t index)
{
   const runtime_error_struct &e = exec.runtimeError();
   bool match = exec.hasRuntimeError() && strcmp(e.array, array) == 0 && e.size == size && e.index == index;
   if (!match)
      printf("     got error=%d array='%s' size=%u index=%d\n", (int)exec.hasRuntimeError(), e.array,
             (unsigned)e.size, (int)e.index);
   return match;
}

static int g_hookCalls = 0;
static void countingHook(const runtime_error_struct *error)
{
   g_hookCalls++;
   printf("hook: array '%s' size %u index %d\n", error->array, (unsigned)error->size, (int)error->index);
}

void setup()
{
   Serial.begin(115200);
   printf("START safe_mode test\n");
   memset(g_extBacking, 0xA5, sizeof(g_extBacking));

   bindVariable((char *)"int", (char *)"progress", NULL, (void *)&g_progress);
   bindVariable((char *)"uint8_t", (char *)"ext", (char *)"[]", (void *)g_ext);

   static ScriptExecutable safe = parseScript(safeScript);
   check(safe.isExeExists(), "safe_mode script compiles and loads");
   if (!safe.isExeExists())
   {
      printf("RESULT FAIL\n");
      return;
   }

   int32_t r = 0;
   bool done;

   done = safe.execute("fillOk", &r);
   check(done && r == 285 && !safe.hasRuntimeError(), "in-bounds writes run normally (sum 285)");

   g_progress = 0;
   done = safe.execute("overflow", &r);
   check(!done, "out-of-bounds write stops the script (execute returns false)");
   check(reports(safe, "buf", 10, 10), "error names buf, size 10, index 10");
   check(g_progress == 1, "nothing after the failed write ran");
   safe.execute("readCanary", &r);
   check(r == 1234, "the other globals were not overwritten (canary still 1234)");
   safe.execute("bufAt", (const int32_t[]){9}, 1, &r);
   check(r == 7, "writes before the failing one did happen (buf[9] == 7)");

   done = safe.execute("fillOk", &r);
   check(done && r == 285 && !safe.hasRuntimeError(), "script runs again normally afterwards, error cleared");

   g_progress = 0;
   done = safe.execute("nested", &r);
   check(!done && reports(safe, "buf", 10, 10), "failure in a called function is reported");
   check(g_progress == 1, "both the callee and the caller were abandoned");

   g_progress = 0;
   done = safe.execute("negative", &r);
   check(!done && reports(safe, "buf", 10, -1), "negative index is caught (index -1)");
   check(g_progress == 1, "nothing after the negative-index write ran");

   g_progress = 0;
   done = safe.execute("grid2d", &r);
   check(!done && reports(safe, "grid", 12, 12), "2D array: flattened index 12 against 12 elements");
   check(g_progress == 2, "the in-bounds 2D write before it ran");

   done = safe.execute("extOk", &r);
   check(done && r == 6, "external array in bounds (ext[5] == 6)");
   g_progress = 0;
   done = safe.execute("extOver", &r);
   check(!done && reports(safe, "ext", 6, 6), "external array out of bounds is reported");
   check(g_progress == 1 && g_ext[6] == 0xA5, "host guard byte after ext untouched");

   onScriptRuntimeError = countingHook;
   done = safe.execute("overflow", &r);
   check(!done && g_hookCalls == 1, "onScriptRuntimeError hook is called once");
   onScriptRuntimeError = NULL;

   static ScriptExecutable plain = parseScript(plainScript);
   check(plain.isExeExists(), "plain script compiles and loads");
   done = plain.execute("poke", &r);
   check(done && r == 42 && !plain.hasRuntimeError(),
         "without safe_mode nothing is checked, even right after a safe_mode script");

   printf("RESULT %s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures, g_failures == 1 ? "" : "s");
}

void loop()
{
}
