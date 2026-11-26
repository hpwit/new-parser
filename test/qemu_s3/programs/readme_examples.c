// C reference of readme_examples.sc
#include <stdio.h>
#include <stdint.h>
void emit(int v){printf("EMIT %d\n",v);}
#define width 100
#define rows 4
uint8_t heat[rows][width], front[width], back[width], level[rows][width], mask[rows][width], trail[width], layerA[width], layerB[width], sum[width];
int main(){
 uint8_t *h_=&heat[0][0], *l_=&level[0][0], *m_=&mask[0][0];
 for(int h=0;h<rows*width;h++){h_[h]=h%251; l_[h]=(h*37)%256; m_[h]=7;}
 for(int x=0;x<width;x++){front[x]=(x*13)%256; trail[x]=x%3; layerA[x]=(x*29)%256; layerB[x]=(x*71+5)%256;}
 for(int x=0;x<width;x++) heat[2][x]=0;
 for(int x=0;x<width;x++) back[x]=front[x];
 for(int x=0;x<width;x++) mask[1][x]=level[1][x]>128;
 for(int x=0;x<width;x++) trail[x]=trail[x]-(trail[x]>0);
 for(int x=0;x<width;x++) sum[x]=layerA[x]+layerB[x];
 for(int h=0;h<rows*width;h++) emit(h_[h]+m_[h]*256);
 for(int x=0;x<width;x++) emit(back[x]+trail[x]*256+sum[x]*65536);
 printf("DONE\n"); return 0;}
