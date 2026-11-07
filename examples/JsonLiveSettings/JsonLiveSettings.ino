// Changing a running script's settings with JSON.
//
// The script runs on its own task, forever. New settings arrive as JSON
// (here from loop() every 2 seconds; in a real sketch from a web page or
// MQTT) and the script applies them itself, by calling applySettings() at
// the top of its loop: the settings then change all at once, between two
// frames, never while the script is using them.
//
// updateJson() could also be called directly from loop(): the script
// would see each new number at its next read, but with several settings
// it could use some old and some new ones in the same frame, or half a
// string. See the README's "Filling variables from JSON".
//
// Needs the ArduinoJson library. esp32 only (executeAsTask()).
#include <ArduinoJson.h>
#include "script_executable.h"
#include "binding.h"

static ScriptExecutable *script = NULL;

// the JSON waiting to be applied; NULL when there is none
static const char *volatile pendingJson = NULL;

// called by the script, on its own task, where it chooses
void applySettings()
{
   const char *json = __atomic_exchange_n(&pendingJson, (const char *)NULL, __ATOMIC_ACQ_REL);
   if (json != NULL && script != NULL)
      script->updateJson(json);
}

void wait(int ms)
{
   delay(ms); // gives the CPU back: the other tasks and the watchdog run
}

char source[] = R"EOF(
json "speed" as int speed = 10;
json "color" as char color[12];
json "steps" as int steps[3];

void main()
{
   int frame = 0;
   int position = 0;
   while (true)
   {
      applySettings();                  // new settings, if any, come in here
      position = position + speed;
      frame++;
      if (frame % 20 == 0)
         printfln("script: frame %d, position %d, speed %d, color %s, steps %d %d %d",
                  frame, position, speed, color, steps[0], steps[1], steps[2]);
      wait(50);
   }
}
)EOF";

const char *presets[] = {
   "{\"speed\":10,\"color\":\"red\",\"steps\":[1,2,3]}",
   "{\"speed\":25,\"color\":\"green\",\"steps\":[4,5,6]}",
   "{\"color\":\"blue\"}",                        // only the color changes
   "{\"speed\":-5,\"color\":\"a very long name\",\"steps\":[7,8,9]}",
};

void setup()
{
   Serial.begin(115200);
   delay(1000);

   bindFunction((char *)"void", (char *)"applySettings", NULL, (void *)applySettings);
   bindFunction((char *)"void", (char *)"wait", (char *)"int", (void *)wait);

   static ScriptExecutable exec = parseScript(source);
   if (!exec.isExeExists())
   {
      showCompileError();
      return;
   }
   script = &exec;
   // the first settings are applied by the script itself, after its
   // initialization (which would overwrite settings applied before)
   pendingJson = presets[0];
   exec.executeAsTask("main");
}

void loop()
{
   static int next = 1;
   static uint32_t last = millis();
   if (next < 4 && millis() - last > 2000)
   {
      printf("sketch: sending %s\n", presets[next]);
      pendingJson = presets[next++];
      last = millis();
   }
   delay(10);
}
