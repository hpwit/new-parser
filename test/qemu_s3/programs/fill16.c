#include <stdio.h>
#include <stdint.h>
void emit(int v){printf("EMIT %d\n",v);}
uint16_t a[400]; int16_t b[200]; uint32_t seed=99;
int rnd(int n){seed=seed*1103515245u+12345u; return ((seed>>16)&32767)%n;}
void span(int o,int x0,int x1,int c){for(int x=x0;x<x1;x++) a[o+x]=c;}
int sumA(){int s=0; for(int h=0;h<400;h++) s+=a[h]*(h%37+1); return s;}
int main(){int live=7;
 for(int t=0;t<300;t++){int o=rnd(100); int x0=rnd(120)-10; int x1=x0+rnd(80)-10;
  if(o+x0<0) x0=0-o; if(o+x1>400) x1=400-o; int c=rnd(70000)-3000; span(o,x0,x1,c); live+=t;
  if(t%30==29) emit(sumA()+live);}
 for(int y=0;y<2;y++){int o=y*200; for(int x=3+y;x<190-y;x++) a[o+x]=1000+y; for(int x=0;x<1;x++) a[x+o]=5;}
 emit(sumA());
 for(int k=0;k<13;k++){for(int i=k;i<200-k*3;i++) b[i]=-5-k; int s=0; for(int h=0;h<200;h++) s+=b[h]*(h+1); emit(s);}
 emit(live); printf("DONE\n"); return 0;}
