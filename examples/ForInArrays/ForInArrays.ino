// Arrays sized by their values, and for (x in array).
//
// The script shows:
//   1. int notes[] = {...};  -- no size between the brackets: the array
//      is as long as its list of values (here 6);
//   2. for (n in notes) runs its body once per element, n holding the
//      element: no index, no size to repeat;
//   3. the C++ spelling for (int n : notes) does the same, and a type
//      can be given in either form: for (float w in weights);
//   4. break and continue work as in any for loop;
//   5. loops over arrays nest;
//   6. without a type, an existing variable receives the elements and
//      keeps the last one after the loop;
//   7. an array of structures takes its values in braces, one {...} per
//      element; in for (v in voices), v is the element itself, so
//      changing v.gain changes the array;
//   8. strings: an array of char *, and a char * field in a structure,
//      given their strings in the braces too.
//
// Expected output:
//   notes: 60 62 64 65 67 69
//   sum 387, highest 69
//   doubled: 120 124 128 130 134 138
//   weighted average: 65.80
//   odd notes until 67: 65
//   pairs: 1-10 1-20 2-10 2-20 3-10 3-20
//   last level: 255
//   voices: 60@1.00 64@2.00 67@0.50
//   colors: red green blue (3 letters for the shortest)
//   apple costs 3
//   kiwi costs 25
//   melon costs 120
//   now: golden kiwi
//
// The array needs one dimension and a size the compiler knows: declared
// int a[8] or given by its values as here. Arrays are globals.
// s[i] reads one character of a char *; names[i][j] needs a variable in
// between (char *s = names[i]; s[j]).
// int a[2] = {1, 2, 3}; is an error (too many values).
#include "script_executable.h"

char script[] = R"EOF(
int notes[] = {60, 62, 64, 65, 67, 69};
float weights[] = {0.5, 1.0, 1.5, 2.0, 2.5, 2.5};
int rows[] = {1, 2, 3};
int cols[] = {10, 20};
uint8_t levels[] = {0, 64, 128, 255};

struct voice
{
   int note;
   float gain;
};
voice voices[] = {{60, 0.5}, {64, 1.0}, {67, 0.25}};

char *colors[] = {"red", "green", "blue"};

struct item
{
   char *name;
   int price;
};
item shop[] = {{"apple", 3}, {"kiwi", 25}, {"melon", 120}};

int length(char *s)
{
   int n = 0;
   while (s[n] != 0)
      n++;
   return n;
}

void main()
{
   // 2. one element at a time
   printf("notes:");
   for (n in notes)
      printf(" %d", n);
   printfln("");

   int sum = 0;
   int highest = 0;
   for (n in notes)
   {
      sum = sum + n;
      if (n > highest)
         highest = n;
   }
   printfln("sum %d, highest %d", sum, highest);

   // 3. the C++ form, and a type of your choice
   printf("doubled:");
   for (int n : notes)
      printf(" %d", n * 2);
   printfln("");

   float total = 0;
   float wsum = 0;
   int i = 0;
   for (float w in weights)
   {
      total = total + w * notes[i];
      wsum = wsum + w;
      i++;
   }
   printfln("weighted average: %.2f", total / wsum);

   // 4. continue skips an element, break leaves the loop
   printf("odd notes until 67:");
   for (n in notes)
   {
      if (n == 67)
         break;
      if (n % 2 == 0)
         continue;
      printf(" %d", n);
   }
   printfln("");

   // 5. nested
   printf("pairs:");
   for (r in rows)
      for (c in cols)
         printf(" %d-%d", r, c);
   printfln("");

   // 6. an existing variable gets the elements
   int level = 0;
   for (level in levels)
   {
   }
   printfln("last level: %d", level);

   // 7. structures: v is the element, not a copy
   for (v in voices)
      v.gain = v.gain * 2;
   printf("voices:");
   for (v in voices)
      printf(" %d@%.2f", v.note, v.gain);
   printfln("");

   // 8. strings
   printf("colors:");
   int shortest = 100;
   for (c in colors)
   {
      printf(" %s", c);
      if (length(c) < shortest)
         shortest = length(c);
   }
   printfln(" (%d letters for the shortest)", shortest);

   for (i in shop)
      printfln("%s costs %d", i.name, i.price);
   shop[1].name = "golden kiwi";
   printfln("now: %s", shop[1].name);
}
)EOF";

void setup()
{
   Serial.begin(115200);

   ScriptExecutable exec = parseScript(script);
   if (exec.isExeExists())
   {
      exec.execute("main");
   }
   else
   {
      printf("script failed to compile/load\n");
   }
}

void loop()
{
}
