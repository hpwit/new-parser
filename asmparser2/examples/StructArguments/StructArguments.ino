// Structures as function arguments.
//
// By value (`ball b`): the function gets its own copy, as in C.
// By pointer (`ball *b`): the function works on the caller's structure;
// reach the fields with b->x, or b[i].x for the i-th of an array.
// &variable gives a pointer: &g, &arr[i], &g.field.
//
// It also shows the sketch calling a script function with a pointer to
// one of its own structs, as a keyboard handler would.
#include "script_executable.h"
#include "binding.h"
#include "arguments.h"

SCRIPT_STRUCT(ball, {
   int x;
   int y;
   int dx;
   int dy;
})

SCRIPT_STRUCT(key_event, {
   char key;
   char code;
})

char script[] = R"EOF(
ball balls[3];

// by value: changes stay in the copy
int distance2(ball b, int x, int y)
{
   b.x = b.x - x;
   b.y = b.y - y;
   return b.x * b.x + b.y * b.y;
}

// by pointer: changes the caller's ball
void step(ball *b)
{
   b->x = b->x + b->dx;
   b->y = b->y + b->dy;
   if (b->x < 0 || b->x > 100)
      b->dx = -b->dx;
   if (b->y < 0 || b->y > 100)
      b->dy = -b->dy;
}

// a pointer to the first of n balls
void stepAll(ball *b, int n)
{
   for (int i = 0; i < n; i++)
      step(&b[i]);
}

void main()
{
   for (int i = 0; i < 3; i++)
   {
      balls[i].x = 10 * i;
      balls[i].y = 50;
      balls[i].dx = 3 + i;
      balls[i].dy = 2;
   }
   for (int t = 0; t < 20; t++)
      stepAll(balls, 3);
   for (int i = 0; i < 3; i++)
      printfln("ball %d at %d,%d, distance2 to the center %d", i, balls[i].x, balls[i].y, distance2(balls[i], 50, 50));

   ball one;
   one.x = 99;
   one.y = 50;
   one.dx = 5;
   one.dy = 0;
   step(&one);
   printfln("one bounced: x %d dx %d", one.x, one.dx);
}

// called by the sketch with a pointer to its own key_event
void keyboard(key_event *k)
{
   printfln("key '%c' code %d", k->key, k->code);
}
)EOF";

void setup()
{
   Serial.begin(115200);
   delay(1000);

   bindStruct(ball_definition);
   bindStruct(key_event_definition);

   static ScriptExecutable exec = parseScript(script);
   if (!exec.isExeExists())
   {
      showCompileError();
      return;
   }
   exec.execute("main");

   key_event k = {'a', 4};
   Arguments args;
   args.add((int)(uintptr_t)&k);
   exec.execute("keyboard", &args);
}

void loop()
{
   delay(1000);
}
