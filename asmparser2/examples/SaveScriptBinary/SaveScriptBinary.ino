// Compiles a script and saves the result to flash (LittleFS) as a
// self-contained binary blob, for a *completely separate* sketch
// (LoadScriptBinary.ino, in this same examples/ directory) to load and
// run later -- without ever seeing this script's source.
//
// This sketch doesn't bind key_char/report to anything real, and never
// executes the script itself -- compiling only needs the `external ...;`
// declarations in the script text (see StructsAndHostBindings.ino for
// the case where a sketch *does* also need to bind+run its own script).
// parseScriptToBinary() (script_executable.h) is the one-call version of
// parse -> createBinary -> serializeBinary: it flattens the compiled
// result (instruction bytes + the relocation header, which still holds
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
// emulation -- parseScriptToBinary() shares that exact same compile
// pipeline under the hood (see script_executable.cpp), it's just a
// thinner wrapper around it.
#include <LittleFS.h>
#include "script_executable.h"

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

   uint32_t size = 0;
   uint8_t *serialized = parseScriptToBinary(script, &size);
   if (serialized == NULL)
   {
      // parseScriptToBinary() already printed the specific parse/
      // assembler error.
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
