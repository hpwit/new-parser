// Host-driven callback pattern: an external variable the script reads
// (rather than a function argument), and a named script function the
// .ino re-enters on demand -- e.g. every time a real key arrives over
// Serial -- reading its return value back into the .ino.
//
// This is the same round trip upstream's keyboard-handling code used
// (setup_keyboard()/print_char() writing a key into a variable, then
// re-entering the script via a stored pointer -- see execute.h's
// Executable class), but simplified: this project doesn't need a
// script-side self-pointer at all. The .ino already owns the loaded
// `executable` (populated once in setup()), so any host-side event
// handler can call back into the script directly with callFunction() --
// no script-side registration call needed.
//
// bindVariable() connects the script's `external int key_char;` to
// g_keyChar below *before* parsing, so the assembler reserves a jump
// table slot for it and the loader patches that slot to point straight
// at g_keyChar's real address (asm_execute.cpp's decodeBinaryHeader,
// case 1). Writing to g_keyChar from the .ino is then exactly the same
// as the script writing to its own external variable -- no call
// required to make the new value visible.
//
// Verified under QEMU (Espressif's esp32 machine model) against real
// generated code: this exact key_char-read-and-uppercase script, called
// by name with the same jump table patching decodeBinaryHeader performs
// at runtime, correctly turns 'a'/'z' into 'A'/'Z' and leaves other
// characters unchanged -- see test/qemu/gen_keyboard.cpp. This was
// worth checking on real hardware emulation specifically because no
// other test/qemu case exercised reading an *external variable* (only
// external function calls and function arguments were covered before).
#include "parser.h"
#include "binding.h"
#include "asm_parser.h"
#include "asm_execute.h"

char script[] = R"EOF(
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

int hostKeyChar = 0;
executable g_exe;

void setup()
{
   Serial.begin(115200);

   // Bind key_char before parsing so the assembler knows about it and
   // reserves a jump table slot -- see binding.h's findLink and
   // StructsAndHostBindings.ino for the same pattern.
   bindVariable((char *)"int", (char *)"key_char", NULL, (void *)&hostKeyChar);

   Script s;
   s.addContent(script);
   s.init();

   Parser p;
   p.clean();
   p.parse(&s, &__allTokens);

   if (Error.error)
   {
      display_error(&Error);
      return;
   }

   Binary bin = createBinary(&footer, &header, &content, false);
   if (bin.error.error)
   {
      printf("assembler error: %s\n", bin.error.error_message ? bin.error.error_message : "?");
      return;
   }

   g_exe = createExecutableFromBinary(&bin);
   if (g_exe.error.error)
   {
      printf("loader error: %s\n", g_exe.error.error_message ? g_exe.error.error_message : "?");
      return;
   }
}

void loop()
{
   if (Serial.available())
   {
      // Write the new key into the bound variable, then re-enter the
      // script by name -- exactly the "print_char() sets key_char, then
      // re-enters the script" pattern, just without a script-side
      // self-pointer to get there.
      hostKeyChar = Serial.read();

      int32_t result = 0;
      if (callFunction(&g_exe, "keyboard", NULL, 0, &result))
      {
         Serial.print((char)hostKeyChar);
         Serial.print(" -> ");
         Serial.println((char)result);
      }
   }
}
