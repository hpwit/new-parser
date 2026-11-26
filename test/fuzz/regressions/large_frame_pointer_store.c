// C reference of large_frame_pointer_store.sc
#include <stdio.h>
#include <string.h>
void emit(int v){printf("EMIT %d\n",v);}
int main(){ char *colors[]={"red","green","blue"}; int sh=100; for(int i=0;i<3;i++){ emit(colors[i][0]); if(strlen(colors[i])<sh) sh=strlen(colors[i]);} emit(sh+29); for(int i=0;i<3;i++) emit(strlen(colors[i])+28); printf("DONE\n"); return 0;}
