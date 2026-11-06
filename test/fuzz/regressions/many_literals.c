// More than 16 literal-pool constants: the harness used to drop every initialized-data record past the 16th, so they read back 0.
#include <stdio.h>
#include <stdint.h>
// Must match runner_fuzz.c's host functions.
static void emit(int v) { printf("EMIT %d\n", v); }
int h(int v)
{
   return v + 100000 + 100007 + 100014 + 100021 + 100028 + 100035 + 100042 + 100049 + 100056 + 100063 + 100070 + 100077 + 100084 + 100091 + 100098 + 100105 + 100112 + 100119 + 100126 + 100133 + 100140 + 100147 + 100154 + 100161 + 100168 + 100175 + 100182 + 100189 + 100196 + 100203 + 100210 + 100217 + 100224 + 100231 + 100238 + 100245 + 100252 + 100259 + 100266 + 100273;
}
int main()
{
   emit(h(1));
   emit(186516);
   printf("DONE\n");
   return 0;
}
