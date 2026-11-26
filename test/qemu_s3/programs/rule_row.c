#include <stdio.h>
#include <stdint.h>
void emit(int v){printf("EMIT %d\n",v);}
uint8_t copy[128][128]; uint8_t hs[3][128];
int main(){uint8_t*f=&copy[0][0]; for(int h=0;h<128*128;h++) f[h]=(h*37+11)%3; uint8_t*g=&hs[0][0]; for(int h=0;h<3*128;h++) g[h]=(h*53+7)%4;
for(int i=1;i<127;i++){uint8_t n=hs[0][i]+hs[1][i]+hs[2][i]; copy[5][i]=(n==3)|((n==4)&copy[5][i]);}
for(int i=0;i<128;i++) emit(copy[5][i]); printf("DONE\n");}
