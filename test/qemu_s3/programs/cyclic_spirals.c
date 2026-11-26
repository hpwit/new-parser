#include <stdio.h>
#include <stdint.h>
void emit(int v){printf("EMIT %d\n",v);}
#define width 64
#define height 48
uint8_t cur[height][width], nxt[height][width], glow[height][width], idx[height][width];
uint32_t seed=12345;
static void cyc(uint8_t a[height][width], uint8_t b[height][width], int j){
 for(int i=1;i<width-1;i++){uint8_t c=a[j][i]; uint8_t n=(c+1)&15;
  uint8_t hit=(a[j][i-1]==n)|(a[j][i+1]==n)|(a[j-1][i]==n)|(a[j+1][i]==n); b[j][i]=(c+hit)&15;}}
static void glowRow(int j){for(int i=0;i<width;i++){uint8_t ch=nxt[j][i]!=cur[j][i]; glow[j][i]=((0-ch)&15)|(glow[j][i]-(glow[j][i]>0));}}
static void index(uint8_t a[height][width],int j){for(int i=0;i<width;i++){uint8_t g2=glow[j][i]+glow[j][i]; uint8_t g4=g2+g2; uint8_t g8=g4+g4; idx[j][i]=a[j][i]+g8+g8;}}
int random16(){seed=seed*1103515245u+12345u; return (seed>>16)&15;}
int main(){uint8_t*c=&cur[0][0],*n=&nxt[0][0],*g=&glow[0][0],*x=&idx[0][0];
 for(int h=0;h<width*height;h++){c[h]=random16(); n[h]=c[h]; g[h]=0;}
 for(int f=0;f<60;f++){
  if((f&1)==0){for(int j=1;j<height-1;j++) cyc(cur,nxt,j); for(int j=0;j<height;j++){glowRow(j); index(nxt,j);}}
  else{for(int j=1;j<height-1;j++) cyc(nxt,cur,j); for(int j=0;j<height;j++){glowRow(j); index(cur,j);}}
  if(f%10==9){int sum=0; for(int h=0;h<width*height;h++) sum+=x[h]*(h%89+1); emit(sum);}}
 printf("DONE\n"); return 0;}
