// ffloat struct members: += and comparisons in a method, a struct array, a member in a cast expression -- raw Q16.16 bits against a C model.
#include <stdio.h>
#include <stdint.h>
typedef int32_t ff;
static ff lit(double v) { double f = v * 65536.0; f = f < 0 ? f - 0.5 : f + 0.5; return (ff)f; }
static ff fromInt(int x) { return (ff)((uint32_t)x << 16); }
static ff mul(ff a, ff b) { return (ff)(((int64_t)a * b) >> 16); }
typedef struct { ff x, v, lim; int hits; } mover;
static void step(mover *m)
{
   m->x += m->v;
   if (m->x >= m->lim - lit(0.5)) { m->x = m->lim - lit(0.5); m->v = -m->v; m->hits++; }
   if (m->x < lit(1.25)) { m->x = lit(1.25); m->v = -m->v; m->hits++; }
}
int main()
{
   mover ms[2];
   for (int i = 0; i < 2; i++) { ms[i].x = fromInt(2); ms[i].v = lit(0.7); ms[i].lim = lit(10.3); ms[i].hits = 0; }
   ms[1].v = lit(-1.35);
   for (int k = 0; k < 40; k++) { step(&ms[0]); step(&ms[1]); }
   printf("EMIT %d\nEMIT %d\nEMIT %d\n", ms[0].x, ms[1].x, ms[0].v);
   printf("EMIT %d\nEMIT %d\n", ms[0].hits, ms[1].hits);
   ff t = mul(lit(2), ms[0].x) - fromInt(3);
   printf("EMIT %d\nDONE\n", t / 65536);
   return 0;
}
