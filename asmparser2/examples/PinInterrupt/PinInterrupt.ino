// A GPIO interrupt that calls back into the running script.
//
// The script registers one of its own functions as the handler for a
// pin: pinInterrupt(_handle_, "increase", 23). Every falling edge on
// GPIO 23 (a button to GND -- the internal pull-up is enabled) then runs
// increase() while main() keeps looping, and main() prints the counter
// each time it changes.
//
// _handle_ is declared in every script and holds the address of the
// executable running it (filled in each time the host enters the
// script). That is all the host side needs to call back in: it is the
// first argument of callFunction() below. v1 called this variable
// _execaddr_.
//
// Ported from asmparser's (v1) pininterrupt example. Two differences:
//   - the handler calls callFunction() with the handle; v1 called
//     Executable::executeOnly(). Neither re-runs the script's global
//     initializers -- ScriptExecutable::execute() would, resetting
//     `number` to 0 on every press, so don't use it here.
//   - the function name is copied: the string the script passes lives in
//     the script's data, which is fine while the script is loaded, but
//     the copy makes the handler independent of it.
#include "script_executable.h"
#include "binding.h"
#include "asm_execute.h"
#include "driver/gpio.h"

typedef struct
{
   executable *exe;
   char name[32];
   uint32_t last;
} call_isr;
static call_isr isr_struct;

static void IRAM_ATTR gpio_isr_handler(void *args)
{
   call_isr *ll = (call_isr *)args;
   uint32_t currentTime = millis();

   // 100 ms debounce, as in v1.
   if ((currentTime - ll->last) > 100)
   {
      ll->last = currentTime;
      callFunction(ll->exe, ll->name, (const int32_t *)NULL, 0, NULL);
   }
}

// external void pinInterrupt(uint32_t exec, char *name, int pin);
void setup_gpio_interrupt(uint32_t handle, char *name, int pin_num)
{
   printf("Setting up the interrupt\n");
   gpio_config_t io_conf = {
       .pin_bit_mask = (1ULL << pin_num),
       .mode = GPIO_MODE_INPUT,
       .pull_up_en = GPIO_PULLUP_ENABLE,
       .pull_down_en = GPIO_PULLDOWN_DISABLE,
       .intr_type = GPIO_INTR_NEGEDGE,
   };
   gpio_config(&io_conf);
   gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1); // ESP_ERR_INVALID_STATE if already installed: fine

   isr_struct.exe = (executable *)(uintptr_t)handle;
   strncpy(isr_struct.name, name, sizeof(isr_struct.name) - 1);
   isr_struct.name[sizeof(isr_struct.name) - 1] = 0;
   isr_struct.last = millis();
   gpio_isr_handler_add((gpio_num_t)pin_num, gpio_isr_handler, &isr_struct);
}

char script[] = R"EOF(
external void pinInterrupt(uint32_t exec, char *name, int pin);

int number = 0;

void increase()
{
   number++;
}

void main()
{
   int shown = -1;
   pinInterrupt(_handle_, "increase", 23);
   while (true)
   {
      if (number != shown)
      {
         shown = number;
         printfln("number:%d", number);
      }
   }
}
)EOF";

void setup()
{
   Serial.begin(115200);
   bindFunction((char *)"void", (char *)"pinInterrupt", (char *)"uint32_t,char*,int", (void *)setup_gpio_interrupt);

   // static: the interrupt keeps a pointer to this script for as long as
   // it can fire.
   static ScriptExecutable exec = parseScript(script);
   if (exec.isExeExists())
   {
      exec.execute("main"); // never returns: main() loops forever
   }
   else
   {
      printf("script failed to compile/load\n");
   }
}

void loop()
{
}
