// Structs as function arguments, by value and by pointer, checked
// against the same code compiled by GCC: every script function below has
// a C twin, and both run on the same data. Also the sketch calling a
// script function with a pointer to one of its own structs.
#include "script_executable.h"
#include "binding.h"
#include "arguments.h"

SCRIPT_STRUCT(key_t2, { char key; char code; })
SCRIPT_STRUCT(particle, {
   char tag;
   int x;
   uint16_t life;
   float speed;
})

particle hostParts[3];
particle hostOne = {'o', 5, 6, 1.0f};
particle scriptParts[4]; // the script's own array, copied out at the end
int results[7];

char script[] = R"EOF(
particle parts[4];

int sumByValue(particle p)
{
   p.x = p.x * 2;          // the caller's copy is untouched
   return p.x + p.life + p.tag;
}

void move(particle *p, int n)
{
   for (int i = 0; i < n; i++)
   {
      p[i].x = p[i].x + 10;
      p[i].life = p[i].life - 1;
      p[i].speed = p[i].speed * 2.0;
   }
}

void setTag(particle *p, int t)
{
   p->tag = t;
}

// &p[i] and p[i] by value, through a pointer parameter
int viaPointer(particle *p)
{
   setTag(&p[1], 88);
   return sumByValue(p[1]);
}

int keyboard(key_t2 *k)
{
   return k->code * 256 + k->key;
}

void main()
{
   for (int i = 0; i < 4; i++)
   {
      parts[i].tag = 65 + i;
      parts[i].x = i * 7;
      parts[i].life = 100 + i;
      parts[i].speed = 1.5;
   }
   results[0] = sumByValue(parts[1]);
   results[1] = parts[1].x;
   move(parts, 4);
   move(hostParts, 3);
   move(&parts[2], 1);
   setTag(&parts[0], 90);
   particle loc;
   loc.tag = 1;
   loc.x = 5;
   loc.life = 6;
   results[2] = sumByValue(loc);
   setTag(&loc, 76);
   results[3] = loc.tag;
   int *px = &parts[3].x;
   *px = *px + 100;
   results[4] = *px;
   results[5] = sumByValue(hostParts[2]);
   results[6] = viaPointer(hostParts);
   setTag(&hostParts[0], 77);
   setTag(&hostOne, 33);
   move(&hostOne, 1);
   for (int i = 0; i < 4; i++)
   {
      scriptParts[i].tag = parts[i].tag;
      scriptParts[i].x = parts[i].x;
      scriptParts[i].life = parts[i].life;
      scriptParts[i].speed = parts[i].speed;
   }
}
)EOF";

// the C twins
static int c_sumByValue(particle p)
{
   p.x = p.x * 2;
   return p.x + p.life + p.tag;
}
static void c_move(particle *p, int n)
{
   for (int i = 0; i < n; i++)
   {
      p[i].x = p[i].x + 10;
      p[i].life = p[i].life - 1;
      p[i].speed = p[i].speed * 2.0f;
   }
}
static void c_setTag(particle *p, int t) { p->tag = t; }
static int c_viaPointer(particle *p)
{
   c_setTag(&p[1], 88);
   return c_sumByValue(p[1]);
}

static void initHost(particle *h)
{
   for (int i = 0; i < 3; i++)
      h[i] = {(char)('a' + i), 1000 * i, (uint16_t)(50 + i), 0.25f};
}

static bool sameParticle(const particle &a, const particle &b)
{
   return a.tag == b.tag && a.x == b.x && a.life == b.life && a.speed == b.speed;
}

void setup()
{
   Serial.begin(115200);
   delay(1000);
   printf("START\n");

   bindStruct(key_t2_definition);
   bindStruct(particle_definition);
   bindVariable((char *)"particle", (char *)"hostParts", (char *)"[3]", (void *)hostParts);
   bindVariable((char *)"particle", (char *)"scriptParts", (char *)"[4]", (void *)scriptParts);
   bindVariable((char *)"particle", (char *)"hostOne", NULL, (void *)&hostOne);
   bindVariable((char *)"int", (char *)"results", (char *)"[7]", (void *)results);

   initHost(hostParts);
   static ScriptExecutable exec = parseScript(script);
   if (!exec.isExeExists())
   {
      showCompileError();
      printf("RESULT FAIL\n");
      return;
   }
   exec.execute("main");

   // the same steps in C
   particle parts[4], host[3];
   int expect[7];
   initHost(host);
   for (int i = 0; i < 4; i++)
      parts[i] = {(char)(65 + i), i * 7, (uint16_t)(100 + i), 1.5f};
   expect[0] = c_sumByValue(parts[1]);
   expect[1] = parts[1].x;
   c_move(parts, 4);
   c_move(host, 3);
   c_move(&parts[2], 1);
   c_setTag(&parts[0], 90);
   particle loc = {1, 5, 6, 0};
   expect[2] = c_sumByValue(loc);
   c_setTag(&loc, 76);
   expect[3] = loc.tag;
   int *px = &parts[3].x;
   *px = *px + 100;
   expect[4] = *px;
   expect[5] = c_sumByValue(host[2]);
   expect[6] = c_viaPointer(host);
   c_setTag(&host[0], 77);
   particle one = {'o', 5, 6, 1.0f};
   c_setTag(&one, 33);
   c_move(&one, 1);

   bool ok = true;
   for (int i = 0; i < 7; i++)
   {
      printf("result %d: script %d, C %d\n", i, results[i], expect[i]);
      ok = ok && results[i] == expect[i];
   }
   for (int i = 0; i < 4; i++)
   {
      printf("parts[%d]: script %c %d %u %.2f, C %c %d %u %.2f\n", i, scriptParts[i].tag, scriptParts[i].x,
             scriptParts[i].life, scriptParts[i].speed, parts[i].tag, parts[i].x, parts[i].life, parts[i].speed);
      ok = ok && sameParticle(scriptParts[i], parts[i]);
   }
   for (int i = 0; i < 3; i++)
   {
      printf("hostParts[%d]: script %c %d %u %.2f, C %c %d %u %.2f\n", i, hostParts[i].tag, hostParts[i].x,
             hostParts[i].life, hostParts[i].speed, host[i].tag, host[i].x, host[i].life, host[i].speed);
      ok = ok && sameParticle(hostParts[i], host[i]);
   }

   printf("hostOne: script %c %d %u %.2f, C %c %d %u %.2f\n", hostOne.tag, hostOne.x, hostOne.life, hostOne.speed,
          one.tag, one.x, one.life, one.speed);
   ok = ok && sameParticle(hostOne, one);

   // the sketch hands a script function a pointer to its own struct
   key_t2 k = {'q', 3};
   Arguments args;
   args.add((int)(uintptr_t)&k);
   int32_t r = 0;
   exec.execute("keyboard", &args, &r);
   printf("keyboard: script %d, C %d\n", (int)r, k.code * 256 + k.key);
   ok = ok && r == k.code * 256 + k.key;

   printf("RESULT %s\n", ok ? "PASS" : "FAIL");
}

void loop()
{
   delay(1000);
}
