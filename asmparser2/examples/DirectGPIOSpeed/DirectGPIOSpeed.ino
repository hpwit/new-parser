// How fast a script can switch a GPIO by writing the chip's GPIO
// registers directly (see DirectGPIO.ino for how the registers are bound
// and how the pin is set up without pinMode()).
//
// The script's main() sets the LED pin up as an output, then switches it
// on and off 10000 times in a row, as fast as it goes:
//
//    for (int i = 0; i < 10000; i++)
//    {
//       led_on = led_bit;    // one store to GPIO_OUT_W1TS
//       led_off = led_bit;   // one store to GPIO_OUT_W1TC
//    }
//
// The host times that with micros(), then runs the same loop compiled
// natively, and prints both: the time, the rate of full on/off cycles
// (the frequency a scope sees on the pin) and the CPU cycles per cycle.
// Repeats every 2 seconds.
//
// Each write to a GPIO register goes over the peripheral bus, which is
// slower than the CPU, and that is what sets the pace. Measured on an
// ESP32 at 240 MHz: script 1011 us, native 1006 us for the 10000 cycles --
// 9.9 MHz on the pin, about 24 CPU cycles per on/off, the script's extra
// load of led_bit and loop counter hidden behind the bus writes.
//
// Pins: GPIO 2 on ESP32 (the on-board LED of most dev kits: at these
// speeds it just looks dimly lit), GPIO 5 on ESP32-S3. Put a scope or a
// logic analyser on the pin to see the square wave.
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
#define TOGGLES 10000

// Values the script reads (bound by address below).
static uint32_t ledBit;
static uint32_t muxClear = ~(uint32_t)(MCU_SEL_M | FUN_PD);
static uint32_t muxGpio = (uint32_t)PIN_FUNC_GPIO << MCU_SEL_S;
static uint32_t gpioOutSignal = SIG_GPIO_OUT_IDX;

char script[] = R"EOF(
external uint32_t led_mux;        // IO_MUX register of the LED pin
external uint32_t led_out_sel;    // GPIO_FUNCn_OUT_SEL_CFG of the LED pin
external uint32_t led_enable_set; // write a bit: that pin becomes an output
external uint32_t led_on;         // write a bit: that pin goes high
external uint32_t led_off;        // write a bit: that pin goes low

external uint32_t led_bit;
external uint32_t mux_clear;
external uint32_t mux_gpio;
external uint32_t gpio_out_signal;

// pinMode(LED_PIN, OUTPUT)
void pinOutput()
{
   led_mux = (led_mux and mux_clear) | mux_gpio;
   led_out_sel = gpio_out_signal;
   led_enable_set = led_bit;
}

void main()
{
   pinOutput();
   for (int i = 0; i < 10000; i++)
   {
      led_on = led_bit;
      led_off = led_bit;
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

static volatile uint32_t *const ledOnReg = (volatile uint32_t *)(LED_PIN >= 32 ? GPIO_OUT1_W1TS_REG : GPIO_OUT_W1TS_REG);
static volatile uint32_t *const ledOffReg = (volatile uint32_t *)(LED_PIN >= 32 ? GPIO_OUT1_W1TC_REG : GPIO_OUT_W1TC_REG);

// The same loop, compiled natively. noinline so that it is timed as the
// loop it is, not folded into the caller.
static void __attribute__((noinline)) nativeToggle()
{
   uint32_t bit = ledBit;
   for (int i = 0; i < TOGGLES; i++)
   {
      *ledOnReg = bit;
      *ledOffReg = bit;
   }
}

static void report(const char *name, unsigned long us)
{
   double hz = TOGGLES / (us / 1e6);
   double cycles = (double)getCpuFrequencyMhz() * us / TOGGLES;
   printf("%s: %d on/off cycles in %lu us -> %.2f MHz, %.1f CPU cycles per on/off\n", name, TOGGLES, us,
          hz / 1e6, cycles);
}

// Set once the script is compiled and loaded (ScriptExecutable is not
// assignable: it lives as a static inside setup()).
static ScriptExecutable *exec = NULL;

void setup()
{
   Serial.begin(115200);
   delay(500);

   bool ledHigh = LED_PIN >= 32;
   ledBit = 1UL << (LED_PIN % 32);
   bindRegister("led_mux", GPIO_PIN_MUX_REG[LED_PIN]);
   bindRegister("led_out_sel", GPIO_FUNC0_OUT_SEL_CFG_REG + 4 * LED_PIN);
   bindRegister("led_enable_set", ledHigh ? GPIO_ENABLE1_W1TS_REG : GPIO_ENABLE_W1TS_REG);
   bindRegister("led_on", ledHigh ? GPIO_OUT1_W1TS_REG : GPIO_OUT_W1TS_REG);
   bindRegister("led_off", ledHigh ? GPIO_OUT1_W1TC_REG : GPIO_OUT_W1TC_REG);
   bindValue("led_bit", &ledBit);
   bindValue("mux_clear", &muxClear);
   bindValue("mux_gpio", &muxGpio);
   bindValue("gpio_out_signal", &gpioOutSignal);

   printf("LED on GPIO %d, CPU %lu MHz\n", LED_PIN, (unsigned long)getCpuFrequencyMhz());
   static ScriptExecutable compiled = parseScript(script);
   if (!compiled.isExeExists())
   {
      printf("script failed to compile/load\n");
      return;
   }
   compiled.execute("main"); // first run: global initialisation, not timed
   exec = &compiled;
}

void loop()
{
   if (exec == NULL)
      return;
   unsigned long t0 = micros();
   exec->executeOnly("main");
   unsigned long scriptUs = micros() - t0;

   t0 = micros();
   nativeToggle();
   unsigned long nativeUs = micros() - t0;

   report("script", scriptUs);
   report("native", nativeUs);
   printf("script/native = %.2f\n\n", (double)scriptUs / nativeUs);
   delay(2000);
}
