<div align="center">

# ⚡ ESPLiveScript 2

**Write C-like scripts, compile them to real Xtensa machine code _on the ESP32 itself_, and run them. No IDE, no re-flash.**

![ESP32](https://img.shields.io/badge/ESP32-S2%20%7C%20S3-E7352C?logo=espressif&logoColor=white)
![Arduino library](https://img.shields.io/badge/Arduino-library-00979D?logo=arduino&logoColor=white)
![Version](https://img.shields.io/badge/version-1.3.0-blue)
![Compiled](https://img.shields.io/badge/compiled%20to-Xtensa%20machine%20code-orange)
![Tested](https://img.shields.io/badge/tested-host%20%C2%B7%20QEMU%20%C2%B7%20fuzzer%20%C2%B7%20real%20board-brightgreen)

[Quick start](#first-light-) · [The language](#what-you-can-do-with-the-language) · [More than leds](#more-than-a-led-language) · [Performance](#performance) · [Examples](#examples) · [Testing](#testing)

</div>

---

## ✨ At a glance

| | |
|:--|:--|
| 🚀 **Native speed** | No interpreter: the script becomes Xtensa machine code on the chip. Loops and floats run at 1 to 1.3 times the time of the same code compiled by gcc, the Mandelbrot set at 24fps on 128x96 ([numbers](#performance)). |
| 🧱 **A real language** | Structs with methods and constructors, `switch`, pointers, strings, recursion, `float` and built-in fixed point (`ffloat`), multidimensional arrays ([more than leds](#more-than-a-led-language)). |
| 🔌 **Talks to your C++** | Call your own functions and use your variables (FastLED, WiFi, sensors...) with `bindFunction()` / `bindVariable()`. |
| 🧵 **Runs alongside your sketch** | Background tasks you can pause, restart and kill; GPIO interrupts calling back into the script; direct register access (a pin switched at 10MHz). |
| 💾 **Compile once, run anywhere** | Save the compiled binary to flash or SD and load it later, without the source, even from another sketch. |
| 🛡️ **Checked, not just trusted** | The whole compiler also runs on a computer; the generated code is executed under QEMU, compared frame by frame with C, and fuzzed against C. |

---

## 📖 The story

A while back I got tired of loading code onto the esp32 through an IDE every time I wanted to try a new led animation, so I wrote [ESPLiveScript](https://github.com/hpwit/asmparser): a small C-like language that compiles straight to real Xtensa machine code on the esp32 itself. No interpreter, no re-flash.

It worked, people used it and [StarLight](https://github.com/MoonModules/StarLight) picked it up. But I wrote v1 the way most first compilers are written: fast, by hand, no grammar, and with no real way to check that a fix hadn't broken something else. Every fix was "works on my board".

**So I rewrote it.** This is v2: same idea, same language, written from scratch. This time the compiler can be checked, not just trusted:
- the whole compiler (tokenizer, parser, assembler, loader) also runs as a normal program on my laptop, no esp32 needed (`test/host/`);
- the compiled machine code runs for real on Espressif's QEMU esp32 emulator and the results are checked (`test/qemu/`);
- random programs are run both through the compiler and as plain C, and the results compared (`test/fuzz/`);
- every script from v1's `sc_examples` directory is compiled by the test suite (`test/sc_examples/`).

None of this changes what you write in a script, but it is why I can say that something works instead of hoping it does.

**If you know v1:** the language is very close, so most of what you know still applies. The C++ side you call from your sketch is new (`ScriptExecutable`, `parseScript()`, `bindFunction()`, `bindVariable()`), some v1 features are not there yet (see [Known limitations](#known-limitations)) and a few things changed on purpose. I point out the differences as we go.

---

## 🗺️ Contents

<!-- TOC start -->

- [Which language?](#which-language)
  * [C like language](#c-like-language)
  * [DIY parser and compiler](#diy-parser-and-compiler)
  * [Not a development environment](#not-a-development-environment)

- [More than a led language](#more-than-a-led-language)

- [First light](#first-light-)
  * [Checking for errors](#checking-for-errors)
  * [Freeing an executable](#freeing-an-executable)

- [The function you call can have input parameters](#the-function-you-call-can-have-input-parameters)

- [Interaction with pre-compiled functions](#interaction-with-pre-compiled-functions)
  * [Access to pre-compiled variables](#access-to-pre-compiled-variables)
  * [Calling pre-compiled functions](#calling-pre-compiled-functions)

- [CRGB and the hue function](#crgb-and-the-hue-function)

- [Safe mode and arrays](#safe-mode-and-arrays)

- [Variable types](#variable-types)
  * [Fixed point: ffloat](#fixed-point-ffloat)
  * [Arrays and multidimensional arrays](#arrays-and-multidimensional-arrays)
  * [Structures](#structures)

- [Saving executables](#saving-executables)
  * [Binded functions](#binded-functions)

- [What you can do with the language](#what-you-can-do-with-the-language)
  * [Use of define](#use-of-define)

- [Running scripts in the background](#running-scripts-in-the-background)

- [Performance](#performance)
  * [Summary](#summary)
  * [Function calls: fib(40)](#function-calls-fib40)
  * [Loops and arrays: Game of Life](#loops-and-arrays-game-of-life)
  * [Floating point: Mandelbrot](#floating-point-mandelbrot)
  * [Structs and methods: bouncing balls](#structs-and-methods-bouncing-balls)
  * [Calling a host function per pixel: rainbow](#calling-a-host-function-per-pixel-rainbow)
  * [A real script: MultiEffectController](#a-real-script-multieffectcontroller)
  * [GPIO switching speed](#gpio-switching-speed)
  * [Compiling](#compiling)

- [Advanced stuff](#advanced-stuff)
  * [Pointer to the executable and interrupts](#pointer-to-the-executable-and-interrupts)

- [Testing](#testing)

- [Examples](#examples)

- [Known limitations](#known-limitations)

- [Conclusion](#conclusion)

<!-- TOC end -->


---

# Which language?

## C like language
Same choice as v1: a C like syntax, close to JavaScript with stronger typing. It is a loose adaptation of C, not a strict subset, but you can write programs like this:

```c
void main()
{
   int h = 1;
   while (h > 0)
   {
      for (int i = 0; i < width; i++)
      {
         for (int j = 0; j < height; j++)
         {
            render2D(i, j);
         }
      }
      show();
      h++;
   }
}
```

## DIY parser and compiler
Still written by hand, still no grammar tool, and I am still not a compiler specialist :). The difference with v1 is that every change now goes through the tests described in [Testing](#testing).

## Not a development environment
This library compiles and runs scripts; it doesn't give you a place to write them. [LedOS](https://github.com/hpwit/LedOS) (a console terminal to edit, save and run programs) and [StarLight](https://github.com/MoonModules/StarLight) (a web-enabled esp32 application) both use v1. Neither has been ported to v2 yet.

---

# More than a led language

ESPLiveScript started as a way to try led animations without re-flashing, and the led side is still where it shines. But the language has grown into something much more **general purpose**: what you write is plain C-like code that has nothing to do with leds, compiled to machine code like any other function of your sketch.

```c
struct Account
{
   int balance;
   int id;
   void deposit(int amount)
   {
      balance += amount;
   }
};

Account accounts[3];

char *level(int b)
{
   if (b >= 100)
      return "gold";
   return "standard";
}

int fact(int n)
{
   if (n <= 1)
      return 1;
   return n * fact(n - 1);
}

void main()
{
   for (int i = 0; i < 3; i++)
   {
      accounts[i].id = i;
      accounts[i].balance = 0;
      accounts[i].deposit(fact(i + 3));
   }
   for (int i = 0; i < 3; i++)
   {
      switch (accounts[i].id)
      {
      case 0:
         printfln("first account: %d (%s)", accounts[i].balance, level(accounts[i].balance));
         break;
      default:
         printfln("account %d: %d (%s)", accounts[i].id, accounts[i].balance, level(accounts[i].balance));
      }
   }
}
```

```
first account: 6 (standard)
account 1: 24 (standard)
account 2: 120 (gold)
```

What you get today, led or no led:

| | |
|:--|:--|
| 🧮 **Numbers** | `int`, `uint8_t`... `uint32_t`, `float` on the hardware FPU, `ffloat` fixed point, `^` power, `<<` `>>`, bitwise `and` / `or` |
| 🧱 **Data** | structs with methods and constructors, arrays of structs, 1D/2D/3D arrays, pointers, `char *` strings |
| 🔀 **Control flow** | `if`/`else`, `while`, `for`, `break`/`continue`, `switch`/`case`/`default`, the ternary operator, short-circuit `&&` / `\|\|`, recursion |
| 🧠 **Memory** | `alloc()` / `free()` with `writeInt()` / `readInt()`, `safe_mode` bounds checking |
| 🔌 **Your C++** | any function or variable of the sketch, bound by name; scripts calling them and them calling back into the script |
| 🧵 **System** | background FreeRTOS tasks, GPIO interrupts, direct register access, binaries saved and loaded |

Examples that have nothing to do with leds: [Factorial](examples/Factorial), [SwitchCase](examples/SwitchCase), [StringFunctions](examples/StringFunctions), [MemoryAllocator](examples/MemoryAllocator), [PinInterrupt](examples/PinInterrupt), [DirectGPIO](examples/DirectGPIO), [ExecuteAsTask](examples/ExecuteAsTask), [SaveScriptBinary](examples/SaveScriptBinary).


---

# First light :)

- Compile a script: `ScriptExecutable exec = parseScript(script);`
- Check that it compiled: `if (exec.isExeExists())`
- Call a function of the script by its name, for instance `main`: `exec.execute("main");`

If you run this program ([SimpleScript](examples/SimpleScript)):

```cpp
#include "script_executable.h"

char script[] = R"EOF(
void main()
{
   for (int i = 0; i < 20; i++)
   {
      printfln("i:%2d  3*i:%2d", i, 3 * i);
   }
}
)EOF";

void setup()
{
   Serial.begin(115200);

   ScriptExecutable exec = parseScript(script);
   if (exec.isExeExists())
   {
      exec.execute("main");
   }
}

void loop()
{
}
```

the output will be:

```
i: 0 3*i: 0
i: 1 3*i: 3
i: 2 3*i: 6
...
i:19 3*i:57
```

> [!NOTE]
> `printf` and `printfln` are built into the compiler, there is nothing to bind. Same for `alloc`, `free`, `writeInt` and `readInt` (see [What you can do with the language](#what-you-can-do-with-the-language)).

> [!NOTE]
> You can call any function of the script, not only `main`, and unlike v1 you get its return value back (see the next section).

## Checking for errors
`exec.isExeExists()` is `true` when parsing, assembling and loading all worked. When it is `false`, `parseScript()` has already printed why: the line and position of a syntax error, or the assembler/loader message. There is no separate error object like v1's `Executable::error`.

## Freeing an executable
A `ScriptExecutable` frees its memory by itself when it goes out of scope, so most of the time you have nothing to do. If you want the memory back sooner, for instance before compiling a second script, use `exec.free()`:

```cpp
ScriptExecutable exec1 = parseScript(script1);
if (exec1.isExeExists())
{
   exec1.execute("main");
}
exec1.free(); // script1's memory is released now

ScriptExecutable exec2 = parseScript(script2);
if (exec2.isExeExists())
{
   exec2.execute("main");
}
```

> [!WARNING]
> A `ScriptExecutable` can't be copied or reassigned. For a second script, create a second `ScriptExecutable` as above.

`free()` can be called several times without harm. See [TwoScripts](examples/TwoScripts) for the complete sketch.

---

# The function you call can have input parameters

```cpp
Arguments args;
args.add(5);
int32_t result = 0;
exec.execute("fact", &args, &result);
```

> [!NOTE]
> For now arguments can only be `int` or `float`, same as v1.

In this example we calculate a factorial in the script and read the result back in the sketch ([Factorial](examples/Factorial)):

```cpp
#include "script_executable.h"

char script[] = R"EOF(
int fact(int h)
{
   if (h == 1)
   {
      return 1;
   }
   return h * fact(h - 1);
}

void main()
{
}
)EOF";

void setup()
{
   Serial.begin(115200);

   ScriptExecutable exec = parseScript(script);
   if (exec.isExeExists())
   {
      Arguments args;
      for (int g = 5; g <= 7; g++)
      {
         args.clear();
         args.add(g);
         int32_t result = 0;
         if (exec.execute("fact", &args, &result))
         {
            printf("factorial of %d is %d\n", g, result);
         }
      }
   }
}

void loop()
{
}
```

result:

```
factorial of 5 is 120
factorial of 6 is 720
factorial of 7 is 5040
```

> [!NOTE]
> `execute()` calls the function directly, like one script function calling another, which is why you get the return value. `main()` is no exception: `exec.execute("main", &result)` works too.

---

# Interaction with pre-compiled functions

A script can't do everything the Espressif compiler can: no WiFi, no SPI or I2C, and you don't want to rewrite FastLED in a script. So a script can call your pre-compiled C++ functions and read/write your C++ variables. In v2 this is done with `bindFunction()` and `bindVariable()` (`binding.h`), called **before** `parseScript()`.

## Access to pre-compiled variables

```cpp
bindVariable((char *)"type", (char *)"name_in_the_script", arraySizeOrNull, (void *)&variable);
```

The script also needs to declare the variable `external`.

Example:

```cpp
#include "script_executable.h"
#include "binding.h"

int variable = 0;
uint16_t _array[10];

char script[] = R"EOF(
external int value;
external uint16_t array[10];

void fillArray()
{
   for (int i = 0; i < 10; i++)
   {
      array[i] = i * 3;
   }
}
void change()
{
   value = value + 2;
}
void main()
{
   printfln("value: %d", value);
}
)EOF";

void setup()
{
   Serial.begin(115200);

   bindVariable((char *)"int", (char *)"value", NULL, (void *)&variable);
   bindVariable((char *)"uint16_t", (char *)"array", (char *)"[10]", (void *)_array);

   ScriptExecutable exec = parseScript(script);
   if (exec.isExeExists())
   {
      variable = 5;
      exec.execute("main");
      variable = 240;
      exec.execute("main");

      variable = 15;
      printf("old value:%d ", variable);
      exec.execute("change");
      printf("new value:%d\n", variable);

      exec.execute("fillArray");
      for (int i = 0; i < 10; i++)
      {
         printf("%d:%d\n", i, _array[i]);
      }
   }
}

void loop()
{
}
```

> [!NOTE]
> Here we call three different functions, all defined in the script.

Result:

```
value: 5
value: 240
old value:15 new value:17
0:0
1:3
2:6
...
9:27
```

## Calling pre-compiled functions

```cpp
bindFunction((char *)"return_type", (char *)"name_in_the_script", (char *)"argument_types", (void *)function);
```

Example:

```cpp
#include "script_executable.h"
#include "binding.h"

void displayfloat(float nb)
{
   printf("from pre-compiled %f\n", nb);
}
float calcul(int pos)
{
   return (float)pos / 34.0;
}
void otherfunction()
{
   printf("from other function\n");
}

char script[] = R"EOF(
external float calc(int pos);
external void displayfloat(float nb);
external void otherfunction();

void main()
{
   float h = calc(52);
   displayfloat(h);
   otherfunction();
}
)EOF";

void setup()
{
   Serial.begin(115200);

   bindFunction((char *)"float", (char *)"calc", (char *)"int", (void *)calcul);
   bindFunction((char *)"void", (char *)"displayfloat", (char *)"float", (void *)displayfloat);
   bindFunction((char *)"void", (char *)"otherfunction", NULL, (void *)otherfunction);

   ScriptExecutable exec = parseScript(script);
   if (exec.isExeExists())
   {
      exec.execute("main");
   }
}

void loop()
{
}
```

Result:

```
from pre-compiled 1.529412
from other function
```

For bigger sketches built the same way, see [StructsAndHostBindings](examples/StructsAndHostBindings) and [BouncingBalls](examples/BouncingBalls).

---

# CRGB and the hue function

If you are playing with leds, `CRGB` is part of the language, nothing to bind:
```c
CRGB color = CRGB(red, green, blue);
```

**Unlike v1 there is no built-in `hsv()` and no `USE_FASTLED` switch.** If you want `hsv()` or a `leds` array shown by your driver, bind them like any other function or variable:

```cpp
#include "script_executable.h"
#include "binding.h"
#define NUM_LEDS 256

CRGB ledsBuffer[NUM_LEDS];
CRGB scriptHsv(int hue, int sat, int val) { return CHSV(hue, sat, val); } // with FastLED
void scriptShow() { FastLED.show(); }

char script[] = R"EOF(
external CRGB leds[256];
external CRGB hsv(int h, int s, int v);
external void show();

void main()
{
   int k = 0;
   while (true)
   {
      for (int i = 0; i < 128; i++)
         for (int j = 0; j < 96; j++)
            leds[j * 16 + i] = hsv(i + j + k, 255, 255);
      k++;
      show();
   }
}
)EOF";

void setup()
{
   Serial.begin(115200);
   FastLED.addLeds<NEOPIXEL, DATA_PIN>(ledsBuffer, NUM_LEDS);

   bindVariable((char *)"CRGB", (char *)"leds", (char *)"[256]", (void *)ledsBuffer);
   bindFunction((char *)"CRGB", (char *)"hsv", (char *)"int,int,int", (void *)scriptHsv);
   bindFunction((char *)"void", (char *)"show", NULL, (void *)scriptShow);

   ScriptExecutable exec = parseScript(script);
   if (exec.isExeExists())
   {
      exec.execute("main");
   }
}

void loop()
{
}
```

See [BouncingBalls](examples/BouncingBalls) for a complete working version.

---

# Safe mode and arrays

Let's consider this script:

```c
external uint16_t array[10];

void main()
{
   for (int i = 0; i < 200; i++)
   {
      array[i] = 200;
   }
}
```

It will write past the end of the array, with unintended consequences.

To avoid this, put `safe_mode` in the script (no `#`, same as v1):

```c
safe_mode
external uint16_t array[10];

void main()
{
   for (int i = 0; i < 200; i++)
   {
      array[i] = 200;
   }
}
```

you will get

```
Execution error: index 10 is out of bounds for array 'array' (size 10)
```

The write doesn't happen and the whole script stops: you are back in your sketch and `execute()` returns `false`. The details stay available until the next call:

```cpp
ScriptExecutable exec = parseScript(script);
if (!exec.execute("main") && exec.hasRuntimeError())
{
   const runtime_error_struct &e = exec.runtimeError();
   // e.array == "array", e.size == 10, e.index == 10
}
```

To report the error your own way (on a display, in a log...), install a handler. It is called instead of the console message:

```cpp
#include "runtime_functions.h"

void onBoundsError(const runtime_error_struct *e)
{
   Serial.printf("%s[%d] out of range (size %u)\n", e->array, (int)e->index, (unsigned)e->size);
}

onScriptRuntimeError = onBoundsError;
```

What is checked: writes to global and `external` arrays declared with a size (`int buf[10];`, `external CRGB leds[96, 128];`), negative indexes included. For a multidimensional array the flattened index is checked against the total size.

What is not checked: reads, plain pointers and arrays declared without a size (`external uint8_t raw[];`).

`safe_mode` only applies to the script it is written in.

> [!TIP]
> Every checked write costs three extra instructions, so it will slow the script down. Use it when you are hunting an indexing bug. See [SafeMode](examples/SafeMode).

---

# Variable types

Here are the types:
* `uint8_t`
* `char`
* `bool`: `true`, `false`
* `int`: int over 4 bytes
* `s_int`: int over 2 bytes
* `uint16_t`
* `uint32_t`
* `float`
* `ffloat`: fixed point written like a float (see below)
* `CRGB`
* `CRGBW`

## Fixed point: ffloat

`ffloat` is a 32-bit fixed-point number: 16 integer bits and 16 fraction bits, so about ±32768 in steps of 1/65536. You write it exactly like a float, and the compiler does the fixed-point arithmetic with integer instructions:

```c
ffloat m = 2.3 / 4 + 2;     // computed at compile time: 2.575
ffloat k = m * 0.5;         // fixed-point multiply, no float instruction
if (k > 1.25)               // integer comparison
   k = k / 3;
int i = m;                  // 2 (toward zero, like a float)
```

What you need to know:
- constants are computed at compile time;
- `+`, `-`, `%`, comparisons, `++`/`--` and `abs` are plain integer instructions;
- `*` is a 64-bit multiply shifted back by 16. Multiplying or dividing by an integer (`x * 3`, `x / n`) is a plain integer multiply or divide. Dividing by zero gives the largest value with the result's sign; it doesn't crash;
- an `int` meeting an `ffloat` is converted for you, and `int i = x;` truncates toward zero like a float;
- as for every type of the language, the type you assign to decides how the expression is computed: `ffloat r = 7 / 2;` is 3.5, `int k = m * 2;` converts `m` to an int first;
- results outside ±32768 wrap around like an int. Watch squares and powers: [BouncingBallsFfloat](examples/BouncingBallsFfloat) computes `(distance / r²)²` instead of `distance² / r⁴`, which goes up to ~72000;
- `printfln` prints the raw 32-bit value of an `ffloat`; print `(int)(x * 100)` or convert it to a `float` first.

> [!TIP]
> The esp32 has a hardware single-precision FPU, so `float` is already fast. `ffloat` wins when there are a lot of conversions, divisions and integer mixing (see [Performance](#performance)).

See [MandelbrotFfloat](examples/MandelbrotFfloat) (the float Mandelbrot with `float` replaced by `ffloat`) next to [MandelbrotFixedPoint](examples/MandelbrotFixedPoint) (the same picture with the scaling done by hand), and [FloatVsFfloat](examples/FloatVsFfloat) for a side-by-side timing.

## Arrays and multidimensional arrays

```c
//definitions
int array[23];
int array2D[height, width];
int array3D[depth, height, width];

//usage
int h = array3D[2, 12, 23];
```

## Structures

You can define new types with `struct`:

```c
struct new_type
{
   float k;
   int l;
};
```

Structures can have methods:

```c
struct new_type
{
   float h;
   int l;
   void display()
   {
      printfln("l:%d", l);
   }
};
```

and constructors:

```c
struct ball
{
   float vx, vy, xc, yc, r;
   int color;

   ball()
   {
      vx = rand(300) / 255.0;
      ...
   }
};

ball Balls[max_nb_balls];
...
Balls[i].updateBall();
```

`varname d;` is the same as `varname d = varname();`, and a constructor with arguments works as you would expect: `varname d2 = varname(23);`.

> [!NOTE]
> - You can have arrays of structures: `new_type arr[10];`
> - A function needs to be defined before it is called (for the moment).
> - Unlike v1, a struct definition ends with `};` like in C.

> [!IMPORTANT]
> Due to memory alignment, the fields need to go from the biggest to the smallest:
> - `float`, `ffloat`, `uint32_t`, `int`
> - `s_int`, `uint16_t`
> - `uint8_t`, `CRGB`, `CRGBW`

---

# Saving executables

You can compile a script once, save the binary to LittleFS or an SD card, and run it later without the source, even from another sketch.

To compile without loading or running: `parseScriptToBinary()`. It returns `NULL` (and has printed the error) if something went wrong:

```cpp
// compiling sketch: never runs the script
uint32_t size = 0;
uint8_t *serialized = parseScriptToBinary(script, &size);
if (serialized != NULL)
{
   // ... write serialized (size bytes) to LittleFS / SD ...
   free(serialized);
}
```

To load it back: `createExecutableFromBuffer()`, which gives you a normal `ScriptExecutable`:

```cpp
// another sketch, later: doesn't need the source
bindVariable(...);   // same external names as when it was compiled
bindFunction(...);
// ... read the saved bytes into buf ...
ScriptExecutable exec = createExecutableFromBuffer(buf, size);
if (exec.isExeExists())
{
   int32_t result = 0;
   exec.execute("someFunction", &result);
}
```

See [SaveScriptBinary](examples/SaveScriptBinary) and [LoadScriptBinary](examples/LoadScriptBinary) for the full pair.

> [!NOTE]
> If you need the lower-level steps (printing the generated assembly before saving, keeping the raw `executable`...), `Parser`, `createBinary()`, `serializeBinary()`, `deserializeBinary()` and `createExecutableFromBinary()` are all still available. See [LanguageBasics](examples/LanguageBasics) where every step is spelled out.

## Binded functions

Same as v1: the bound functions and variables are not saved with the binary. The sketch that loads it needs to call `bindFunction()`/`bindVariable()` for every `external` name the script uses, **before** loading. They are matched by name, so the two sketches only need to agree on the names.

---

# What you can do with the language

Like any normal language you can have:
- loops (`while`, `for`)
- `break`, `continue`
- testing: `if`, `else`, `(test) ? true_statement : false_statement`
- `switch`/`case`/`default`, as in C (see [SwitchCase](examples/SwitchCase)):

  ```c
  switch (mode)
  {
  case MODE_FIRE:
     drawFire();
     break;
  case 1:
  case 2:            // either 1 or 2
     drawStars(mode);
     break;
  default:
  {
     int t = millis() / 100;   // a braced body can have its own variables
     drawPlasma(t);
  }
  }
  ```
  The case values are integer constants (a number, hex, negative, or a `#define`).
- blocks `{ ... }` with their own local variables
- `++`, `--`: for integers and pointers
- `+=`, `-=`, `/=`, `*=`
- pointers
- strings as `char *`: functions returning one (script or bound host function), `char *` parameters, `%s` in `printfln` (see [StringFunctions](examples/StringFunctions)):

  ```c
  char *dayName(int d)
  {
     if (d == 0)
        return "Sunday";
     return "weekday";
  }

  void main()
  {
     char *s = dayName(1);
     printfln("%s / %s", dayName(0), s);   // Sunday / weekday
  }
  ```
  NB: indexing a `char *` local (`s[0]`) is not supported yet.
- `^` for power (`x^2` is x squared, not a xor)
- `>>` and `<<`
- `&&`, `||` (or `and`, `or`)
- type conversion `(float)(expr)`, `(int)(expr)`. The parentheses around `expr` are needed: `(int)x` alone doesn't compile.
  NB: the conversion float <-> int is automatic.
- `define`: see below
- predefined functions:
  * `printf`, `printfln`
  * `alloc(size)`, `free(p)`, `writeInt(p, offset, value)`, `readInt(p, offset)`: the language has no `*p` syntax, so a block from `alloc()` is read and written with these (see [MemoryAllocator](examples/MemoryAllocator))

## Use of define

More or less like in C. **Unlike v1, the `#` is needed:**

```c
#define TOKEN 25

if (i < TOKEN)
{
  ...
}

will be compiled as

if (i < 25)
{
  ...
}
```

> [!NOTE]
> There are no macros yet.

---

# Running scripts in the background

You can run a script function as a task in the background (esp32 only):

```cpp
exec.executeAsTask("function_name");
```

When using this you can also do:

- `exec.pause()`: the task stops where it is
- `exec.restart()`: the task carries on from where it stopped
- `exec.kill()`: the task is deleted
- `exec.state()`: `ScriptState::notStarted`, `running`, `paused` or `stopped`
- `exec.isRunning()`, `exec.isPaused()`

A control that doesn't apply (pausing a paused script...) does nothing and returns `false`. Calling `executeAsTask()` again kills the previous task first, and `free()` or the end of the `ScriptExecutable` kills it too. See [ExecuteAsTask](examples/ExecuteAsTask).

> [!WARNING]
> You can't choose where the task is stopped. It can be inside a host function the script called, holding `Serial`, a lock or a mutex of yours. If that matters, give the script a host function to call in its loop that checks a flag and returns at a point you choose.

What v1 has and v2 doesn't yet: `scriptRuntime` to manage several scripts by name, `sync()` to synchronize their frames, and pre/post kill. See [Known limitations](#known-limitations).

---

# Performance

Everything below was measured on a real esp32 (no PSRAM) at 240MHz, with `micros()` around the script's code only: compiling, and sending a frame to the leds (`show()`), are not counted. "Native" is the same code written in C++ and compiled by the Arduino IDE (gcc `-Os`); "v1" is ESPLiveScript 1.2.0. Where a test calls `hsv()`, it is FastLED's `CHSV` to `CRGB` conversion, for the script and the native code alike, as in a sketch driving a real panel. Every script result was checked against the native one: same frames, same checksums.

## Summary

|Test|Native|v1|v2|v2 / native||
|:----|----:|----:|----:|:----:|:----|
|`fib(40)`, recursive (331 million calls)|16.96s|22.41s|21.72s|1.28|`█████████████`|
|Game of Life 128x128, per generation|5.95ms|14.67ms|7.13ms|1.20|`████████████`|
|Mandelbrot 128x96 `float`, per frame|33.5ms|94.5ms|41.9ms|1.25|`████████████`|
|Mandelbrot 128x96 `ffloat`, per frame|30.0ms (fixed point)|--|31.5ms|1.05|`██████████`|
|60 bouncing balls 128x96 `float`, per frame|6.49ms|--|8.38ms|1.29|`█████████████`|
|60 bouncing balls 128x96 `ffloat`, per frame|7.02ms (fixed point)|--|6.86ms|0.98|`██████████`|
|Rainbow 128x96, one `hsv()` call per pixel, per frame|7.12ms|--|7.13ms|1.00|`██████████`|
|GPIO on/off through the registers|9.9MHz|--|9.9MHz|1.00|`██████████`|

<sub>Each █ is a tenth of the native time: ten blocks is exactly as fast as native code, fewer is faster.</sub>

So a script takes 1 to 1.3 times as long as precompiled code, and the same time when calls to the host (`hsv()` here) dominate; the `ffloat` balls are even slightly faster than the fixed point written by hand in C++. Against v1 it is 2 to 2.3 times faster on loops, arrays and floats, and about the same on function calls. The effects of a real multi-effect script are in [A real script: MultiEffectController](#a-real-script-multieffectcontroller).

## Function calls: fib(40)

```C
int fib(int n)
{
   if (n < 2)
      return n;
   return fib(n - 1) + fib(n - 2);
}
```

`fib(40)` = 102334155 takes 21.7s, about 15.7 cycles per call. At this recursion depth most of a call's cost is the register window overflow and underflow exceptions of the Xtensa windowed ABI, which native code pays too: gcc turns the second call into a loop, so it makes half as many calls. See `examples/FibonacciTiming`.

QEMU does not charge for the window exceptions, so it reads about 5 cycles per call: don't project real hardware times from it.

## Loops and arrays: Game of Life

`examples/GameOfLife`'s script on a 128x128 grid (`uint8_t copy[128, 128]` plus two line buffers, `external CRGB leds[128, 128]`), 50 generations: 357ms in the script, 297ms native, the 51 frames identical. v1 takes 733ms.

What closed most of the gap: array addresses and constants loaded once before a loop instead of at every access, locals kept in registers instead of the stack, 2D indexes computed with shifts, and `a && b` in a condition compiled as one branch per comparison (stopping at the first false one, as in C).

## Floating point: Mandelbrot

The Mandelbrot set on a 128x96 matrix, 50 iterations, `examples/MandelbrotCompare` (which runs all four versions and prints their times and checksums):

|Version|Time per frame|Checksum|
|:----|----:|:----:|
|Native `float`|33.5ms|0c584e72|
|Native fixed point|30.0ms|c026b3cf|
|Script `float`|41.9ms (24fps)|0c584e72|
|Script `ffloat`|31.5ms (32fps)|c026b3cf|
|v1 `float`|94.5ms|0c584e72|

The inner loop (`while (iter < maxIterations && x2 + y2 <= 4)`) is 12 instructions in `float`: the condition is two branches, the constants 2.0 and 4.0 are set once before the loop, and float registers are only saved around a call (the per-pixel divisions, `hsv()`) when their value is still needed after it.

## Structs and methods: bouncing balls

60 balls (a struct with a constructor and two methods, the scripts of `examples/BouncingBalls` and `examples/BouncingBallsFfloat`) drawn on a 128x96 matrix, 100 frames, each clearing the matrix and calling `hsv()` for every pixel lit, `examples/BouncingBallsCompare`:

|Version|Time per frame|Checksum|
|:----|----:|:----:|
|Native `float`|6.49ms|d82c6aa0|
|Native fixed point (16.16)|7.02ms|96c0e39f|
|Script `float`|8.38ms|d82c6aa0|
|Script `ffloat`|6.86ms|96c0e39f|

Here `ffloat` is 18% faster than `float` in a script, and slightly faster than fixed point written by hand in C++.

> [!TIP]
> The esp32 has a single-precision FPU, so `float` is already fast. `ffloat` wins where conversions, divisions and integer mixing dominate, as in the balls; in the Mandelbrot's inner loop it is fixed-point multiplies against single float instructions.

## Calling a host function per pixel: rainbow

`examples/RainbowCompare` draws a scrolling rainbow, 100 frames, the offset `h` going up by one each frame:

```C
for (int i = 0; i < width; i++)
   for (int j = 0; j < height; j++)
      leds[j * width + i] = hsv(i + h + j, 255, 180);
```

Every pixel is a call to the host's `hsv()` (FastLED's `CHSV`) and a `CRGB` store: 12288 calls per frame.

|Version|Time per frame|
|:----|----:|
|Native|7.12ms (140fps)|
|Script|7.13ms (140fps)|

The 100 frames are identical (same hash). The time is FastLED's colour conversion: around it, the script's loop and call cost the same as native code. With a small integer `hsv()` instead (`USE_FASTLED_HSV 0` in the example), native took 4.25ms and the script 6.77ms (measured on 2026-11-01). The script compiles in 0.12s.

## A real script: MultiEffectController

`examples/MultiEffectController` is a 730-line script: a particle fountain, stars, ripples, comets and a plasma, drawn on a 128x96 matrix with a fading trail, all host functions bound (`hsv()` is the sketch's own integer conversion here). Each effect function below was called 100 times on its own with `executeOnly()`, with `millis()` made to return a fixed sequence so the plasma draws the same frames in every build. Time per call:

|Effect|What it does|Time per call|
|:----|:----|----:|
|`fadeTrail()`|fades the 128x96 trail buffer|0.51ms|
|`renderParticles()`|160 particles (structs with methods): move, sort by life, draw, stamp the trail|1.55ms|
|`renderStars()`|twinkling stars: update, sort by brightness, draw|2.01ms|
|`renderRipples()`|expanding rings, restarted when they fade|1.07ms|
|`renderComets()`|move and draw the comets, stamp the trail|0.21ms|
|`renderPlasma()`|four `sin()`, one `hypot()` and six float divisions per pixel|28.1ms|
|`drawModeIndicator()`|the mode bar on the top row|0.06ms|

The plasma's time is the 12288 x 11 calls to `sin()`, `hypot()` and the float division routine. The frames are the same, hash for hash, as with the previous version of the compiler, which took 2.06ms for the particles, 1.18ms for the ripples and 0.37ms for the comets.

## GPIO switching speed

`examples/DirectGPIOSpeed` switches a pin on and off 10000 times by writing the GPIO registers from the script: 1011us, against 1006us for the same loop in C++. That is 9.9MHz on the pin (10MHz on a signal analyser), about 24 cycles per on/off. Both are limited by the peripheral bus, not by the code.

## Compiling

|Script|Lines|Compile time|
|:----|----:|----:|
|`fib()`|12|40ms|
|Rainbow|16|0.12s|
|Game of Life|55|0.78s|
|`MultiEffectController`|730|11s|

The compiler and runtime add about 162KB of flash and 8.6KB of RAM to a sketch. The loader alone (to run a saved binary, see [Saving executables](#saving-executables)) adds 10KB of flash and 1KB of RAM. A short script needs around 30-40KB of heap to compile; `MultiEffectController` uses most of a PSRAM-less esp32's heap but fits.

---

# Advanced stuff

## Pointer to the executable and interrupts

Every script has a variable `_handle_` holding the address of the executable running it (v1 called it `_execaddr_`). You can use it to register an interrupt:

```c
external void pinInterrupt(uint32_t exec, char *name, int pin);

int number = 0;
void increase()
{
   number++;
}

void main()
{
   pinInterrupt(_handle_, "increase", 23);
   while (true)
   {
      printfln("number:%d", number);
   }
}
```

`pinInterrupt` is a normal bound function and the interrupt itself is set up on the C++ side (`gpio_isr_handler_add()`...). See [PinInterrupt](examples/PinInterrupt) for the complete sketch.

> [!WARNING]
> In the interrupt handler call the script with `callFunction((executable *)handle, name, NULL, 0, NULL)` or `exec.executeOnly(name)`, not `execute()`. `execute()` runs the global initializers first and would reset the script's globals on every interrupt.

---

# Testing

This is the part that is really new compared to v1:

- `test/host/`: the compiler built as a normal program on a computer. `make run` for the main suite, `make check-optimized` (same suite at `-Os`, the Arduino default), `make check-asan` (AddressSanitizer), `make run-sc` (every script from v1's `sc_examples`), `make run-large` (a size check for an esp32 without PSRAM).
- `test/qemu/`: the compiled machine code executed on Espressif's QEMU emulator: arithmetic, external calls, recursion, arguments, external variables, save/load, and the `fib()` timing above. See `test/qemu/README.md`.
- `test/effects/`: the led effects of the examples (plasma, fire, voronoi, mandala, Mandelbrot...) run under QEMU and compared frame by frame with the same code written in C.
- `test/fuzz/`: random programs run through the compiler and as plain C, results compared. Every bug found is kept in `test/fuzz/regressions/`. See `test/fuzz/README.md`.

---

# Examples

| Example | What it shows |
| --- | --- |
| `SimpleScript` | The first light example above. |
| `ScriptPrintf` | Printing from a script with `printf()`/`printfln()`. |
| `Factorial` | `Arguments`: calling a function several times with different values. |
| `CallScriptFunction` | Calling a function with a raw `int32_t[]` and using its return value. |
| `TwoScripts` | Two scripts one after the other, freeing the first with `exec.free()`. |
| `FibonacciTiming` | Timing a script with `micros()`. |
| `BouncingBalls` | Structs with methods and constructors, an array of structs, several bound functions. |
| `StructsAndHostBindings` | Structs with `bindFunction()`/`bindVariable()`, smaller. |
| `KeyboardCallback` | An `external` variable, calling a loaded script again from an event handler. |
| `LanguageBasics` | Every compiling step one by one, printing the AST and the generated assembly. |
| `SaveScriptBinary` / `LoadScriptBinary` | Compile once, save to flash, load and run from another sketch. |
| `PrintBinaryHex` | Hex dump of a compiled script and its relocation header. |
| `PrintFibonacciAssembly` | The generated Xtensa assembly of a recursive `fib()`, then running `fib(10)`. |
| `MultiEffectController` | A real multi-effect script of several hundred lines, every binding real. |
| `GameOfLife` | Conway's Game of Life on a 2D `external CRGB leds[h, w]` matrix. |
| `ProceduralEffects` | Five effects for a 128x96 matrix: plasma, fire, Voronoi, mandala and spinning squares ([frames](examples/ProceduralEffects/README.md)). |
| `FloatVsFfloat` | The same calculations in `float` and in `ffloat`, timed side by side. |
| `BouncingBallsFfloat` | The bouncing balls with `ffloat`, and how to stay within ±32768. |
| `MandelbrotFfloat` | The float Mandelbrot with `float` replaced by `ffloat`. |
| `BouncingBallsCompare` | The bouncing balls four ways -- native C++ float and fixed point, script `float` and `ffloat` -- timed, with checksums (see [Performance](#structs-and-methods-bouncing-balls)). |
| `MandelbrotCompare` | The Mandelbrot set four ways -- native C++ float and fixed point, script `float` and `ffloat` -- timed, with a checksum showing they draw the same picture. |
| `MandelbrotFixedPoint` | The Mandelbrot set with integers only, fixed point done by hand. |
| `ExecuteAsTask` | `main()` on its own FreeRTOS task, with `pause()`/`restart()`/`kill()`/`state()`. esp32 only. |
| `PinInterrupt` | A GPIO interrupt calling back into the running script. esp32 only. |
| `DirectGPIO` | Driving pins from a script through the GPIO and IO_MUX registers; esp32 and esp32-S3. |
| `RainbowCompare` | A scrolling rainbow drawn by a script and natively, 100 frames timed, with a hash showing they are identical (see [Performance](#calling-a-host-function-per-pixel-rainbow)). |
| `DirectGPIOSpeed` | The fastest a script can switch a pin through the registers, timed against native code (see [GPIO switching speed](#gpio-switching-speed)). |
| `SwitchCase` | `switch`/`case`/`default`: a state machine, shared cases, fall-through, `break` vs `continue`. |
| `StringFunctions` | `char *` in and out of functions, `%s`. |
| `MemoryAllocator` | `alloc()`/`free()`/`writeInt()`/`readInt()`. |
| `SafeMode` | `safe_mode`: an out-of-range write stops the script and reports the array and index. |

---

# Known limitations

- **No multi-script scheduler**: no `scriptRuntime`, no `sync()`, no pre/post kill. Each script's own task can be paused, restarted and killed (see [Running scripts in the background](#running-scripts-in-the-background)).
- **`import`** (v1's `import rand`...) **doesn't work yet.** Declare the function `external` and bind a host function instead.
- **No built-in `hsv()` or led support**: bind them yourself (see [CRGB and the hue function](#crgb-and-the-hue-function)).

---

# Conclusion

It all started with led animations. I wanted to try a new effect on my panel without going through the IDE and re-flashing the esp32 every time, and that is still what ESPLiveScript does best: the Mandelbrot, the plasma or the bouncing balls of this page run at close to the speed of precompiled code.

But along the way the language outgrew the leds. Once you have structs, arrays, recursion, `switch`, strings and interrupts, you are not writing an animation any more, you are writing a program. The best proof is **Tetris** 🎮: the whole game is one script. The board, the seven tetrominoes, rotation, collisions, clearing the full lines, the falling speed and the buttons are all script code, and the leds are only where it shows.

This is its game logic (from v1's `sc_examples`, compiled by v2's test suite; tidied a little, and it runs as is on v2):

```c
#define WIDTH 10
#define HEIGHT 20
uint8_t board[HEIGHT, WIDTH];
uint8_t currentShape[4, 4];
int currentX = 0;
int currentY = 0;

bool checkCollision(int dx, int dy)
{
   for (int y = 0; y < 4; y++)
      for (int x = 0; x < 4; x++)
         if (currentShape[y, x] > 0)
         {
            int newX = currentX + x + dx;
            int newY = currentY + y + dy;
            if (newX < 0 || newX >= WIDTH || newY >= HEIGHT || board[newY, newX])
               return true;
         }
   return false;
}

void clearFullLines()
{
   for (int y = 0; y < HEIGHT; y++)
   {
      bool full = true;
      for (int x = 0; x < WIDTH; x++)
         if (board[y, x] == 0)
         {
            full = false;
            break;
         }
      if (full)
      {
         for (int ty = y; ty > 0; ty--)
            for (int x = 0; x < WIDTH; x++)
               board[ty, x] = board[ty - 1, x];
         for (int x = 0; x < WIDTH; x++)
            board[0, x] = 0;
      }
   }
}
```

and the controls are three buttons, each a GPIO interrupt calling back into the running script (see [Pointer to the executable and interrupts](#pointer-to-the-executable-and-interrupts)):

```c
pinInterrupt(_handle_, "left", 23);
pinInterrupt(_handle_, "right", 2);
pinInterrupt(_handle_, "rotate", 15);
```

v1 was my first try at "can I build a language for this". v2 is the answer to "can I trust it", and more and more to "what else can I build with it". The goal hasn't changed: write code live on an esp32, without an IDE in the loop. It just doesn't have to be a led animation any more.

Issues and feature requests are welcome, same as v1.

As always enjoy and have fun.

<div align="center">

**⚡ Happy scripting! ⚡**

</div>
