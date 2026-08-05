// A more realistic ESPLiveScript program: a struct with a constructor and
// two member functions, nested for loops, +=, the ^ (power) operator, an
// array of structs, float division, four host-bound calls (rand/setPixel/
// clear/show) and one compiler built-in (printfln -- see setup()'s own
// comment on why it must NOT be bound here).
//
// Three real compiler gaps used to block this script from ever reaching
// createBinary() successfully, let alone running it reliably; all three
// are now fixed:
//
//  1. Float division (e.g. `rand(300) / 255`) compiles to a call to a
//     hand-assembled `__div` helper (Xtensa has no hardware float divide).
//     The codegen and injection logic for it already existed but was
//     commented out in visitnode.cpp, waiting on the actual instruction
//     data from upstream ESPLiveScript's functionlib.h, which had never
//     been ported -- now added there. Assembling `__div`'s own body also
//     needed 8 Xtensa FP reciprocal/division-support opcodes (div0.s,
//     nexp01.s, maddn.s, mkdadj.s, addexpm.s, addexp.s, const.s, divn.s):
//     their encoders/operand tables were already ported to
//     asm_encoders.h, just never wired into asm_parser.cpp's mnemonic
//     dispatch.
//  2. bindFunction()-only registration (no explicit `external` in script
//     text) has a *separate*, still-unfixed gap: the parser's auto-
//     declare path (parser.cpp's getFunction(), ~line 991) synthesizes
//     the declaration into a throwaway scratch tree (`extra_parser`)
//     that's cleared right after resolving the call site, so the
//     assembler's jump-table reservation for it (visitnode.cpp's
//     _visitdefExtFunctionNode) never gets visited. Worked around the
//     same way as every other example here: declare each host-bound
//     function `external` explicitly in the script text too.
//  3. Once both of those were out of the way, this script's assembly
//     *was* well-formed (1368 bytes of instructions, no assembler
//     errors) but Parser::parse() itself reliably crashed first -- a
//     script this size (struct with a constructor + 2 methods, an array
//     of structs, several external declarations, nested loops) reliably
//     triggered a real, pre-existing bug: nodetoken.h's `children` used
//     to be an inline array of NodeToken values, grown via realloc --
//     which can move, silently invalidating any raw NodeToken* held into
//     it. Only a handful of the many such pointers scattered across
//     parser.cpp were patched up after each move; the rest (current_cntx
//     among them) were left to dangle. `children` is now an array of
//     pointers to individually heap-allocated nodes instead, so growing
//     it never moves an existing node -- see nodetoken.h's comment on
//     `children` for the full explanation. Confirmed via 30/30 clean
//     runs of this exact script's full create+execute pipeline
//     afterward (0/30 before).
//
// The pipeline below uses parseScript()/ScriptExecutable
// (script_executable.h) rather than spelling out parse -> createBinary ->
// createExecutableFromBinary by hand, and produces a real, executable
// binary reliably.
#include "script_executable.h"
#include "binding.h"

int scriptRand(uint32_t s)
{
   return rand() % (s + 1);
}

void scriptSetPixel(int x, int y, int c)
{
   printf("setPixel(%d,%d,%d)\n", x, y, c);
}

void scriptClear()
{
   printf("clear()\n");
}

void scriptShow()
{
   printf("show()\n");
}

char script[] = R"EOF(
external uint32_t rand(uint32_t s);
external void setPixel(int x, int y, int c);
external void clear();
external void show();
// printfln is a compiler built-in (see runtime_functions.h/README.md's
// "Built-in printf / printfln" and ScriptPrintf.ino) -- no `external`
// declaration or bindFunction() call needed, unlike every other host
// call in this script. See setup()'s own comment on the bindFunction()
// call this used to have here, before printf/printfln became built-in.

#define max_nb_balls 20
#define rmax 8
#define rmin 8
#define width 384
#define height 168
#define true 1

struct ball
{
   float vx, vy, xc, yc, r;
   int color;
   void drawBall()
   {
      int startx = xc - r;
      int r2 = r * r;
      int starty = yc - r;
      int _xc = xc;
      int _yc = yc;
      for (int i = startx; i <= _xc; i++)
         for (int j = starty; j <= _yc; j++)
         {
            int distance = (i - xc) ^ 2 + (j - yc) ^ 2;
            if (distance <= r2)
            {
               setPixel(i,j,1);
               setPixel((int)(2 * xc - i),j,1);
               setPixel((int)(2 * xc - i),(int)(2 * yc - j),1);
               setPixel(i,(int)(2 * yc - j),1);
            }
         }
   }
   void updateBall()
   {
      xc += vx;
      yc += vy;
      if (xc >= (width - r - 1))
      {
         xc = width - r - 1;
         vx = -vx;
      }
      if (xc < (r + 1))
      {
         xc = r + 1;
         vx = -vx;
      }
      if (yc >= height - r - 1)
      {
         yc = height - r - 1;
         vy = -vy;
      }
      if (yc < r + 1)
      {
         yc = r + 1;
         vy = -vy;
      }
      drawBall();
   }
   ball()
   {
      vx = rand(300) / 255 + 0.7;
      vy = rand(280) / 255 + 0.5;
      r = (rmax - rmin) * (rand(280) / 180) + rmin;
      xc = width / 2 * (rand(280) / 255 + 0.3) + 15;
      yc = height / 2 * (rand(280) / 255 + 0.3) + 15;
      color = rand(255);
   }
}

ball Balls[max_nb_balls];

void main()
{
   int num = 8;
   if (num > max_nb_balls)
   {
      num = max_nb_balls;
   }
   if (num <= 0)
   {
      num = 1;
   }
   printfln("numberof balls:%d",num);
   uint32_t h;
   while (true)
   {
      clear();
      for (int i = 0; i < num; i++)
         Balls[i].updateBall();
      h++;
      show();
   }
}
)EOF";

void setup()
{
   Serial.begin(115200);

   bindFunction((char *)"uint32_t", (char *)"rand", (char *)"uint32_t", (void *)scriptRand);
   bindFunction((char *)"void", (char *)"setPixel", (char *)"int,int,int", (void *)scriptSetPixel);
   bindFunction((char *)"void", (char *)"clear", NULL, (void *)scriptClear);
   bindFunction((char *)"void", (char *)"show", NULL, (void *)scriptShow);
   // Do NOT bindFunction() printfln here: it's a compiler built-in (see
   // the script's own comment above), auto-registered the first time
   // anything is parsed -- but only if nothing has already bound that
   // name first (registerBuiltinRuntimeFunctions(), runtime_functions.cpp,
   // skips it via findLink() if so). This used to bind printfln to NULL
   // ("left unresolved... a companion loader would bind it"), which
   // shadowed the real built-in and left main()'s very first statement
   // (printfln("numberof balls:%d",num);) calling through a null
   // function pointer -- a real, reproduced-on-hardware crash (Guru
   // Meditation Error: InstrFetchProhibited, PC=0x00000000).

   ScriptExecutable exec = parseScript(script);
   if (!exec.isExeExists())
   {
      return;
   }

   // execute("main") calls through callFunction() (see script_executable.h)
   // rather than runExecutable()'s own wrapper path -- equivalent here
   // since main() takes no arguments (no wrapper even exists for a
   // 0-argument function, see callFunction()'s own doc comment), except
   // that runExecutable() also pre-zeroes two data-region scratch words
   // some scripts' sync() expects; this script doesn't call sync(), so
   // it doesn't matter here. main()'s own while(true) never returns --
   // this call does not either.
   exec.execute("main");
}

void loop()
{
}
