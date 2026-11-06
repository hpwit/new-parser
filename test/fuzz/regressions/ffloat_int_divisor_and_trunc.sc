// ffloat / int divisor (variable, array element, call, uint8_t) is a quos with a check for 0 (saturates like ___ffdiv); ffloat -> int truncates toward zero with srai + extui ..,0,16 -- raw Q16.16 bits and ints against a C model.
external void emit(int v);
external void emitf(ffloat v);
int iarr[4];
uint8_t small;

int pick(int k)
{
   return iarr[k];
}

void main()
{
   iarr[0] = 0;
   iarr[1] = 7;
   iarr[2] = -3;
   iarr[3] = 144;
   small = 12;
   ffloat vals[5];
   vals[0] = 1000.123;
   vals[1] = -0.75;
   vals[2] = 0;
   vals[3] = -31000.5;
   vals[4] = 2.5;
   for (int i = 0; i < 5; i++)
   {
      ffloat a = vals[i];
      for (int k = 0; k < 4; k++)
      {
         int n = iarr[k];
         emitf(a / n);
         emitf(a / iarr[k]);
         emitf(a / pick(k));
         emitf(-a / n + 1);
      }
      emitf(a / small);
      int t = a;
      emit(t);
      int u = a * 3 - 0.5;
      emit(u);
   }
   ffloat neg[6];
   neg[0] = -0.00002;
   neg[1] = -1;
   neg[2] = -1.5;
   neg[3] = -32768;
   neg[4] = -32767.99;
   neg[5] = 32767.99;
   for (int k = 0; k < 6; k++)
   {
      int v = neg[k];
      emit(v);
   }
   int zero = 0;
   ffloat q = -5;
   emitf(q / zero);
   emitf(0 / zero);
}
