// Full-system copy of examples/HostStructs, printing START/RESULT for run.sh.
// Sharing a struct between the sketch and a script.
//
// bindStruct() hands the script the struct's definition, so the script
// uses the type without defining it again. SCRIPT_STRUCT() (binding.h)
// declares the C struct and keeps that definition as text, so it is
// written once. Script structs follow C's layout (each member aligned
// to its size, the whole padded to its largest member), which is what
// lets a bound variable of the type read and write the sketch's own
// fields, single structs and arrays alike.
#include "script_executable.h"
#include "binding.h"

SCRIPT_STRUCT(sensor, {
   char id;
   int value;
   uint16_t count;
   float average;
})

sensor probe = {'A', 10, 0, 0};
sensor probes[3] = {{'x', 1, 0, 0}, {'y', 2, 0, 0}, {'z', 3, 0, 0}};

char script[] = R"EOF(
// sensor comes from the sketch: no definition here
void main()
{
   probe.count = probe.count + 1;
   probe.value = probe.value * 3;
   probe.average = probe.value / 4.0;
   printfln("script: probe %c value %d count %d", probe.id, probe.value, probe.count);

   for (int i = 0; i < 3; i++)
   {
      probes[i].value = probes[i].value * 100;
      probes[i].count = i + 7;
      probes[i].average = probes[i].value / 8.0;
   }
   // and read back: 8 + 9
   probe.count = probes[1].count + probes[2].count;
}
)EOF";

void setup()
{
   Serial.begin(115200);
   delay(1000);
   printf("START\n");

   bindStruct(sensor_definition);
   bindVariable((char *)"sensor", (char *)"probe", NULL, (void *)&probe);
   bindVariable((char *)"sensor", (char *)"probes", (char *)"[3]", (void *)probes);

   static ScriptExecutable exec = parseScript(script);
   if (!exec.isExeExists())
   {
      showCompileError();
      printf("RESULT FAIL\n");
      return;
   }
   exec.execute("main");

   // what the sketch sees after the script ran
   printf("sketch: probe %c value %d count %u average %.2f\n", probe.id, probe.value, probe.count, probe.average);
   for (int i = 0; i < 3; i++)
      printf("sketch: probes[%d] %c value %d count %u average %.2f\n", i, probes[i].id, probes[i].value,
             probes[i].count, probes[i].average);
   bool ok = probe.value == 30 && probe.count == 17 && probe.average == 7.5f;
   for (int i = 0; i < 3; i++)
      ok = ok && probes[i].value == (i + 1) * 100 && probes[i].count == i + 7 &&
           probes[i].average == (i + 1) * 12.5f;
   printf("RESULT %s\n", ok ? "PASS" : "FAIL");
}

void loop()
{
   delay(1000);
}
