// ESP-IDF port of examples/KeyboardCallback/KeyboardCallback.ino --
// host-driven callback pattern: an external variable the script reads
// (rather than a function argument), and a named script function the
// app re-enters on demand -- here, every time a real character arrives
// over the console UART -- reading its return value back.
//
// bindVariable() connects the script's `external int key_char;` to
// g_hostKeyChar below *before* parsing, so the assembler reserves a jump
// table slot for it and the loader patches that slot to point straight
// at g_hostKeyChar's real address (asm_execute.cpp's decodeBinaryHeader,
// case 1). Writing to g_hostKeyChar from here is then exactly the same
// as the script writing to its own external variable -- no call
// required to make the new value visible.
//
// Arduino's Serial.available()/Serial.read() don't exist under plain
// ESP-IDF -- the equivalent here is stdin (the same console UART) put
// into non-blocking mode via fcntl(O_NONBLOCK), then polled with
// fgetc()/EOF each pass through the loop, exactly the way ESP-IDF's own
// console examples do it.
//
// See KeyboardCallback.ino's header comment for why the loaded script is
// held in a `static ScriptExecutable` rather than a plain
// `executable` assigned into after the fact -- that pattern (populate
// once, keep using the same variable later) is unrelated to the
// Arduino-vs-ESP-IDF differences above and carries over unchanged.
#include "script_executable.h"
#include "binding.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <fcntl.h>
#include <unistd.h>

static char script[] = R"EOF(
external int key_char;

int keyboard()
{
   int c;
   c = key_char;
   if (c >= 97)
   {
      if (c <= 122)
      {
         c = c - 32;
      }
   }
   return c;
}

void main()
{
}
)EOF";

static int g_hostKeyChar = 0;

extern "C" void app_main(void)
{
   // Bind key_char before parsing so the assembler knows about it and
   // reserves a jump table slot -- see binding.h's findLink and
   // StructsAndHostBindings.ino for the same pattern.
   bindVariable((char *)"int", (char *)"key_char", NULL, (void *)&g_hostKeyChar);

   static ScriptExecutable exec = parseScript(script);
   if (!exec.isExeExists())
   {
      printf("script failed to compile/load\n");
      return;
   }

   // Non-blocking stdin, same console UART idf.py monitor already talks
   // over -- makes fgetc() below return EOF instead of blocking when no
   // character has arrived yet, mirroring Serial.available() == false.
   fcntl(fileno(stdin), F_SETFL, fcntl(fileno(stdin), F_GETFL, 0) | O_NONBLOCK);

   printf("type a letter over the console to see it uppercased by the script\n");

   while (true)
   {
      int c = fgetc(stdin);
      if (c != EOF)
      {
         // Write the new key into the bound variable, then re-enter the
         // script by name -- exactly the "print_char() sets key_char,
         // then re-enters the script" pattern.
         g_hostKeyChar = c;

         int32_t result = 0;
         if (exec.execute("keyboard", &result))
         {
            printf("%c -> %c\n", (char)g_hostKeyChar, (char)result);
         }
      }
      vTaskDelay(pdMS_TO_TICKS(20));
   }
}
