// C reference of string_struct_fields.sc
#include <stdio.h>
#include <string.h>
#include <stdint.h>
void emit(int v){printf("EMIT %d\n",v);}
struct item { char *name; int price; uint8_t stock; };
struct item shop[] = {{"apple", 3, 10}, {"kiwi", 25, 0}, {"melon", 120, 2}};
struct item spare[2];
struct item best = {"plum", 4, 9};
int main(){ spare[1].name="fig"; spare[1].price=7;
 for(int j=0;j<3;j++){ char *n=shop[j].name; emit(strlen(n)); emit(n[0]); emit(shop[j].price+shop[j].stock);}
 char *f=spare[1].name; emit(f[1]); emit(strlen(shop[2].name));
 for(int j=0;j<3;j++) if(shop[j].stock==0) shop[j].name="sold out";
 for(int j=0;j<3;j++) emit(strlen(shop[j].name)*100);
 emit(strlen(best.name)); emit(best.price*10+best.stock);
 char *k=shop[1].name; emit(strlen(k));
 printf("DONE\n"); return 0;}
