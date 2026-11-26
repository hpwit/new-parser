// C reference of global_char_pointers.sc
#include <stdio.h>
void emit(int v){printf("EMIT %d\n",v);}
char *word = "pointer";
char arr[] = "abc";
char fixed[8] = "xy";
char *other;
int len2(char *s)
{
   int n = 0;
   while (s[n] != 0)
      n++;
   return n;
}
int main()
{
   emit(word[0]);
   emit(len2(word));
   char *w = word + 3;
   emit(w[0]);
   emit(arr[1]);
   emit(len2(arr));
   emit(fixed[1]);
   other = word;
   emit(other[1]);
   word = "zz";
   emit(word[0]);
   emit(len2(word));
   emit(other[2]);
   emit(len2("hello"));
printf("DONE\n");}
