// char *names[] = {"red", ...}: an array of pointers (it used to be an array of char), stored at load, for (s in names), a char passed as a number
external void emit(int v);
char *names[] = {"red", "green", "blue"};
char *more[4];
int len(char *s)
{
   int n = 0;
   while (s[n] != 0)
      n++;
   return n;
}
void main()
{
   more[0] = "abc";
   more[3] = names[2];
   for (int i = 0; i < 3; i++)
      emit(len(names[i]));
   char *t = names[1];
   emit(t[0]);
   emit(t[4]);
   char *m = more[0];
   emit(m[1]);
   emit(len(more[3]));
   for (s in names)
      emit(s[0]);
   for (char *s : names) { emit(len(s) * 10); }
}
