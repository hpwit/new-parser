// C reference of for_in_array.sc
#include <stdio.h>
#include <stdint.h>
void emit(int v){printf("EMIT %d\n",v);}
int arr[] = {5, 6, 7, 8}; uint8_t small[] = {200, 100, 3}; float fl[] = {1.5, -2.25, 4.0}; int tbl[6] = {1,2,3}; int g;
int sum(int k){int s=0; for(int i=0;i<4;i++){int v=arr[i]; s=s+v*k;} return s;}
int main(){
 for(int i=0;i<4;i++) emit(arr[i]);
 for(int i=0;i<4;i++) emit(arr[i]*10);
 for(int i=0;i<3;i++) emit(small[i]);
 for(int i=0;i<3;i++) emit((int)(fl[i]*4));
 for(int i=0;i<4;i++){int x=arr[i]; if(x==6) continue; if(x==8) break; emit(x+100);}
 for(int i=0;i<4;i++) for(int j=0;j<3;j++) emit(arr[i]*1000+small[j]);
 g=0; for(int i=0;i<6;i++){g=tbl[i]; emit(g);} emit(g);
 emit(sum(3));
 int n=0; for(int i=0;i<6;i++) n+=tbl[i]; emit(n);
 printf("DONE\n"); return 0;}
