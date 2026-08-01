// Loads the binary SaveScriptBinary.ino saved to /script.bin and runs
// it -- this sketch never includes that script's source, only its own
// implementations of the external names it declares ("key_char",
// "report"), registered via bindVariable()/bindFunction() exactly like
// any other script this project compiles itself (see
// StructsAndHostBindings.ino). Everything else -- the actual machine
// code -- comes from flash.
//
// Flash SaveScriptBinary.ino first to write /script.bin, then flash
// this sketch (it does not erase the filesystem, so the earlier write
// survives the re-flash) to load and execute it.
//
// asm_serialize.h's deserializeBinary() reconstructs a Binary from the
// saved bytes; createExecutableFromBinary() then resolves "key_char"/
// "report" through binding.h's findLink() using *this* sketch's
// bindings -- not whatever (nothing, in SaveScriptBinary.ino's case) was
// in scope when the script was compiled. That's the whole point: the
// relocation is late-bound by name, so the compiling and executing
// sketches don't need to agree on anything but those names.
//
// Verified on host (deserializes and reloads correctly under freshly
// bound names) via test/host/test_parser.cpp's save/load test. Real
// execution proof -- that a second, independent program's own bound
// variable and callback are genuinely what the loaded code reads and
// calls -- is QEMU-verified: test/qemu/gen_saveload.cpp + runner_saveload.c.
#include <LittleFS.h>
#include "binding.h"
#include "asm_execute.h"
#include "asm_serialize.h"

int hostKeyChar = 0;

void hostReport(int c)
{
   printf("script reported: %d\n", c);
}

void setup()
{
   Serial.begin(115200);

   bindVariable((char *)"int", (char *)"key_char", NULL, (void *)&hostKeyChar);
   bindFunction((char *)"void", (char *)"report", (char *)"int", (void *)&hostReport);

   if (!LittleFS.begin(false))
   {
      printf("LittleFS mount failed -- run SaveScriptBinary.ino first\n");
      return;
   }

   File f = LittleFS.open("/script.bin", "r");
   if (!f)
   {
      printf("/script.bin not found -- run SaveScriptBinary.ino first\n");
      return;
   }
   size_t size = f.size();
   uint8_t *buf = (uint8_t *)malloc(size);
   size_t got = f.read(buf, size);
   f.close();
   if (got != size)
   {
      printf("short read: %u of %u bytes\n", (unsigned)got, (unsigned)size);
      free(buf);
      return;
   }

   Binary bin = deserializeBinary(buf, size);
   free(buf);
   if (bin.error.error)
   {
      printf("deserializeBinary error: %s\n", bin.error.error_message ? bin.error.error_message : "?");
      return;
   }

   executable exe = createExecutableFromBinary(&bin);
   freeBinary(&bin);
   if (exe.error.error)
   {
      printf("loader error: %s\n", exe.error.error_message ? exe.error.error_message : "?");
      return;
   }

   for (int n = 0; n <= 5; n++)
   {
      hostKeyChar = n;
      int32_t result = 0;
      if (!callFunction(&exe, "keyboard", NULL, 0, &result))
      {
         printf("callFunction(\"keyboard\") failed\n");
         break;
      }
      printf("key_char=%d -> keyboard() returned %d\n", n, result);
   }

   freeExecutable(&exe);
}

void loop()
{
}
