// Passing a string, an array and a structure to a script function that runs
// as a task: executeAsTask(name, &args, ...) with the data's addresses.
//
// A parameter is 32 bits, and so is an address on the esp32: the sketch adds
// (int)(uintptr_t)pointer to the Arguments, the script declares a pointer
// parameter (char *, int *, target *) and works on the sketch's data where it
// is. Nothing is copied:
// - the data must stay valid while the task uses it: globals or static
//   buffers here, never a local variable of the function that starts the task;
// - what the script writes through the pointer lands in the sketch's own
//   variables (moveTarget() below changes the sketch's struct).
//
// The three functions run one after the other; runTask() waits for each task
// to end before starting the next one. Output in the Serial Monitor (115200).
//
// See "Parameters for a task" in the README; TaskParameters passes ints and
// floats typed in the Serial Monitor. ESP32 only, like executeAsTask().
#include "script_executable.h"
#include "binding.h"

char script[] = R"EOF(
// the same fields, in the same order, as the sketch's Target below
struct target
{
   int x;
   int y;
   int hits;
};

// a string: char *, read where the sketch keeps it
void greet(char *name, int times)
{
   for (int i = 1; i <= times; i++)
      printfln("%d: hello %s", i, name);
}

// an array: a pointer and its length
void sumValues(int *values, int count)
{
   int total = 0;
   for (int i = 0; i < count; i++)
      total = total + values[i];
   printfln("%d values, sum %d", count, total);
}

// a structure: the script writes into the sketch's own struct
void moveTarget(target *t, int dx, int dy)
{
   t->x = t->x + dx;
   t->y = t->y + dy;
   t->hits = t->hits + 1;
   printfln("target moved to %d,%d (%d moves)", t->x, t->y, t->hits);
}

void main()
{
}
)EOF";

// The sketch's data. Global: still there while the tasks read and write it.
struct Target
{
   int x;
   int y;
   int hits;
};

static char name[32] = "ESPLiveScript";
static int values[5] = {3, 14, 15, 92, 65};
static Target target = {10, 20, 0};

static ScriptExecutable *g_exec = NULL;

// Starts `function` as a task with `args`, then waits until it has returned.
static void runTask(const char *function, Arguments *args)
{
   if (!g_exec->executeAsTask(function, args, 8192, 1, tskNO_AFFINITY, function))
   {
      Serial.printf("could not start %s\n", function);
      return;
   }
   while (g_exec->state() == ScriptState::running)
      delay(10);
}

void setup()
{
   Serial.begin(115200);
   delay(1000);

   static ScriptExecutable exec = parseScript(script);
   if (!exec.isExeExists())
   {
      showCompileError();
      return;
   }
   g_exec = &exec;

   // greet(char *name, int times): a string and an int
   {
      Arguments args;
      args.add((int)(uintptr_t)name);   // the string's address
      args.add(3);
      runTask("greet", &args);
   }

   // sumValues(int *values, int count): an array and its length
   {
      Arguments args;
      args.add((int)(uintptr_t)values);
      args.add((int)(sizeof(values) / sizeof(values[0])));
      runTask("sumValues", &args);
   }

   // moveTarget(target *t, int dx, int dy), twice: the script changes the
   // sketch's struct, and the sketch sees it
   for (int i = 0; i < 2; i++)
   {
      Arguments args;
      args.add((int)(uintptr_t)&target);
      args.add(5);
      args.add(-2);
      runTask("moveTarget", &args);
   }
   Serial.printf("in the sketch: target is at %d,%d after %d moves\n",
                 target.x, target.y, target.hits);

   // the string can change between two calls: the script reads it again
   strlcpy(name, "ESP32-S3", sizeof(name));
   {
      Arguments args;
      args.add((int)(uintptr_t)name);
      args.add(1);
      runTask("greet", &args);
   }
}

void loop()
{
   delay(1000);
}
