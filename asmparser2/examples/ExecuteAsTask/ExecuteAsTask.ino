// Demonstrates ScriptExecutable::executeAsTask() -- runs a script
// function (here, main()'s own while(true) loop) on its own FreeRTOS
// task, pinned to a chosen CPU core, instead of blocking whichever
// function calls execute()/executeAsTask() itself. Every other example
// that calls a script's main() (BouncingBalls.ino,
// MultiEffectController.ino, ...) does so from setup(), and main()'s
// own infinite loop means setup() -- and so loop() below it -- never
// actually runs. executeAsTask() exists for exactly this: hand main()
// its own task, on its own core, so the sketch's real loop() keeps
// running concurrently instead of competing with it for the same one.
//
// A deliberately minimal port of upstream ESPLiveScript's much larger
// executeAsTask() family (execute.h): that version also maintains a
// registry of several concurrently-running scripts, suspend()/
// restart()/kill() with cross-task handshaking, and sync()/_syncExt
// inter-task coordination. None of that exists here -- see README.md's
// "Known limitations". This is plain, fire-and-forget FreeRTOS task
// creation: once started, nothing in this library stops it again; the
// script itself would need to return on its own (this one never does,
// same as every other main()-with-while(true) example) for the task to
// end.
//
// ESP32-only (FreeRTOS's task API isn't available on host, including
// this repo's own test/host/ build) -- executeAsTask() itself is
// conditionally compiled out everywhere else, matching that.
#include "script_executable.h"
#include "binding.h"

void scriptDelay(uint32_t ms)
{
   delay(ms);
}

char script[] = R"EOF(
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

void setup()
{
   Serial.begin(115200);

   // delay(), unlike printfln(), isn't a compiler built-in -- needs a
   // real binding, and (per BouncingBalls.ino's header comment) an
   // explicit `external` declaration in the script text too, since
   // bindFunction()-only auto-declare has a known jump-table gap.
   bindFunction((char *)"void", (char *)"delay", (char *)"uint32_t", (void *)scriptDelay);

   // `static` so this ScriptExecutable outlives setup() -- the task
   // executeAsTask() spawns keeps calling back into it for as long as
   // the sketch runs. Same lifetime pattern KeyboardCallback.ino uses
   // for the same reason; see its header comment for the full
   // explanation of why direct-initialization here (not a two-step
   // "declare then assign") matters.
   static ScriptExecutable holder = parseScript(script);
   if (!holder.isExeExists())
   {
      printf("script failed to compile/load\n");
      return;
   }

   // main()'s while(true) now runs on its own task, pinned to core 0 --
   // this call returns immediately, unlike holder.execute("main")
   // would. Arduino's own setup()/loop() run pinned to core 1 by
   // default (WiFi/BT's stack owns core 0), so this keeps the script's
   // task off the same core loop() runs on; pass no core argument (or
   // tskNO_AFFINITY explicitly) to leave the choice to the scheduler
   // instead.
   holder.executeAsTask("main", 8192, 1, 0);
}

void loop()
{
   // Runs concurrently with the script's own task -- proof
   // executeAsTask() actually handed main() off instead of blocking
   // here the way execute("main") would have.
   printf("loop() tick\n");
   delay(2500);
}
