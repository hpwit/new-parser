// Several scripts running at once, each on its own FreeRTOS task,
// managed by name through scriptScheduler (script_scheduler.h): add,
// run, list (with the CPU each one uses), pause, restart, kill, remove.
//
// Three scripts:
//   busy      -- counts primes, the CPU-hungry one (gives the CPU back
//                for 1 ms every 500 numbers, see below)
//   blink     -- a tick every 100 ms, almost no CPU
//   countdown -- counts down 5..1 and returns on its own
// Each bumps a counter of the sketch, so you can see which ones move.
//
// Once a second the sketch prints the scheduler's table and the counters,
// and does one thing to the scripts:
//   1 s  pause busy        -- its counter stops, its CPU drops to 0
//   3 s  restart busy      -- it carries on where it stopped
//   5 s  kill blink
//   6 s  remove countdown  -- finished by then
//   7 s  run blink again   -- a fresh start
//   9 s  remove everything
//
// A script running as a task has to give the CPU back from time to time
// (here: delay()), or the lower priority tasks of its core never run --
// including, on core 0, the idle task the task watchdog checks.
// esp32 only.
#include "script_executable.h"
#include "script_scheduler.h"
#include "binding.h"

static volatile uint32_t busyCount = 0, blinkCount = 0, countdownCount = 0;

static void scriptDelay(uint32_t ms)
{
   vTaskDelay(pdMS_TO_TICKS(ms));
}

char busyScript[] = R"EOF(
external uint32_t busyCount;
external void delay(uint32_t ms);

bool isPrime(int n)
{
   if (n < 2)
      return false;
   for (int d = 2; d * d <= n; d++)
      if (n % d == 0)
         return false;
   return true;
}

void main()
{
   int n = 2;
   while (true)
   {
      if (isPrime(n))
         busyCount++;
      n++;
      if (n % 500 == 0)
         delay(1);
      if (n > 1000000)
         n = 2;
   }
}
)EOF";

char blinkScript[] = R"EOF(
external uint32_t blinkCount;
external void delay(uint32_t ms);

void main()
{
   while (true)
   {
      blinkCount++;
      delay(100);
   }
}
)EOF";

char countdownScript[] = R"EOF(
external uint32_t countdownCount;
external void delay(uint32_t ms);

void main()
{
   for (int i = 5; i > 0; i--)
   {
      printfln("countdown: %d", i);
      countdownCount++;
      delay(300);
   }
   printfln("countdown: done");
}
)EOF";

static void showCounters()
{
   printf("    counters: busy %lu  blink %lu  countdown %lu\n\n", (unsigned long)busyCount,
          (unsigned long)blinkCount, (unsigned long)countdownCount);
}

void setup()
{
   Serial.begin(115200);
   delay(500);

   bindVariable((char *)"uint32_t", (char *)"busyCount", NULL, (void *)&busyCount);
   bindVariable((char *)"uint32_t", (char *)"blinkCount", NULL, (void *)&blinkCount);
   bindVariable((char *)"uint32_t", (char *)"countdownCount", NULL, (void *)&countdownCount);
   bindFunction((char *)"void", (char *)"delay", (char *)"uint32_t", (void *)scriptDelay);

   if (!scriptScheduler.add("busy", busyScript) || !scriptScheduler.add("blink", blinkScript) ||
       !scriptScheduler.add("countdown", countdownScript))
   {
      showCompileError();
      return;
   }
   printf("\n--- 3 scripts added\n");
   scriptScheduler.list();

   scriptScheduler.run("busy", "main", 8192, 1, 1); // pinned to core 1
   scriptScheduler.run("blink");
   scriptScheduler.run("countdown");
   printf("\n--- all running\n");
}

static int step = 0;

void loop()
{
   if (step > 10)
      return;
   delay(1000);
   step++;
   switch (step)
   {
   case 1:
      printf("--- pause busy: %d\n", scriptScheduler.pause("busy"));
      break;
   case 3:
      printf("--- restart busy: %d\n", scriptScheduler.restart("busy"));
      break;
   case 5:
      printf("--- kill blink: %d\n", scriptScheduler.kill("blink"));
      break;
   case 6:
      printf("--- remove countdown: %d\n", scriptScheduler.remove("countdown"));
      break;
   case 7:
      printf("--- run blink again: %d\n", scriptScheduler.run("blink"));
      break;
   case 9:
      printf("--- remove everything\n");
      scriptScheduler.removeAll();
      break;
   }
   printf("t = %d s\n", step);
   scriptScheduler.list();
   showCounters();
   if (step == 10)
      printf("SCHED DONE\n");
}
