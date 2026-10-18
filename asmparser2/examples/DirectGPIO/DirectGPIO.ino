// Driving GPIOs from a script by writing the chip's GPIO registers
// directly -- no host function call per pin change.
//
// The LED blinks; it blinks fast while the BOOT button (GPIO 0, on the
// dev kits of both chips) is held down.
//
// How: each register is bound as an external variable whose address is
// the register itself (bindVariable() below). The loader patches the
// script's slot for the variable with that address, so in the script
//
//    led_on = led_bit;
//
// compiles to one load of led_bit and one store (s32i) into
// GPIO_OUT_W1TS_REG, and reading button_in is one load (l32i) from
// GPIO_IN_REG.
//
// Same sketch for ESP32 and ESP32-S3: soc/gpio_reg.h gives the addresses
// of the chip being built for (the register offsets are the same on both,
// the base differs: 0x3FF44000 on ESP32, 0x60004000 on ESP32-S3), and the
// host picks the bank -- pins 0-31 are in GPIO_OUT/GPIO_IN, pins 32 and
// up in GPIO_OUT1/GPIO_IN1 -- and hands the script the pin's bit. The
// script itself has no address or pin number in it.
//
// Pins: the LED is GPIO 2 on ESP32 (the on-board LED of most dev kits).
// On ESP32-S3 the on-board LED is usually a WS2812, which a GPIO level
// cannot light, so connect an LED (with a resistor) to GPIO 5, or change
// LED_PIN.
//
// Note the script language spells bitwise AND `and` (`&` alone is
// address-of); `|` is bitwise OR.
#include "script_executable.h"
#include "binding.h"
#include "soc/gpio_reg.h"

#if CONFIG_IDF_TARGET_ESP32S3
#define LED_PIN 5
#else
#define LED_PIN 2
#endif
#define BUTTON_PIN 0

static uint32_t ledBit;
static uint32_t buttonBit;

char script[] = R"EOF(
external uint32_t led_on;     // write a bit: that pin goes high
external uint32_t led_off;    // write a bit: that pin goes low
external uint32_t button_in;  // input levels, one bit per pin
external uint32_t led_bit;
external uint32_t button_bit;
external void delay(uint32_t ms);

void main()
{
   while (true)
   {
      uint32_t wait = 500;
      if ((button_in and button_bit) == 0)   // pressed: the button pulls the pin to GND
         wait = 100;
      led_on = led_bit;
      delay(wait);
      led_off = led_bit;
      delay(wait);
   }
}
)EOF";

void setup()
{
   Serial.begin(115200);

   // Direction, pull-up and the pin's GPIO function are set up once, the
   // usual way; from then on the script only touches the data registers.
   pinMode(LED_PIN, OUTPUT);
   pinMode(BUTTON_PIN, INPUT_PULLUP);

   ledBit = 1UL << (LED_PIN % 32);
   buttonBit = 1UL << (BUTTON_PIN % 32);
   bindVariable((char *)"uint32_t", (char *)"led_on", NULL,
                (void *)(LED_PIN < 32 ? GPIO_OUT_W1TS_REG : GPIO_OUT1_W1TS_REG));
   bindVariable((char *)"uint32_t", (char *)"led_off", NULL,
                (void *)(LED_PIN < 32 ? GPIO_OUT_W1TC_REG : GPIO_OUT1_W1TC_REG));
   bindVariable((char *)"uint32_t", (char *)"button_in", NULL,
                (void *)(BUTTON_PIN < 32 ? GPIO_IN_REG : GPIO_IN1_REG));
   bindVariable((char *)"uint32_t", (char *)"led_bit", NULL, (void *)&ledBit);
   bindVariable((char *)"uint32_t", (char *)"button_bit", NULL, (void *)&buttonBit);
   bindFunction((char *)"void", (char *)"delay", (char *)"uint32_t", (void *)delay);

   printf("LED on GPIO %d, button on GPIO %d\n", LED_PIN, BUTTON_PIN);
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
