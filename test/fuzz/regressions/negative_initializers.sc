// negative values in global array initializers (int, uint8_t, float, signs and parentheses): they used to be stored as 0, so a direction table {0, -1, 0, 1} moved nowhere.
external void emit(int v);
int dxs[4] = {0, -1, 0, 1};
int dys[4] = {-1, 0, 1, 0};
int big[3] = {-300000, 300000, - 2};
int wrapped[2] = {-(3), +4};
uint8_t bytes[3] = {-1, 200, -56};
float f[3] = {-1.5, 2.25, -0.125};

void main()
{
   int x = 5;
   int y = 5;
   for (int d = 0; d < 4; d++)
   {
      x = x + dxs[d] * (d + 1);
      y = y + dys[d] * (d + 2);
      emit(x * 100 + y);
   }
   for (int i = 0; i < 3; i++)
      emit(big[i]);
   emit(wrapped[0]);
   emit(wrapped[1]);
   for (int i = 0; i < 3; i++)
      emit(bytes[i]);
   for (int i = 0; i < 3; i++)
      emit((int)(f[i] * 1000));
}
