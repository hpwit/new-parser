// ESP-IDF port of examples/LoadScriptBinary.ino -- loads the binary
// save_script_binary saved to /spiffs/script.bin and runs it. This
// program never includes that script's source, only its own
// implementations of the external names it declares ("key_char",
// "report"), registered via bindVariable()/bindFunction() exactly like
// any other script this project compiles itself (see
// structs_and_host_bindings). Everything else -- the actual machine
// code -- comes from flash.
//
// Flash save_script_binary first to write /spiffs/script.bin, then
// flash this project (it does not erase the filesystem, and shares the
// same partitions.csv/sdkconfig.defaults, so the SPIFFS partition lines
// up on the same device) to load and execute it.
//
// createExecutableFromBuffer() (script_executable.h) is the one-call
// version of deserializeBinary() + createExecutableFromBinary() +
// freeBinary(): it reconstructs a Binary from the saved bytes, then
// resolves "key_char"/"report" through binding.h's findLink() using
// *this* program's bindings -- not whatever (nothing, in
// save_script_binary's case) was in scope when the script was compiled.
// That's the whole point: the relocation is late-bound by name, so the
// compiling and executing programs don't need to agree on anything but
// those names.
#include "script_executable.h"
#include "binding.h"
#include "esp_spiffs.h"
#include <cstdio>
#include <cstdlib>

static int hostKeyChar = 0;

static void hostReport(int c)
{
   printf("script reported: %d\n", c);
}

extern "C" void app_main(void)
{
   bindVariable((char *)"int", (char *)"key_char", NULL, (void *)&hostKeyChar);
   bindFunction((char *)"void", (char *)"report", (char *)"int", (void *)hostReport);

   esp_vfs_spiffs_conf_t conf = {};
   conf.base_path = "/spiffs";
   conf.partition_label = NULL;
   conf.max_files = 5;
   conf.format_if_mount_failed = false;

   if (esp_vfs_spiffs_register(&conf) != ESP_OK)
   {
      printf("SPIFFS mount failed -- run save_script_binary first\n");
      return;
   }

   FILE *f = fopen("/spiffs/script.bin", "rb");
   if (f == NULL)
   {
      printf("/spiffs/script.bin not found -- run save_script_binary first\n");
      return;
   }
   fseek(f, 0, SEEK_END);
   long size = ftell(f);
   fseek(f, 0, SEEK_SET);

   uint8_t *buf = (uint8_t *)malloc(size);
   size_t got = fread(buf, 1, size, f);
   fclose(f);
   if (got != (size_t)size)
   {
      printf("short read: %u of %ld bytes\n", (unsigned)got, size);
      free(buf);
      return;
   }

   ScriptExecutable exec = createExecutableFromBuffer(buf, size);
   free(buf);
   if (!exec.isExeExists())
   {
      // createExecutableFromBuffer() already printed the specific
      // deserialize/loader error.
      return;
   }

   for (int n = 0; n <= 5; n++)
   {
      hostKeyChar = n;
      int32_t result = 0;
      // executeOnly(), not execute() -- re-entering "keyboard" repeatedly
      // here, same as keyboard_callback's own once-per-keypress pattern
      // (see ScriptExecutable::executeOnly()'s own doc comment).
      if (!exec.executeOnly("keyboard", &result))
      {
         printf("executeOnly(\"keyboard\") failed\n");
         break;
      }
      printf("key_char=%d -> keyboard() returned %d\n", n, (int)result);
   }
}
