// A USB keyboard driving a script, on an ESP32-S3.
//
// Plug a USB keyboard into the S3's USB host (OTG) port: every key goes to
// the script's keyboard() function, which echoes it, keeps the line being
// typed (Backspace erases) and prints it on Enter. Output on the serial
// monitor at 115200. The README's "A USB keyboard driving a script"
// section walks through it.
//
// Needs, besides this library:
//   - an ESP32-S3 (the plain ESP32 has no USB host);
//   - the ESP32_USB_Host_HID library, in the version whose keyboard.h
//     defines struct key_return and calls the hid_keyboard_print_char()
//     hook on every key (the one keybard5inch uses).
#include "keyboard.h"
#include "script_executable.h"
#include "binding.h"

// The script hands itself over with setup_keyboard(_handle_); every key
// then runs its keyboard() function, which reads the key from `key`.
ScriptExecutable *keyboardScript = NULL;
key_return lastKey;

void setup_keyboard(uint32_t handle)
{
   keyboardScript = (ScriptExecutable *)handle;
}

// called by the keyboard library on every key
void print_char(key_return k)
{
   lastKey = k;
   if (keyboardScript != NULL)
      keyboardScript->executeOnly("keyboard");
}

char script[] = R"EOF(
// key_return (key_char, key_code) comes from the keyboard library,
// bound by the sketch: no definition here
char line[100];
int len = 0;

void keyboard()
{
   if (key.key_code == 40)            // Enter: show the line typed
   {
      printf("line (%d chars): ", len);
      for (int i = 0; i < len; i++)
         printf("%c", line[i]);
      printfln("");
      len = 0;
   }
   else if (key.key_code == 42)       // Backspace
   {
      if (len > 0)
         len--;
      printfln("backspace, %d chars", len);
   }
   else if (key.key_char != 0)
   {
      if (len < 99)
      {
         line[len] = key.key_char;
         len++;
      }
      printfln("key '%c' code %d", key.key_char, key.key_code);
   }
   else
      printfln("key code %d (no character)", key.key_code);
}

void main()
{
   setup_keyboard(_handle_);
   printfln("ready: type on the USB keyboard, Enter shows the line");
}
)EOF";

void setup()
{
   Serial.begin(115200);
   delay(1000);

   if (!BIND_STRUCT(key_return, key_char, key_code))
      return;
   bindVariable((char *)"key_return", (char *)"key", NULL, (void *)&lastKey);
   bindFunction((char *)"void", (char *)"setup_keyboard", (char *)"uint32_t", (void *)setup_keyboard);

   static ScriptExecutable exec = parseScript(script);
   if (!exec.isExeExists())
   {
      showCompileError();
      return;
   }
   exec.execute("main");

   // start the USB host and the keyboard driver
   hid_keyboard_print_char = print_char;
   app_main_keyboard();
}

void loop()
{
   delay(100);
}
