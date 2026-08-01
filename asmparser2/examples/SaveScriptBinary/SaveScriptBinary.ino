// Compiles a script and saves the result to flash (LittleFS) as a
// self-contained binary blob, for a *completely separate* sketch
// (LoadScriptBinary.ino, in this same examples/ directory) to load and
// run later -- without ever seeing this script's source.
//
// This sketch doesn't bind key_char/report to anything real, and never
// executes the script itself -- compiling only needs the `external ...;`
// declarations in the script text (see StructsAndHostBindings.ino for
// the case where a sketch *does* also need to bind+run its own script).
// asm_serialize.h's serializeBinary() flattens createBinary()'s output
// (instruction bytes + the relocation header, which still holds
// "key_char"/"report" as *names*, not addresses) into one buffer that's
// meaningful on its own, in any process that later binds those same
// names to something real.
//
// Flash this sketch once to write /script.bin, then flash
// LoadScriptBinary.ino (which does NOT erase the filesystem) to load and
// run it.
//
// Verified on host (parses, assembles, and serializes correctly, with a
// byte-exact round trip through a real file) via test/host/test_parser.cpp's
// "save a compiled script to a file and load+relocate it under a
// different binding" test. Real execution of a save/load round trip is
// QEMU-verified: test/qemu/gen_saveload.cpp + runner_saveload.c compile
// and serialize the same shape of script, then a second, independent
// program (standing in for LoadScriptBinary.ino) deserializes it, binds
// its own key_char/report, and runs it correctly on a real Xtensa CPU
// emulation.
#include <LittleFS.h>
#include "parser.h"
#include "asm_parser.h"
#include "asm_serialize.h"

char script[] = R"EOF(
external int key_char;
external void report(int c);

int keyboard()
{
   int c;
   c = key_char + 1;
   report(c);
   return c;
}

void main()
{
}
)EOF";

void setup()
{
   Serial.begin(115200);

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

   uint32_t size = 0;
   uint8_t *serialized = serializeBinary(&bin, &size);
   if (serialized == NULL)
   {
      printf("serializeBinary failed\n");
      return;
   }

   if (!LittleFS.begin(true))
   {
      printf("LittleFS mount failed\n");
      free(serialized);
      return;
   }

   File f = LittleFS.open("/script.bin", "w");
   if (!f)
   {
      printf("failed to open /script.bin for writing\n");
      free(serialized);
      return;
   }
   size_t written = f.write(serialized, size);
   f.close();
   free(serialized);

   if (written != size)
   {
      printf("short write: %u of %u bytes\n", (unsigned)written, size);
      return;
   }

   printf("saved compiled script to /script.bin (%u bytes)\n", size);
   printf("now flash LoadScriptBinary.ino (without erasing flash) to run it\n");
}

void loop()
{
}
