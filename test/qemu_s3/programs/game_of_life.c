#include <stdio.h>
#include <stdint.h>
void emit(int v){printf("EMIT %d\n",v);}
#define width 128
#define height 128
uint8_t copy[height][width];
uint8_t hs[3][width];
void sumRow(int j,int s){for(int i=1;i<width-1;i++) hs[s][i]=copy[j][i-1]+copy[j][i]+copy[j][i+1];}
void ruleRow(int j,int a,int c,int b){for(int i=1;i<width-1;i++){uint8_t n=hs[a][i]+hs[c][i]+hs[b][i]; copy[j][i]=(n==3)|((n==4)&copy[j][i]);}}
void step(){int a=0,c=1,b=2; sumRow(0,a); sumRow(1,c); for(int j=1;j<height-1;j++){sumRow(j+1,b); ruleRow(j,a,c,b); int t=a;a=c;c=b;b=t;}}
int main(){uint8_t *f=&copy[0][0]; for(int h=0;h<width*height;h++) f[h]=0; for(int h=0;h<height;h++) copy[h][width/2]=1;
 for(int h=0;h<width;h++){copy[height/2][h]=1; copy[(h*7)%height][(h*13)%width]=1;}
 for(int g=0;g<20;g++){step(); int sum=0; for(int h=0;h<width*height;h++) sum+=f[h]*(h%97+1); emit(sum);} printf("DONE\n"); return 0;}
