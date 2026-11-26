// C reference of hex_initializers.sc
#include <stdio.h>
#include <stdint.h>
void emit(int v){printf("EMIT %d\n",v);}
uint8_t font[16] = {0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00, 0xE0, 0x60, 0x6C, 0x76, 0x66, 0x66, 0xE6, 0xff};
int words[] = {0x10, -0x20, 0x7FFF, 0xABCDEF, 12};
struct pt { int x; uint8_t c; };
struct pt pts[] = {{0x100, 0xA5}, {-0x3, 0x0f}};
char *word = "hi";
int glyphrow(char *t, int k, int row){int c = t[k]; return font[(c - 104) * 8 + row];}
int main(){
 for(int i=0;i<16;i++) emit(font[i]);
 for(int i=0;i<5;i++) emit(words[i]);
 for(int i=0;i<2;i++){emit(pts[i].x); emit(pts[i].c);}
 for(int row=0;row<8;row++){int bits=glyphrow(word,0,row); int n=0; for(int col=0;col<8;col++) if((bits&(128>>col))!=0) n++; emit(n);}
 for(int i=14;i<19;i++){switch(i){case 0x10: emit(100); break; case 0x0F: emit(200); break; case 18: emit(300); break; default: emit(1);}}
 printf("DONE\n"); return 0;}
