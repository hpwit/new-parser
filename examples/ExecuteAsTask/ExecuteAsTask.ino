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
// loop() then controls the script's task, as v1's suspend()/restart()/
// kill() do: it lets the script run, pauses it (its counter stops),
// restarts it (the counter carries on from where it stopped), kills it,
// and starts it again from scratch (main() begins again at 0) -- printing
// state() at each step: notStarted, running, paused or stopped.
//
// A pause or kill stops the task wherever it is, here most likely inside
// delay(); see pause()'s comment in script_executable.h for what that
// means when the script is inside a host function holding a lock.
//
// ESP32-only (FreeRTOS's task API isn't available on host, including
// this repo's own test/host/ build) -- executeAsTask() and the controls
// are conditionally compiled out everywhere else, matching that.
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

static ScriptExecutable *g_exec = NULL;

static const char *stateName(ScriptState s)
{
   switch (s)
   {
   case ScriptState::notStarted:
      return "not started";
   case ScriptState::running:
      return "running";
   case ScriptState::paused:
      return "paused";
   default:
      return "stopped";
   }
}

static void report(const char *action)
{
   printf("loop(): %s -> %s\n", action, stateName(g_exec->state()));
}

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
   // the sketch runs (and destroying it would kill the task). Same
   // lifetime pattern KeyboardCallback.ino uses for the same reason; see
   // its header comment for the full explanation of why
   // direct-initialization here (not a two-step "declare then assign")
   // matters.
   static ScriptExecutable holder = parseScript(script);
   if (!holder.isExeExists())
   {
      printf("script failed to compile/load\n");
      return;
   }
   g_exec = &holder;
   report("compiled");

   // main()'s while(true) now runs on its own task, pinned to core 0 --
   // this call returns immediately, unlike holder.execute("main")
   // would. Arduino's own setup()/loop() run pinned to core 1 by
   // default (WiFi/BT's stack owns core 0), so this keeps the script's
   // task off the same core loop() runs on; pass no core argument (or
   // tskNO_AFFINITY explicitly) to leave the choice to the scheduler
   // instead.
   holder.executeAsTask("main", 8192, 1, 0);
   report("executeAsTask(\"main\")");
}

void loop()
{
   if (g_exec == NULL)
      return;

   // Runs concurrently with the script's own task -- proof
   // executeAsTask() actually handed main() off instead of blocking
   // here the way execute("main") would have.
   delay(5000);
   g_exec->pause();
   report("pause()");

   delay(3000); // no ticks
   g_exec->restart();
   report("restart()");

   delay(3000); // ticks carry on from where they stopped
   g_exec->kill();
   report("kill()");

   delay(2000); // no ticks
   g_exec->executeAsTask("main", 8192, 1, 0);
   report("executeAsTask(\"main\") again");
}
