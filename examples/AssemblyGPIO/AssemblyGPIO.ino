// Driving a GPIO from Xtensa assembly inside a script, and an assembly
// function that takes values and gives one back.
//
// The LED blinks; it blinks fast while the BOOT button (GPIO 0) is held
// down. Before that, the script prints what rgb565() makes of a few colours,
// and how fast burst() can toggle the pin.
//
// The GPIO registers reach the script as in DirectGPIO: each is bound as an
// external variable whose address is the register itself. In an __ASM__
// function, "movExt a4,@_ext_led_on" puts that address in a4 (the loader
// patches it in, as for the script's own uses of an external variable), and
// "s32i a2,a4,0" writes the register: here GPIO_OUT_W1TS, where a 1 bit sets
// that pin high. A value bound the same way (led_bit, the pin's bit) is one
// more load: "movExt a4,@_ext_led_bit" then "l32i a4,a4,0". A variable of
// the script itself is reached with "l32r a4,@_name" instead.
//
// The functions:
//   ledWrite(on)       the LED on or off
//   buttonPressed()    1 while the BOOT button is held (its level is 0)
//   burst(count)       count pulses on the LED pin, as fast as the CPU goes
//   cycles()           the CPU's cycle counter, to time burst()
//   rgb565(r, g, b)    three 0-255 values in, one 16-bit RGB565 colour out
//
// The calling convention is the one of the script's own functions: the
// arguments arrive in a2, a3, a4..., an int result goes back in a2. Start
// with "entry a1,32" and end with "retw.n". See the README's "Assembly
// functions".
//
// The pins are set up by pinMode() here in C, to keep the example about the
// assembly (DirectGPIO shows the register writes for that too). Same sketch
// for ESP32 and ESP32-S3: the register addresses come from the SDK headers.
// The LED is GPIO 2 on ESP32 (the on-board LED of most dev kits); on ESP32-S3
// connect one (with a resistor) to GPIO 5, or change LED_PIN.
#include "script_executable.h"
#include "binding.h"
#include "soc/gpio_reg.h"

#if CONFIG_IDF_TARGET_ESP32S3
#define LED_PIN 5
#else
#define LED_PIN 2
#endif
#define BUTTON_PIN 0

static uint32_t ledBit = 1u << LED_PIN;
static uint32_t buttonBit = 1u << BUTTON_PIN;

char script[] = R"EOF(
external uint32_t led_on;      // GPIO_OUT_W1TS: a 1 bit sets that pin high
external uint32_t led_off;     // GPIO_OUT_W1TC: a 1 bit sets it low
external uint32_t button_in;   // GPIO_IN: the level of every pin
external uint32_t led_bit;     // 1 << the LED's pin
external uint32_t button_bit;  // 1 << the button's pin
external void delay(uint32_t ms);

// the LED on (on != 0) or off
__ASM__ void ledWrite(int on)
{
"entry a1,32"
"movExt a4,@_ext_led_bit"
"l32i a4,a4,0"
"movExt a5,@_ext_led_on"
"bnez a2,write_led"
"movExt a5,@_ext_led_off"
"write_led:"
"s32i a4,a5,0"
"retw.n"
}

// 1 while the button is held down (its pin reads 0), else 0
__ASM__ int buttonPressed()
{
"entry a1,32"
"movExt a4,@_ext_button_in"
"l32i a4,a4,0"
"movExt a5,@_ext_button_bit"
"l32i a5,a5,0"
"and a4,a4,a5"
"movi a2,0"
"bnez a4,button_done"
"movi a2,1"
"button_done:"
"retw.n"
}

// `count` pulses on the LED pin: high, low, again, nothing else in the loop
__ASM__ void burst(int count)
{
"entry a1,32"
"movExt a4,@_ext_led_bit"
"l32i a4,a4,0"
"movExt a5,@_ext_led_on"
"movExt a6,@_ext_led_off"
"beqz a2,burst_done"
"burst_loop:"
"s32i a4,a5,0"
"s32i a4,a6,0"
"addi a2,a2,-1"
"bnez a2,burst_loop"
"burst_done:"
"memw"
"retw.n"
}

__ASM__ uint32_t cycles()
{
"entry a1,32"
"rsr a2,ccount"
"retw.n"
}

// red, green, blue (0-255) in, an RGB565 colour out:
// rrrrrggg gggbbbbb, the top 5, 6 and 5 bits of each
__ASM__ int rgb565(int r, int g, int b)
{
"entry a1,32"
"extui a2,a2,3,5"
"slli a2,a2,11"
"extui a3,a3,2,6"
"slli a3,a3,5"
"extui a4,a4,3,5"
"or a2,a2,a3"
"or a2,a2,a4"
"retw.n"
}

void main()
{
   printfln("rgb565: white %d, red %d, green %d, blue %d, grey %d", rgb565(255, 255, 255), rgb565(255, 0, 0),
            rgb565(0, 255, 0), rgb565(0, 0, 255), rgb565(128, 128, 128));
   uint32_t t0 = cycles();
   burst(10000);
   uint32_t t1 = cycles();
   // 240 cycles per microsecond: 10000 pulses in (t1 - t0) / 240 us,
   // so 10000 * 240000 / (t1 - t0) kHz (written so that it fits an int)
   printfln("burst: 10000 pulses in %d cycles, %d kHz", t1 - t0, 2400000 / ((t1 - t0) / 1000));
   while (true)
   {
      uint32_t wait = 500;
      if (buttonPressed())
         wait = 100;
      ledWrite(1);
      delay(wait);
      ledWrite(0);
      delay(wait);
   }
}
)EOF";

void setup()
{
   Serial.begin(115200);
   delay(1000);
   pinMode(LED_PIN, OUTPUT);
   pinMode(BUTTON_PIN, INPUT_PULLUP);
   // the registers, by their address; the values, by the address of the variable holding them
   bindVariable((char *)"uint32_t", (char *)"led_on", NULL, (void *)GPIO_OUT_W1TS_REG);
   bindVariable((char *)"uint32_t", (char *)"led_off", NULL, (void *)GPIO_OUT_W1TC_REG);
   bindVariable((char *)"uint32_t", (char *)"button_in", NULL, (void *)GPIO_IN_REG);
   bindVariable((char *)"uint32_t", (char *)"led_bit", NULL, (void *)&ledBit);
   bindVariable((char *)"uint32_t", (char *)"button_bit", NULL, (void *)&buttonBit);
   bindFunction((char *)"void", (char *)"delay", (char *)"uint32_t", (void *)delay);
   static ScriptExecutable exec = parseScript(script);
   if (exec.isExeExists())
      exec.execute("main");
   else
      showCompileError();
}

void loop()
{
}
