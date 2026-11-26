// Full-system check, printing START/RESULT for run.sh: hex numbers in a
// CRGB array's initializer, CRGB(0x12, 0x34, 0x56), were stored as 0.
// The script copies its palette into a bound CRGB array, and the sketch
// compares the bytes.
#include "script_executable.h"
#include "binding.h"

struct CRGB
{
   uint8_t red, green, blue;
};

static CRGB leds[3];

char script[] = R"EOF(
CRGB pal[3] = {CRGB(0x12, 0x34, 0x56), CRGB(0xff, 0x80, 0x01), CRGB(200, 0x0A, 7)};
void main()
{
   for (int i = 0; i < 3; i++)
      leds[i] = pal[i];
}
)EOF";

void setup()
{
   Serial.begin(115200);
   delay(1000);
   printf("START\n");

   bindVariable((char *)"CRGB", (char *)"leds", (char *)"[3]", (void *)leds);
   ScriptExecutable exec = parseScript(script);
   if (!exec.isExeExists())
   {
      showCompileError();
      printf("RESULT FAIL\n");
      return;
   }
   exec.execute("main");

   const uint8_t expected[9] = {0x12, 0x34, 0x56, 0xff, 0x80, 0x01, 200, 0x0A, 7};
   const uint8_t *got = (const uint8_t *)leds;
   bool ok = true;
   for (int i = 0; i < 9; i++)
   {
      printf("%02x ", got[i]);
      if (got[i] != expected[i])
         ok = false;
   }
   printf("\nRESULT %s\n", ok ? "PASS" : "FAIL");
}

void loop()
{
   delay(1000);
}
