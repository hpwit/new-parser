// ++/-- on an int struct member inside a method stepped by 4 (the member
// is read through `this`, marked like a pointer), so hits counted 4 per ++.
#include <stdio.h>
#include <stdint.h>
// Must match runner_fuzz.c's host functions.
static void emit(int v) { printf("EMIT %d\n", v); }
typedef struct { int hits, down; } counter;
counter cs[2];
static void bump(counter *c) { c->hits++; c->down--; }
int main()
{
   for (int i = 0; i < 2; i++) { cs[i].hits = 0; cs[i].down = 100; }
   bump(&cs[0]);
   bump(&cs[0]);
   bump(&cs[1]);
   emit(cs[0].hits);
   emit(cs[0].down);
   emit(cs[1].hits);
   printf("DONE\n");
   return 0;
}
