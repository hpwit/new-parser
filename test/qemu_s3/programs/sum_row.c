#include <stdio.h>
#include <stdint.h>
void emit(int v){printf("EMIT %d\n",v);}
uint8_t copy[128][128]; uint8_t hs[3][128];
int main(){uint8_t*f=&copy[0][0]; for(int h=0;h<128*128;h++) f[h]=(h*37+11)%251;
for(int i=1;i<127;i++) hs[1][i]=copy[5][i-1]+copy[5][i]+copy[5][i+1];
for(int i=0;i<128;i++) emit(hs[1][i]); printf("DONE\n");}
