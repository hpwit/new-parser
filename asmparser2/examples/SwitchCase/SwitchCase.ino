// switch / case / break / default, as in C.
//
// The script shows:
//   1. a state machine (a traffic light), every case ending with break;
//   2. several cases sharing one body (0 and 6), and a deliberate
//      fall-through from one case into the next (5 into default);
//   3. break and continue inside a loop: break leaves the switch only,
//      continue goes straight to the loop's next round;
//   and colorName(), which returns from inside its switch.
//
// Expected output:
//   light: red
//   light: green
//   light: yellow
//   light: red
//   day 0: weekend
//   day 1: work
//   day 2: work
//   day 3: work
//   day 4: work
//   day 5: friday!
//   day 5: work
//   day 6: weekend
//   i=1 sum=1
//   i=2 sum=101
//   i=4 sum=105
//   i=5 sum=205
//
// Case values are integer constants: numbers (negative or hex too) or
// #define'd names like RED below. A case body in braces can declare its
// own variables: case 1: { int t = ...; ... break; }
#include "script_executable.h"

char script[] = R"EOF(
#define RED 0
#define GREEN 1
#define YELLOW 2

char *colorName(int c)
{
   switch (c)
   {
   case RED:
      return "red";
   case GREEN:
      return "green";
   case YELLOW:
      return "yellow";
   default:
      return "?";
   }
}

void main()
{
   // 1. A state machine: every case ends with break.
   int light = RED;
   for (int step = 0; step < 4; step++)
   {
      printfln("light: %s", colorName(light));
      switch (light)
      {
      case RED:
         light = GREEN;
         break;
      case GREEN:
         light = YELLOW;
         break;
      case YELLOW:
         light = RED;
         break;
      }
   }

   // 2. Several cases sharing a body, and falling through on purpose.
   for (int day = 0; day < 7; day++)
   {
      switch (day)
      {
      case 0:
      case 6:
         printfln("day %d: weekend", day);
         break;
      case 5:
         printfln("day %d: friday!", day);
         // no break: carries on into default
      default:
         printfln("day %d: work", day);
      }
   }

   // 3. In a loop: break leaves the switch, continue goes to the next i.
   int sum = 0;
   for (int i = 0; i < 6; i++)
   {
      switch (i % 3)
      {
      case 0:
         continue;
      case 1:
         sum = sum + i;
         break;
      default:
         sum = sum + 100;
      }
      printfln("i=%d sum=%d", i, sum);
   }
}
)EOF";

void setup()
{
   Serial.begin(115200);

   ScriptExecutable exec = parseScript(script);
   if (exec.isExeExists())
   {
      exec.execute("main");
   }
   else
   {
      printf("script failed to compile/load\n");
   }
}

void loop()
{
}
