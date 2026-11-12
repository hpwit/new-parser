// Passing parameters to a script function that runs as a task:
// executeAsTask(name, &args, ...) gives the function its parameters, as
// execute(name, &args) does, but on its own FreeRTOS task.
//
// Type a command in the Serial Monitor (115200 baud, "Newline" line ending):
//   fib 35          runs fibTask(35): fib(35) and the time it took
//   count 5 0.5     runs countTask(5, 0.5): counts to 5, one step every 0.5 s
//   stop            kills the task
// Numbers with a '.' are passed as float, the others as int.
//
// What a parameter can be (see "Parameters for a task" in the README):
// - an int (in the script: int, uint32_t, uint16_t, uint8_t, bool...) or a
//   float: Arguments::add(int) / add(float) keep the type;
// - 6 at most: they travel in the registers a10..a15, more are dropped;
// - a function with fewer parameters ignores the extra values, one with
//   more sees garbage in the missing ones.
// No strings, structures or arrays as parameters: share those with
// bindVariable() / bindStruct() instead.
//
// ESP32 only, like executeAsTask().
#include "script_executable.h"
#include "binding.h"

static void scriptDelay(uint32_t ms)
{
   delay(ms);
}

static uint32_t scriptMillis()
{
   return millis();
}

char script[] = R"EOF(
external void delay(uint32_t ms);
external uint32_t millis();

int fib(int n)
{
   if (n < 2)
      return n;
   return fib(n - 1) + fib(n - 2);
}

// one int parameter
void fibTask(int n)
{
   printfln("computing fib(%d)...", n);
   uint32_t start = millis();
   int value = fib(n);
   printfln("fib(%d) = %d   (%d ms)", n, value, millis() - start);
}

// an int and a float parameter
void countTask(int times, float pause)
{
   int ms = pause * 1000;
   for (int i = 1; i <= times; i++)
   {
      printfln("count %d / %d", i, times);
      delay(ms);
   }
   printfln("count done");
}

void main()
{
}
)EOF";

static ScriptExecutable *g_exec = NULL;

// Splits "count 5 0.5" into the command word and its parameters, typed by
// the presence of a '.', then starts the matching script function as a task.
static void runCommand(char *line)
{
   char *word = strtok(line, " \t\r\n");
   if (word == NULL)
      return;
   if (strcmp(word, "stop") == 0)
   {
      Serial.println(g_exec->kill() ? "stopped" : "nothing running");
      return;
   }
   const char *function = NULL;
   if (strcmp(word, "fib") == 0)
      function = "fibTask";    // a string literal: stays valid while the task runs
   else if (strcmp(word, "count") == 0)
      function = "countTask";
   else
   {
      Serial.println("commands: fib <n>, count <times> <seconds>, stop");
      return;
   }

   Arguments args;   // copied by executeAsTask(): a local variable is fine
   for (char *p = strtok(NULL, " \t\r\n"); p != NULL; p = strtok(NULL, " \t\r\n"))
   {
      if (strchr(p, '.') != NULL)
         args.add((float)atof(p));
      else
         args.add((int)atoi(p));
   }
   // 16 KB of stack: fib() recurses
   if (!g_exec->executeAsTask(function, &args, 16384, 1, tskNO_AFFINITY, function))
      Serial.println("could not start the task");
}

void setup()
{
   Serial.begin(115200);
   delay(1000);
   bindFunction((char *)"void", (char *)"delay", (char *)"uint32_t", (void *)scriptDelay);
   bindFunction((char *)"uint32_t", (char *)"millis", NULL, (void *)scriptMillis);

   static ScriptExecutable exec = parseScript(script);
   if (!exec.isExeExists())
   {
      showCompileError();
      return;
   }
   g_exec = &exec;
   Serial.println("commands: fib <n>, count <times> <seconds>, stop");
}

void loop()
{
   static char line[64];
   static size_t len = 0;
   while (g_exec != NULL && Serial.available())
   {
      char c = Serial.read();
      if (c == '\n' || c == '\r')
      {
         if (len > 0)
         {
            line[len] = '\0';
            runCommand(line);
            len = 0;
         }
      }
      else if (len < sizeof(line) - 1)
      {
         line[len++] = c;
      }
   }
   delay(10);
}
