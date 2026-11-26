// char * struct fields: strings in a struct array's initializer and in a single struct's, assigned, passed as char *, through for (i in shop)
external void emit(int v);
struct item
{
   char *name;
   int price;
   uint8_t stock;
};
item shop[] = {{"apple", 3, 10}, {"kiwi", 25, 0}, {"melon", 120, 2}};
item spare[2];
item best = {"plum", 4, 9};
int len(char *s)
{
   int n = 0;
   while (s[n] != 0)
      n++;
   return n;
}
void main()
{
   spare[1].name = "fig";
   spare[1].price = 7;
   for (i in shop)
   {
      char *n = i.name;
      emit(len(n));
      emit(n[0]);
      emit(i.price + i.stock);
   }
   char *f = spare[1].name;
   emit(f[1]);
   emit(len(shop[2].name));
   for (i in shop)
      if (i.stock == 0)
         i.name = "sold out";
   for (i in shop)
      emit(len(i.name) * 100);
   emit(len(best.name));
   emit(best.price * 10 + best.stock);
   char *k = shop[1].name;
   emit(len(k));
}
