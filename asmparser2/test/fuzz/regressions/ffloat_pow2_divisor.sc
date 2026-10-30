// ffloat / +-2^k literal is a shift truncated toward zero (srai, extui ..,0,k or srli for k > 16, add, srai, neg for a negative divisor) instead of quos -- 2..2^30, 1 and -1, negative, tiny (raw -1) and extreme dividends, raw Q16.16 bits against a C model.
external void emitf(ffloat v);

void main()
{
   ffloat vals[8];
   vals[0] = 1000.123;
   vals[1] = -1000.123;
   vals[2] = -0.00002;
   vals[3] = 0.00002;
   vals[4] = -32768;
   vals[5] = 32767.99;
   vals[6] = -3.5;
   vals[7] = 0;
   for (int i = 0; i < 8; i++)
   {
      ffloat a = vals[i];
      emitf(a / 1);
      emitf(a / -1);
      emitf(a / 2);
      emitf(a / -2);
      emitf(a / 8);
      emitf(a / 128);
      emitf(a / -128);
      emitf(a / 1024);
      emitf(a / 65536);
      emitf(a / -65536);
      emitf(a / 131072);
      emitf(a / 1048576);
      emitf(a / 1073741824);
      emitf(a / 0x4000);
      emitf((a + 1) / 4 - a / 16);
   }
}
