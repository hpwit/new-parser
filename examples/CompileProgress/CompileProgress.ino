// How far a compile has got: setCompileProgress() hands the compiler a
// function it calls as it works, with a percentage (0 to 100, never going
// back) and the stage it is in: "reading", "parsing", "generating",
// "optimizing", "assembling", then "done" with 100.
//
// A few lines like this one compile in a moment; a script of thousands of
// lines takes seconds to minutes on an esp32 (Ironforge's 3200 lines: about
// five minutes), and this is how a sketch can show that it is working: a
// line on the Serial Monitor here, a bar on a screen, a LED blinking...
//
// The function runs inside the compiler, on the task that compiles: it may
// print or draw, but not compile, load or free a script itself. It is
// called at most once per percent (plus once when the stage changes), so it
// doesn't slow the compile down.
#include "script_executable.h"

char script[] = R"EOF(
int primes[200];

int isPrime(int n)
{
   if (n < 2)
      return 0;
   for (int d = 2; d * d <= n; d++)
      if (n % d == 0)
         return 0;
   return 1;
}

int fillPrimes()
{
   int count = 0;
   int n = 2;
   while (count < 200)
   {
      if (isPrime(n) == 1)
      {
         primes[count] = n;
         count++;
      }
      n++;
   }
   return primes[199];
}

int main()
{
   return fillPrimes();
}
)EOF";

// The Serial Monitor can't rewrite a line: one line every 10%, and one
// whenever the stage changes.
static int shownTens = -1;
static const char *shownStage = NULL;

void showProgress(int percent, const char *stage, void *ctx)
{
   (void)ctx;
   if (percent / 10 == shownTens && stage == shownStage)
      return;
   shownTens = percent / 10;
   shownStage = stage;
   char bar[21];
   for (int i = 0; i < 20; i++)
      bar[i] = i < percent / 5 ? '#' : '.';
   bar[20] = 0;
   Serial.printf("  [%s] %3d%%  %s\n", bar, percent, stage);
}

void setup()
{
   Serial.begin(115200);
   delay(1000);

   Serial.println("compiling...");
   setCompileProgress(showProgress, NULL);
   uint32_t t0 = millis();
   ScriptExecutable exec = parseScript(script);
   setCompileProgress(NULL, NULL);   // no more calls (optional)
   Serial.printf("compiled in %u ms\n", (unsigned)(millis() - t0));
   if (!exec.isExeExists())
      return;

   // main() returns its value through callFunction(): the 200th prime, 1223
   int32_t result = 0;
   callFunction(&exec.exe, "main", NULL, 0, &result);
   Serial.printf("the 200th prime is %d\n", (int)result);
}

void loop()
{
}
