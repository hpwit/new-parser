// Binding a struct the sketch didn't define itself, one that comes from
// a library's header: BIND_STRUCT(type, field, field, ...) makes the
// script's definition from the C type and checks that the script lays it
// out exactly as C does before binding anything.
//
// Two library structs here: key_return, as the USB keyboard library
// defines it (keyboard_library.h stands for that header), and the C
// library's own struct tm, the date and time.
#include <time.h>
#include "script_executable.h"
#include "binding.h"
#include "arguments.h"
#include "keyboard_library.h"

key_return lastKey = {'a', 4};
struct tm today;

char script[] = R"EOF(
// neither key_return nor tm is defined here: BIND_STRUCT did it

void keyboard(key_return *k)
{
   printfln("script: key '%c' code %d", k->key_char, k->key_code);
}

void addDays(tm *t, int days)
{
   t->tm_mday = t->tm_mday + days;   // mktime() normalizes it afterwards
}

void main()
{
   printfln("script: today is %d/%d/%d", today.tm_mday, today.tm_mon + 1, today.tm_year + 1900);
   printfln("script: last key '%c' code %d", lastKey.key_char, lastKey.key_code);
   addDays(&today, 30);
}
)EOF";

void setup()
{
   Serial.begin(115200);
   delay(1000);

   // every field, in order; false (with the reason printed) if the
   // script couldn't match the C layout
   if (!BIND_STRUCT(key_return, key_char, key_code) ||
       !BIND_STRUCT(tm, tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst))
      return;
   bindVariable((char *)"key_return", (char *)"lastKey", NULL, (void *)&lastKey);
   bindVariable((char *)"tm", (char *)"today", NULL, (void *)&today);

   today.tm_year = 2026 - 1900;
   today.tm_mon = 10;    // November
   today.tm_mday = 6;
   today.tm_hour = 12;
   mktime(&today);

   static ScriptExecutable exec = parseScript(script);
   if (!exec.isExeExists())
   {
      showCompileError();
      return;
   }
   exec.execute("main");
   mktime(&today);
   printf("sketch: 30 days later it is %d/%d/%d\n", today.tm_mday, today.tm_mon + 1, today.tm_year + 1900);

   // the sketch hands the script a pointer to its own key
   key_return k = {'z', 29};
   Arguments args;
   args.add((int)(uintptr_t)&k);
   exec.execute("keyboard", &args);
}

void loop()
{
   delay(1000);
}
