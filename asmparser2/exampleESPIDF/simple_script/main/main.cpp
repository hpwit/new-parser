// ESP-IDF port of examples/SimpleScript/SimpleScript.ino -- see that
// file for the full explanation of parseScript()/ScriptExecutable
// (script_executable.h), the v2 equivalent of upstream ESPLiveScript's
// Parser::parseScript()/Executable.
//
// The only differences from the Arduino .ino are structural, not
// behavioral: app_main() instead of setup()/loop() (plain ESP-IDF
// doesn't call a function repeatedly the way Arduino's loop() does, so
// there's nothing to put there), and no Serial.begin() -- ESP-IDF's
// printf() already goes to the console UART by default.
#include "script_executable.h"

static char script[] = R"EOF(
int fact(int h)
{
   if (h == 1)
   {
      return 1;
   }
   return h * fact(h - 1);
}

void main()
{
}
)EOF";

extern "C" void app_main(void)
{
   ScriptExecutable exec = parseScript(script);
   if (exec.isExeExists())
   {
      // main() itself, no return value used -- matches upstream's
      // exec.execute("main") exactly.
      exec.execute("main");

      // fact() directly, with a real argument and a real result back.
      Arguments args;
      args.add(6);
      int32_t result = 0;
      if (exec.execute("fact", &args, &result))
      {
         // (int) cast: result is int32_t (`long int` on this toolchain,
         // a distinct type from `int` as far as -Wformat is concerned,
         // though both are 32 bits) -- ESP-IDF's default build treats
         // that mismatch as an error.
         printf("fact(6) = %d\n", (int)result);
      }
   }
   else
   {
      printf("script failed to compile/load\n");
   }
}
