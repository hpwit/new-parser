// a char * local stored in a large frame (a1 + 184): its address was an addi past 127, refused by the assembler
external void emit(int v);
char *colors[] = {"red", "green", "blue"};
int length(char *s) { int n = 0; while (s[n] != 0) n++; return n; }
void main()
{
   int v0 = 0;
   int v1 = 1;
   int v2 = 2;
   int v3 = 3;
   int v4 = 4;
   int v5 = 5;
   int v6 = 6;
   int v7 = 7;
   int v8 = 8;
   int v9 = 9;
   int v10 = 10;
   int v11 = 11;
   int v12 = 12;
   int v13 = 13;
   int v14 = 14;
   int v15 = 15;
   int v16 = 16;
   int v17 = 17;
   int v18 = 18;
   int v19 = 19;
   int v20 = 20;
   int v21 = 21;
   int v22 = 22;
   int v23 = 23;
   int v24 = 24;
   int v25 = 25;
   int v26 = 26;
   int v27 = 27;
   int v28 = 28;
   int v29 = 29;
   int shortest = 100;
   for (int i = 0; i < 3; i++)
   {
      char *c = colors[i];
      emit(c[0]);
      if (length(c) < shortest)
         shortest = length(c);
   }
   emit(shortest + v29);
   for (c2 in colors)
      emit(length(c2) + v28);
}
