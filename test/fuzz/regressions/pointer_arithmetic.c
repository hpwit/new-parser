// C reference of pointer_arithmetic.sc
#include <stdio.h>
#include <stdint.h>
void emit(int v){printf("EMIT %d\n",v);}
uint16_t buf[8]; uint8_t bytes[8]; int ints[6]; float fl[4];
struct pt { int x; uint8_t c; }; struct pt pts[4];
uint16_t *gp; int *gi; struct pt *gs; char *word = "pointer";
int sum3(uint16_t *p){return p[0]+p[1]+p[2];}
uint16_t *advance(uint16_t *p, int n){return p+n;}
int sumBytes(uint8_t *p, int n){int s=0; for(int i=0;i<n;i++) s=s+p[i]; return s;}
int main(){
 for(int i=0;i<8;i++){buf[i]=100+i; bytes[i]=10+i;}
 for(int i=0;i<6;i++) ints[i]=1000*i;
 for(int i=0;i<4;i++){fl[i]=i*1.5; pts[i].x=50+i; pts[i].c=200+i;}
 gp=buf; gi=ints; gs=pts;
 uint16_t *d=gp; emit(d[2]); d[3]=77; emit(buf[3]);
 uint16_t *p=gp+2; emit(p[0]); p=p+1; emit(p[0]); p=p-2; emit(p[0]); p+=3; emit(p[0]); p-=1; emit(p[0]); p++; emit(p[0]);
 emit(p-gp); emit(gp+5-p);
 p=1+gp; emit(p[0]); p=gp+1+1; emit(p[0]); int k=4; p=gp+k; emit(p[0]); p=buf+6; emit(p[0]);
 gp=gp+1; emit(gp[0]); gp=buf;
 int *q=gi+3; emit(q[0]); emit(q-gi);
 float *f=fl+2; emit(f[0]*10);
 uint8_t *b=bytes+5; emit(b[0]); emit(sumBytes(bytes+2,3)); emit(sumBytes(b,2));
 struct pt *s=gs+2; emit(s[0].x); emit(s[0].c); s=s+1; emit(s[0].x); emit(s-gs); emit(gs[2].x);
 emit(sum3(gp)); emit(sum3(gp+4)); uint16_t *a=advance(gp,5); emit(a[0]); emit(advance(buf,2)-buf);
 char *w=word+3; emit(w[0]); emit(w[1]);
 printf("DONE\n"); return 0;}
