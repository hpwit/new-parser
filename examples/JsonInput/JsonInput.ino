// A script's variables filled from a JSON document, as in v1's
// jsonexample: `json path as type name;` in the script, and the JSON
// given to execute(). Handy for settings coming from a web page, MQTT or
// a file, without recompiling the script.
//
// Needs the ArduinoJson library: including it below is what switches the
// JSON support on.
#include <ArduinoJson.h>
#include "script_executable.h"

char script[] = R"EOF(
json dp.value as int g2;              // v1's form: an unquoted path
json dp.test as int g;
json dp2 as float x;
json "name" as char label[16];        // a string, cut to fit if longer
json "steps" as int steps[3];         // a JSON array fills an array
json "speed" as int speed = 50;       // 50 unless the JSON has a "speed"

int fib(int h)
{
   if (h <= 1)
      return h;
   return fib(h - 1) + fib(h - 2);
}

void main()
{
   int d = x * 5;
   printfln("%s: x * 5 = %d, speed %d", label, d, speed);
   printfln("fib of %d is %d", g2, fib(g2));
   printfln("fib of %d is %d", g, fib(g));
   printfln("steps %d %d %d", steps[0], steps[1], steps[2]);
}
)EOF";

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

   exec.execute("main", "{\"dp\":{\"value\":6,\"test\":12},\"dp2\":4.5,\"name\":\"first run\",\"steps\":[1,2,3]}");
   exec.execute("main", "{\"dp\":{\"value\":10,\"test\":20},\"dp2\":7.5854,\"name\":\"second run\",\"steps\":[4,5,6],\"speed\":80}");

   // only fill the variables, then call (the values stay until changed)
   exec.updateJson("{\"dp\":{\"value\":15}}");
   exec.executeOnly("main");

   // a JSON that can't be read: execute() says so and returns false
   if (!exec.execute("main", "{not json"))
      printf("the last JSON was refused\n");
}

void loop()
{
   delay(1000);
}
