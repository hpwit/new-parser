// a global char * given a string is a pointer, as in C: it held the characters themselves, so reading it as a pointer, word + 3 and word = "zz" went wrong
external void emit(int v);
char *word = "pointer";
char arr[] = "abc";
char fixed[8] = "xy";
char *other;
int len(char *s)
{
   int n = 0;
   while (s[n] != 0)
      n++;
   return n;
}
void main()
{
   emit(word[0]);
   emit(len(word));
   char *w = word + 3;
   emit(w[0]);
   emit(arr[1]);
   emit(len(arr));
   emit(fixed[1]);
   other = word;
   emit(other[1]);
   word = "zz";
   emit(word[0]);
   emit(len(word));
   emit(other[2]);
   emit(len("hello"));
}
