// Demonstrates a simple dynamic-memory design for scripts: this DSL has
// no pointer/dereference syntax of its own, so a heap allocation is bound
// as four ordinary externals -- alloc()/free() wrapping real malloc()/
// free(), plus writeInt()/readInt() accessor externals standing in for
// the `*(int*)(buf+offset) = v` a script can't write directly. A
// script-held uint32_t is just the real heap address, reinterpreted --
// the same trick every other external pointer in this repo already uses
// (an internal global's address, an external array's base -- see
// StructsAndHostBindings.ino).
//
// The script allocates a 10-int block, writes i*i into each slot via
// writeInt(), sums them back via readInt(), and frees the block --
// exercising the full alloc -> write -> read -> free round trip under
// real Xtensa calling-convention rules, not just a host-side structural
// check. Expected output: sum of squares 0..9 = 285.
#include "script_executable.h"
#include "binding.h"

static uint32_t scriptAlloc(int size)
{
   return (uint32_t)(uintptr_t)malloc((size_t)size);
}
static void scriptFree(uint32_t p)
{
   free((void *)(uintptr_t)p);
}
static void scriptWriteInt(uint32_t p, int offset, int value)
{
   *(int32_t *)((uint8_t *)(uintptr_t)p + offset) = value;
}
static int scriptReadInt(uint32_t p, int offset)
{
   return *(int32_t *)((uint8_t *)(uintptr_t)p + offset);
}

char script[] = R"EOF(
external uint32_t alloc(int size);
external void free(uint32_t p);
external void writeInt(uint32_t p, int offset, int value);
external int readInt(uint32_t p, int offset);

void main()
{
   uint32_t b;
   b = alloc(40);
   printfln("b=%u", b);
   int i;
   for (i = 0; i < 10; i++)
      writeInt(b, i*4, i*i);
   int sum;
   sum = 0;
   for (i = 0; i < 10; i++)
      sum = sum + readInt(b, i*4);
   printfln("sum of squares 0..9 = %d", sum);
   free(b);
}
)EOF";

void setup()
{
   Serial.begin(115200);

   bindFunction((char *)"uint32_t", (char *)"alloc", (char *)"int", (void *)scriptAlloc);
   bindFunction((char *)"void", (char *)"free", (char *)"uint32_t", (void *)scriptFree);
   bindFunction((char *)"void", (char *)"writeInt", (char *)"uint32_t,int,int", (void *)scriptWriteInt);
   bindFunction((char *)"int", (char *)"readInt", (char *)"uint32_t,int", (void *)scriptReadInt);

   ScriptExecutable exec = parseScript(script);
   if (!exec.isExeExists())
   {
      return;
   }

   exec.execute("main");
   printf("********** done **********\n");
}

void loop()
{
}
