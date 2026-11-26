// C reference of for_in_structs.sc
#include <stdio.h>
#include <stdint.h>
void emit(int v){printf("EMIT %d\n",v);}
struct st { int h; int g; }; struct mix { uint8_t a; float f; uint16_t w; int n; };
struct st arr[] = {{1, 2}, {3, 4}, {5, -6}};
struct mix mx[] = {{7, 1.5f, 300, -9}, {255, -0.25f, 65535, 100000}, {0}};
struct st filled[3];
int r[]={1,40},g[]={2,50},b[]={3,60};
int p = 77;
int main(){
 for(int i=0;i<3;i++) emit(arr[i].g);
 for(int i=0;i<3;i++) emit(arr[i].h*100+arr[i].g);
 emit(p);
 for(int i=0;i<3;i++){ emit(mx[i].a); emit((int)(mx[i].f*100)); emit(mx[i].w); emit(mx[i].n);}
 for(int i=0;i<3;i++){filled[i].h=i+1; filled[i].g=(i+1)*10;}
 for(int i=0;i<3;i++) filled[i].g += filled[i].h;
 for(int i=0;i<3;i++) emit(filled[i].g);
 for(int i=0;i<3;i++) for(int j=0;j<3;j++) emit(arr[i].h*10+arr[j].h);
 printf("DONE\n"); return 0;}
