// ESP-IDF port of examples/SaveScriptBinary/SaveScriptBinary.ino --
// compiles a script and saves the result to flash (SPIFFS) as a
// self-contained binary blob, for a *completely separate* program
// (load_script_binary, this same exampleESPIDF/ tree's sibling project)
// to load and run later -- without ever seeing this script's source.
//
// This program doesn't bind key_char/report to anything real, and never
// executes the script itself -- compiling only needs the `external ...;`
// declarations in the script text (see structs_and_host_bindings for the
// case where a program *does* also need to bind+run its own script).
// parseScriptToBinary() (script_executable.h) is the one-call version of
// parse -> createBinary -> serializeBinary: it flattens the compiled
// result (instruction bytes + the relocation header, which still holds
// "key_char"/"report" as *names*, not addresses) into one buffer that's
// meaningful on its own, in any process that later binds those same
// names to something real.
//
// Flash this project once to write /spiffs/script.bin, then flash
// load_script_binary (which does NOT erase the filesystem -- both
// projects share the same partitions.csv/sdkconfig.defaults so the
// SPIFFS partition lines up on the same device) to load and run it.
//
// SPIFFS here replaces the .ino original's Arduino LittleFS library --
// ESP-IDF's built-in esp_spiffs.h (esp_vfs_spiffs_register()) mounted at
// "/spiffs", after which plain POSIX fopen()/fwrite()/fclose() work
// exactly the same way LittleFS's File API did.
#include "script_executable.h"
#include "esp_spiffs.h"
#include <cstdio>
#include <cstdlib>

static char script[] = R"EOF(
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

extern "C" void app_main(void)
{
   uint32_t size = 0;
   uint8_t *serialized = parseScriptToBinary(script, &size);
   if (serialized == NULL)
   {
      // parseScriptToBinary() already printed the specific parse/
      // assembler error.
      return;
   }

   esp_vfs_spiffs_conf_t conf = {};
   conf.base_path = "/spiffs";
   conf.partition_label = NULL;
   conf.max_files = 5;
   conf.format_if_mount_failed = true;

   if (esp_vfs_spiffs_register(&conf) != ESP_OK)
   {
      printf("SPIFFS mount failed\n");
      free(serialized);
      return;
   }

   FILE *f = fopen("/spiffs/script.bin", "wb");
   if (f == NULL)
   {
      printf("failed to open /spiffs/script.bin for writing\n");
      free(serialized);
      return;
   }
   size_t written = fwrite(serialized, 1, size, f);
   fclose(f);
   free(serialized);

   if (written != size)
   {
      printf("short write: %u of %u bytes\n", (unsigned)written, (unsigned)size);
      return;
   }

   printf("saved compiled script to /spiffs/script.bin (%u bytes)\n", (unsigned)size);
   printf("now flash load_script_binary (without erasing flash) to run it\n");
}
