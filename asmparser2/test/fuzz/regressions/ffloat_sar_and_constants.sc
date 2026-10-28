// ffloat multiplies share SAR (ssai 16 dropped while SAR is known to be 16): variable shifts (ssl / wsr sar) and calls between multiplies must make the next multiply set it again; constants built as movi + slli (4.0, 0.75, -2048.0, -32768.0...) -- raw Q16.16 bits against a C model.
external void emit(int v);
external void emitf(ffloat v);

int shifty(int v, int n)
{
   return (v << n) + (v >> n);
}

void main()
{
   ffloat x = 3.25;
   ffloat y = -1.75;
   int n = 5;
   int m = 3;
   for (int i = 0; i < 3; i++)
   {
      ffloat p = x * y;
      int s = i << n;
      ffloat q = p * x;
      int t = 4000 >> m;
      ffloat r = q * y;
      emit(s);
      emit(t);
      emitf(p);
      emitf(q);
      emitf(r);
      ffloat u = x * y + (x >> m) * y;
      emitf(u);
      ffloat w = x * x;
      int z = shifty(i + 1, m);
      w = w * y;
      emit(z);
      emitf(w);
      ffloat chain = x * y * x * 0.5 * y;
      emitf(chain);
      x = x + 0.5;
   }
   ffloat c[10];
   c[0] = 4;
   c[1] = 0.75;
   c[2] = -1.5;
   c[3] = 1024;
   c[4] = -2048;
   c[5] = -32768;
   c[6] = 2047.5;
   c[7] = 0.5;
   c[8] = 3;
   c[9] = -0.25;
   for (int k = 0; k < 10; k++)
   {
      emitf(c[k]);
      emitf(c[k] * x);
   }
   if (x * y <= 4)
      emit(1);
   else
      emit(0);
}
