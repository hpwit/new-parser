// == and != between uint32_t values: the compiler asked for bequ/bneu, which Xtensa doesn't have (an assembler error, 'Opcode bneu not found', for if (rand(4) == 0)); with < and >= still unsigned.
external void emit(int v);
uint32_t u;
uint32_t v;
int i;
void main()
{
   u = 7;
   v = 4000000000;
   i = -1;
   for (int k = 0; k < 10; k++)
   {
      if (u == 7)
         emit(1);
      if (u != 7)
         emit(2);
      if (v == 4000000000)
         emit(3);
      if (v != u)
         emit(4);
      if (u < v)
         emit(5);
      if (v >= u)
         emit(6);
      if (u == k)
         emit(7);
      u = u + 1;
      v = v - 1;
   }
}
