// C reference of string_arrays.sc
#include <stdio.h>
#include <string.h>
void emit(int v){printf("EMIT %d\n",v);}
char *names[] = {"red", "green", "blue"}; char *more[4];
int main(){ more[0]="abc"; more[3]=names[2];
 for(int i=0;i<3;i++) emit(strlen(names[i]));
 char *t=names[1]; emit(t[0]); emit(t[4]); emit(more[0][1]); emit(strlen(more[3]));
 for(int i=0;i<3;i++) emit(names[i][0]);
 for(int i=0;i<3;i++) emit(strlen(names[i])*10);
 printf("DONE\n"); return 0;}
