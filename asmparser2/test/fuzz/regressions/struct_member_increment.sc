// ++/-- on an int struct member inside a method stepped by 4 (the member
// is read through `this`, marked like a pointer), so hits counted 4 per ++.
external void emit(int v);
struct counter
{
   int hits;
   int down;
   void bump()
   {
      hits++;
      down--;
   }
   counter()
   {
      hits = 0;
      down = 100;
   }
};
counter cs[2];
void main()
{
   cs[0].bump();
   cs[0].bump();
   cs[1].bump();
   emit(cs[0].hits);
   emit(cs[0].down);
   emit(cs[1].hits);
}
