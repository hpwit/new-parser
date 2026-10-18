// Driving GPIOs from a script by writing the chip's GPIO registers
// directly -- setting the pins up as well as switching them, with no host
// function call at all.
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
// What pinMode() does, the script does itself (pinOutput() and
// pinInputPullup()), with the same register writes as ESP-IDF's
// gpio_config():
//   - the pin's IO_MUX register: select the GPIO function (MCU_SEL), and
//     for the input, enable the input buffer (FUN_IE) and the pull-up
//     (FUN_PU), clearing the pull-down (FUN_PD);
//   - for the output, GPIO_FUNCn_OUT_SEL_CFG = 256 (SIG_GPIO_OUT_IDX):
//     the pin is driven by its bit in GPIO_OUT;
//   - GPIO_ENABLE: the pin's bit set for an output, cleared for an input.
//
// Same sketch for ESP32 and ESP32-S3. The registers and fields are the
// same on both, but not their addresses or every value, so the host hands
// them to the script from the SDK headers of the chip being built for:
//   - GPIO registers: base 0x3FF44000 on ESP32, 0x60004000 on ESP32-S3;
//     pins 0-31 are in GPIO_OUT/GPIO_ENABLE/GPIO_IN, pins 32 and up in
//     GPIO_OUT1/GPIO_ENABLE1/GPIO_IN1;
//   - the pin's IO_MUX register, from GPIO_PIN_MUX_REG[] (on ESP32 they
//     are not in pin order: GPIO 0 is at +0x44, GPIO 2 at +0x40);
//   - the GPIO function number (PIN_FUNC_GPIO): 2 on ESP32, 1 on ESP32-S3.
// The script itself has no address, pin number or chip in it.
//
// Pins: the LED is GPIO 2 on ESP32 (the on-board LED of most dev kits).
// On ESP32-S3 the on-board LED is usually a WS2812, which a GPIO level
// cannot light, so connect an LED (with a resistor) to GPIO 5, or change
// LED_PIN. The BOOT button has its own pull-up resistor on both dev kits,
// so the button works even where the IO_MUX pull-up does not apply (on
// ESP32, ESP-IDF may set the pull-up of RTC-capable pins such as GPIO 0
// through the RTC IO registers instead).
//
// The script language spells bitwise AND `and` (`&` alone is address-of);
// `|` is bitwise OR. Both have the precedence of `*`, so put parentheses
// around them when they meet `+` or `-`.
#include "script_executable.h"
#include "binding.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_sig_map.h"
#include "soc/io_mux_reg.h"
#include "soc/gpio_periph.h"

#if CONFIG_IDF_TARGET_ESP32S3
#define LED_PIN 5
#else
#define LED_PIN 2
#endif
#define BUTTON_PIN 0

// Values the script reads (bound by address below).
static uint32_t ledBit, buttonBit;
static uint32_t muxClear = ~(uint32_t)(MCU_SEL_M | FUN_PD);
static uint32_t muxGpio = (uint32_t)PIN_FUNC_GPIO << MCU_SEL_S;
static uint32_t muxInput = FUN_IE;
static uint32_t muxPullup = FUN_PU;
static uint32_t gpioOutSignal = SIG_GPIO_OUT_IDX;

char script[] = R"EOF(
// Registers.
external uint32_t led_mux;           // IO_MUX register of the LED pin
external uint32_t button_mux;        // IO_MUX register of the button pin
external uint32_t led_out_sel;       // GPIO_FUNCn_OUT_SEL_CFG of the LED pin
external uint32_t led_enable_set;    // write a bit: that pin becomes an output
external uint32_t button_enable_clear; // write a bit: that pin stops being an output
external uint32_t led_on;            // write a bit: that pin goes high
external uint32_t led_off;           // write a bit: that pin goes low
external uint32_t button_in;         // input levels, one bit per pin

// Values for this chip and these pins.
external uint32_t led_bit;
external uint32_t button_bit;
external uint32_t mux_clear;         // keeps everything but the function and the pull-down
external uint32_t mux_gpio;          // the GPIO function
external uint32_t mux_input;         // input buffer on
external uint32_t mux_pullup;        // pull-up on
external uint32_t gpio_out_signal;   // "driven by GPIO_OUT"

external void delay(uint32_t ms);

// pinMode(LED_PIN, OUTPUT)
void pinOutput()
{
   led_mux = (led_mux and mux_clear) | mux_gpio;
   led_out_sel = gpio_out_signal;
   led_enable_set = led_bit;
}

// pinMode(BUTTON_PIN, INPUT_PULLUP)
void pinInputPullup()
{
   button_mux = (button_mux and mux_clear) | mux_gpio | mux_input | mux_pullup;
   button_enable_clear = button_bit;
}

void main()
{
   pinOutput();
   pinInputPullup();
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

static void bindRegister(const char *name, uint32_t address)
{
   bindVariable((char *)"uint32_t", (char *)name, NULL, (void *)address);
}

static void bindValue(const char *name, uint32_t *value)
{
   bindVariable((char *)"uint32_t", (char *)name, NULL, (void *)value);
}

void setup()
{
   Serial.begin(115200);

   bool ledHigh = LED_PIN >= 32, buttonHigh = BUTTON_PIN >= 32;
   ledBit = 1UL << (LED_PIN % 32);
   buttonBit = 1UL << (BUTTON_PIN % 32);

   bindRegister("led_mux", GPIO_PIN_MUX_REG[LED_PIN]);
   bindRegister("button_mux", GPIO_PIN_MUX_REG[BUTTON_PIN]);
   bindRegister("led_out_sel", GPIO_FUNC0_OUT_SEL_CFG_REG + 4 * LED_PIN);
   bindRegister("led_enable_set", ledHigh ? GPIO_ENABLE1_W1TS_REG : GPIO_ENABLE_W1TS_REG);
   bindRegister("button_enable_clear", buttonHigh ? GPIO_ENABLE1_W1TC_REG : GPIO_ENABLE_W1TC_REG);
   bindRegister("led_on", ledHigh ? GPIO_OUT1_W1TS_REG : GPIO_OUT_W1TS_REG);
   bindRegister("led_off", ledHigh ? GPIO_OUT1_W1TC_REG : GPIO_OUT_W1TC_REG);
   bindRegister("button_in", buttonHigh ? GPIO_IN1_REG : GPIO_IN_REG);

   bindValue("led_bit", &ledBit);
   bindValue("button_bit", &buttonBit);
   bindValue("mux_clear", &muxClear);
   bindValue("mux_gpio", &muxGpio);
   bindValue("mux_input", &muxInput);
   bindValue("mux_pullup", &muxPullup);
   bindValue("gpio_out_signal", &gpioOutSignal);

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
