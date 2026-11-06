// ESP-IDF port of examples/ExecuteAsTask/ExecuteAsTask.ino --
// demonstrates ScriptExecutable::executeAsTask() -- runs a script
// function (here, main()'s own while(true) loop) on its own FreeRTOS
// task, pinned to a chosen CPU core, instead of blocking whichever
// function calls execute()/executeAsTask() itself.
//
// A deliberately minimal port of upstream ESPLiveScript's much larger
// executeAsTask() family -- see script_executable.h's own comment on
// executeAsTask() for what's intentionally not here (a registry of
// concurrently-running scripts, suspend()/restart()/kill(),
// sync()/_syncExt). This is plain, fire-and-forget FreeRTOS task
// creation.
//
// Structural difference from the .ino: app_main() itself takes the role
// loop() had (printing its own tick and delaying), since plain ESP-IDF
// doesn't call any function repeatedly the way Arduino's loop() does --
// app_main() just doesn't return.
#include "script_executable.h"
#include "binding.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void scriptDelay(uint32_t ms)
{
   vTaskDelay(pdMS_TO_TICKS(ms));
}

static char script[] = R"EOF(
external void delay(uint32_t ms);

void main()
{
   int n = 0;
   while (true)
   {
      printfln("script task tick %d", n);
      n = n + 1;
      delay(1000);
   }
}
)EOF";

extern "C" void app_main(void)
{
   // delay(), unlike printfln(), isn't a compiler built-in -- needs a
   // real binding, and (per BouncingBalls.ino's header comment) an
   // explicit `external` declaration in the script text too, since
   // bindFunction()-only auto-declare has a known jump-table gap.
   bindFunction((char *)"void", (char *)"delay", (char *)"uint32_t", (void *)scriptDelay);

   // `static` so this ScriptExecutable outlives app_main() -- the task
   // executeAsTask() spawns keeps calling back into it for as long as
   // the program runs.
   static ScriptExecutable holder = parseScript(script);
   if (!holder.isExeExists())
   {
      printf("script failed to compile/load\n");
      return;
   }

   // main()'s while(true) now runs on its own task, pinned to core 0 --
   // this call returns immediately, unlike holder.execute("main") would.
   // Pass no core argument (or tskNO_AFFINITY explicitly) to leave the
   // choice to the scheduler instead.
   holder.executeAsTask("main", 8192, 1, 0);

   // Runs concurrently with the script's own task -- proof
   // executeAsTask() actually handed main() off instead of blocking
   // here the way execute("main") would have.
   while (true)
   {
      printf("app_main() tick\n");
      vTaskDelay(pdMS_TO_TICKS(2500));
   }
}
