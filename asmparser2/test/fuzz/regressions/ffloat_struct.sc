// ffloat struct members: += and comparisons in a method, a struct array, a member in a cast expression -- raw Q16.16 bits against a C model.
external void emit(int v);
external void emitf(ffloat v);
struct mover
{
   ffloat x, v, lim;
   int hits;
   void step()
   {
      x += v;
      if (x >= lim - 0.5)
      {
         x = lim - 0.5;
         v = -v;
         hits++;
      }
      if (x < 1.25)
      {
         x = 1.25;
         v = -v;
         hits++;
      }
   }
   mover()
   {
      x = 2;
      v = 0.7;
      lim = 10.3;
      hits = 0;
   }
};
mover ms[2];
void main()
{
   ms[1].v = -1.35;
   for (int k = 0; k < 40; k++)
   {
      ms[0].step();
      ms[1].step();
   }
   emitf(ms[0].x);
   emitf(ms[1].x);
   emitf(ms[0].v);
   emit(ms[0].hits);
   emit(ms[1].hits);
   emit((int)(2 * ms[0].x - 3));
}
