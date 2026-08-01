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
// script (populated once in setup(), see ScriptExecutable below), so
// any host-side event handler can call back into it directly with
// execute() -- no script-side registration call needed.
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
//
// Uses parseScript()/ScriptExecutable (script_executable.h) rather than
// spelling out parse -> createBinary -> createExecutableFromBinary by
// hand. This rewrite also fixed a real, independent bug this exact
// pattern (a script's executable populated once in setup(), used later
// in loop()) had: the previous version declared `executable g_exe;`
// at file scope and *assigned* into it from setup() (`g_exe =
// createExecutableFromBinary(&bin);`). executable (asm_types.h) holds
// vect<globalcall>/vect<jsonVariable> members; vect<T> (vect.h) has a
// destructor but no matching copy/move constructor or assignment
// operator, so that assignment invoked the compiler-generated *shallow*
// member-wise copy -- g_exe.functions and the right-hand side's
// temporary executable ended up sharing the same vect<T> backing-array
// pointer, and the temporary's destructor freed it at the end of that
// statement, leaving g_exe.functions permanently dangling for the rest
// of the program. Every loop() call into callFunction() after that was a
// heap-use-after-free (confirmed via AddressSanitizer, reproducing this
// exact pattern host-side). ScriptExecutable sidesteps this by being
// non-copyable/non-movable and only ever constructed via direct-
// initialization from a prvalue (guaranteed-elided since C++17, so the
// unsafe copy/assignment is never actually invoked -- see
// script_executable.h's class comment for the full explanation); a
// `static` local inside setup(), captured by a pointer used in loop(),
// gets the same "populate once, use later" shape this example needs
// without ever assigning into an already-existing ScriptExecutable.
#include "script_executable.h"
#include "binding.h"

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
ScriptExecutable *g_exec = NULL;

void setup()
{
   Serial.begin(115200);

   // Bind key_char before parsing so the assembler knows about it and
   // reserves a jump table slot -- see binding.h's findLink and
   // StructsAndHostBindings.ino for the same pattern.
   bindVariable((char *)"int", (char *)"key_char", NULL, (void *)&hostKeyChar);

   // `static` gives this ScriptExecutable the same "construct once in
   // setup(), still alive in loop()" lifetime g_exe used to have, without
   // ever assigning into an already-existing one -- see the header
   // comment for why that matters. Direct-initialization from
   // parseScript()'s return value here is what makes this safe (elided,
   // per script_executable.h's class comment) -- don't replace this with
   // a two-step "declare then assign".
   static ScriptExecutable holder = parseScript(script);
   g_exec = &holder;

   if (!g_exec->isExeExists())
   {
      printf("script failed to compile/load\n");
   }
}

void loop()
{
   if (g_exec != NULL && g_exec->isExeExists() && Serial.available())
   {
      // Write the new key into the bound variable, then re-enter the
      // script by name -- exactly the "print_char() sets key_char, then
      // re-enters the script" pattern, just without a script-side
      // self-pointer to get there.
      hostKeyChar = Serial.read();

      int32_t result = 0;
      if (g_exec->execute("keyboard", &result))
      {
         Serial.print((char)hostKeyChar);
         Serial.print(" -> ");
         Serial.println((char)result);
      }
   }
}
