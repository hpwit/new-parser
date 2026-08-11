# Introduction

A while back I got tired of loading code onto the esp32 through an IDE every time I wanted to try a new led animation, so I wrote [ESPLiveScript](https://github.com/hpwit/asmparser) (what is now "v1" of this library): a small C-like language that compiles straight to real Xtensa machine code on the device itself, no interpreter, no re-flash.

That worked, people used it, [StarLight](https://github.com/MoonModules/StarLight) picked it up. But v1 was written the way most first compilers are written: fast, by hand, no grammar, and -- crucially -- with no real way to check that a change to the compiler hadn't quietly broken something other than the one script I happened to test it against. Every fix was "works on my board." That's fine until it isn't.

**So I rewrote it.** Not a refactor -- a from-scratch reimplementation of the same idea (same language, same approach: compile to real Xtensa machine code, run it on-device), built around one thing v1 never had: the ability to actually verify the compiler, not just trust it.

- The whole compiler (tokenizer, parser, assembler, loader) builds and runs as an ordinary program on my laptop, no ESP32 or Arduino framework involved -- see `test/host/`.
- A real Xtensa CPU emulator (QEMU, Espressif's own ESP32/ESP32-S3 machine models) runs the *actual compiled bytes* of representative scripts and checks the real results, not just "did it crash" -- see `test/qemu/`.
- Every real-world script from v1's own `sc_examples/` corpus gets compiled and checked against this same pipeline -- see `test/sc_examples/`.

None of that changes what you actually write in a script day to day -- this document is about that part. But it's why I can say something works instead of hoping it does.

**This is v2.** The language is close enough to v1 that most of what you already know still applies -- but the C++-side API you call from your sketch (`ScriptExecutable`, `bindFunction()`, `bindVariable()`, `parseScript()`) is different, some things v1 had aren't here yet (see [Known limitations](#known-limitations)), and a couple of behaviors changed on purpose. This README describes v2 as it actually is.

<!-- TOC start -->

- [Which language?](#which-language)
  * [C-like language](#c-like-language)
  * [DIY parser and compiler](#diy-parser-and-compiler)
  * [Not a development environment](#not-a-development-environment)

- [First light](#first-light)
  * [Checking for errors](#checking-for-errors)
  * [Freeing an executable](#freeing-an-executable)

- [The function you call can have input parameters](#the-function-you-call-can-have-input-parameters)

- [Interaction with pre-compiled functions](#interaction-with-pre-compiled-functions)
  * [Access to pre-compiled variables](#access-to-pre-compiled-variables)
  * [Calling pre-compiled functions](#calling-pre-compiled-functions)

- [CRGB and the hue function](#crgb-and-the-hue-function)

- [Safe mode and arrays](#safe-mode-and-arrays)

- [Variable types](#variable-types)
  * [Arrays and multidimensional arrays](#arrays-and-multidimensional-arrays)
  * [Structures](#structures)

- [Saving executables](#saving-executables)
  * [Binded functions](#binded-functions)

- [What you can do with the language](#what-you-can-do-with-the-language)
  * [Use of define](#use-of-define)

- [Running scripts in the background](#running-scripts-in-the-background)

- [Performance](#performance)

- [Advanced stuff](#advanced-stuff)
  * [Pointer to the executable, and interrupts](#pointer-to-the-executable-and-interrupts)

- [Verification & testing infrastructure](#verification--testing-infrastructure)

- [Known limitations](#known-limitations)

- [Conclusion](#conclusion)

<!-- TOC end -->

# Which language?

## C-like language

Same choice as v1: a C-like syntax, a bit closer to JavaScript, with stronger typing. A loose adaptation, not a strict C subset -- but you can write things like this:

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

Still hand-written, still no grammar-generator tool involved, still not written by a compiler specialist. What's different from v1 is that this time the whole thing is exercised by a real test suite instead of by hand -- see [Verification & testing infrastructure](#verification--testing-infrastructure).

## Not a development environment

Same as v1: this library compiles and runs scripts, it doesn't give you anywhere to write them. If you want an actual editor/terminal environment around this, [LedOS](https://github.com/hpwit/LedOS) (built for v1) is the closest thing, and [StarLight](https://github.com/MoonModules/StarLight) wraps v1 into a full web-enabled ESP32 application. Neither has been ported to v2 yet.

# First light

- Compile a script and get back a live, callable object: `ScriptExecutable exec = parseScript(script);`
- Check it actually compiled: `if (exec.isExeExists())`
- Call a function declared in it by name, e.g. `main`: `exec.execute("main");`

If you run this ([SimpleScript](examples/SimpleScript)):

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

the output is:

```
i: 0 3*i: 0
i: 1 3*i: 3
i: 2 3*i: 6
...
i:19 3*i:57
```

`printf`/`printfln` need no setup at all -- they're built into the compiler, not something you bind (see [Talking to your own code](#interaction-with-pre-compiled-functions) for functions that do need binding).

**NB: `execute()` doesn't only work on `main` -- any function declared in the script can be called by name, and (unlike v1) it hands you back its real return value if you ask for one. See [the next section](#the-function-you-call-can-have-input-parameters).**

## Checking for errors

`ScriptExecutable::isExeExists()` is `true` if parsing, assembling, and loading all succeeded and the script has at least one callable function. Check it before calling anything else. On failure, `parseScript()` already printed why -- the parse error's exact line/position for a syntax error, or an assembler/loader error message otherwise -- so there's no separate error object to inspect the way v1's `Executable::error` worked.

## Freeing an executable

Nothing to call. `ScriptExecutable` frees the compiled binary automatically when it goes out of scope -- v1's `exec.free()` doesn't exist in v2 because there's nothing left to free by the time you'd call it. This is also why `ScriptExecutable` can't be copied or reassigned (only constructed once, directly, from `parseScript()`) -- see its class comment in `script_executable.h` if you're curious why.

# The function you call can have input parameters

```cpp
Arguments args;
args.add(5);
int32_t result = 0;
exec.execute("fact", &args, &result);
```

**NB: for now, arguments are `int`/`float` only, same restriction v1 had.**

Factorial, computed in the script and read back into the sketch ([Factorial](examples/Factorial)):

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

**NB: this is the real, load-bearing difference from v1 worth calling out here -- `execute()` calls a function directly by its own address, exactly like one script function calling another, which is what makes a genuine return value available. `main()` gets no special treatment: `exec.execute("main", &result)` returns its value too, the same way. The one place this can matter is `sync()` -- see [Calling `main()` vs. calling other functions](#calling-main-vs-calling-other-functions) below if a script uses it.**

# Interaction with pre-compiled functions

The script language can't do everything the real Espressif toolchain can -- no WiFi, no I2C/SPI, no reusing FastLED or any other existing library's code -- so a script needs to be able to call out to real, pre-compiled C++ functions, and read/write real C++ variables. In v2 this is `bindFunction()`/`bindVariable()` (`binding.h`), called *before* `parseScript()`.

## Access to pre-compiled variables

```cpp
bindVariable((char *)"type", (char *)"name_in_script", arraySizeOrNull, (void *)&hostVariable);
```

The script must still declare it `external` itself -- binding registers the host pointer, the `external` declaration is what makes the assembler reserve a jump-table slot for it:

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

**NB: three different functions, all defined in the same script, called independently by name.**

## Calling pre-compiled functions

```cpp
bindFunction((char *)"returnType", (char *)"name_in_script", (char *)"argTypesCommaSeparated", (void *)hostFunctionPointer);
```

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

result:

```
from pre-compiled 1.529412
from other function
```

See `examples/StructsAndHostBindings` and `examples/BouncingBalls` for larger, real working sketches built the same way (structs with methods, several bound functions and an external array).

# CRGB and the hue function

`CRGB` is a real type in the language -- `CRGB color = CRGB(r, g, b);` works out of the box, no binding needed.

**Unlike v1, v2 doesn't ship a built-in `hsv()`/FastLED integration.** There's no `USE_FASTLED` switch. If your script wants `hsv()`, or an `leds[]` array driven by a real driver's `show()`, bind them yourself the exact same way as any other host function/variable:

```cpp
#include "script_executable.h"
#include "binding.h"
#define NUM_LEDS 256

CRGB ledsBuffer[NUM_LEDS];
CRGB scriptHsv(int hue, int sat, int val) { return CHSV(hue, sat, val); } // e.g. via FastLED
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

See `examples/BouncingBalls` for a complete, working version of exactly this pattern.

# Safe mode and arrays

Same footgun as v1: an external array with no bounds checking will happily write past its end.

```c
external uint16_t array[10];

void main()
{
   for (int i = 0; i < 200; i++)
   {
      array[i] = 200; // out of bounds past i == 9
   }
}
```

`safe_mode` (still the same keyword, no `#`) turns on a bounds check before every write to a `safe_mode`-declared array:

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

**NB: the check runs on every single write, so it costs real speed -- turn it on while debugging an array-indexing bug, not by default.**

# Variable types

```
uint8_t
char
bool      : true, false
int       : 32-bit
s_int     : 16-bit signed
uint16_t
uint32_t
float
CRGB
CRGBW
```

## Arrays and multidimensional arrays

```c
int array[23];
int array2D[height, width];
int array3D[depth, height, width];

int h = array3D[2, 12, 23];
```

## Structures

```c
struct new_type
{
   float k;
   int l;
}
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
}
```

And constructors (same name as the struct):

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
}

ball Balls[max_nb_balls];
...
Balls[i].updateBall();
```

`varname d;` is equivalent to `varname d = varname();`; a constructor that takes arguments works the same way you'd expect: `varname d2 = varname(23);`.

**NB 1: you can have arrays of structs -- `new_type arr[10];`.**

**NB 2: a function has to be declared before it's called, same as v1 -- no forward references (yet).**

**NB 3: fields inside a struct still need to go from largest to smallest (`float`/`uint32_t`/`int`, then `s_int`/`uint16_t`, then `uint8_t`/`CRGB`/`CRGBW`) -- same memory-alignment reason as v1. `examples/BouncingBalls`'s own `ball` struct above is written in exactly this order for that reason.**

# Saving executables

Same idea as v1 -- compile once, write the machine code out, load and run it later without the source, possibly from a completely different sketch. The API is different: v2's `parseScript()`/`ScriptExecutable` hide the intermediate `Binary` (freeing it once loaded), so saving needs the lower-level pipeline instead:

```cpp
// compiling sketch -- never runs the script itself
Binary bin = createBinary(&footer, &header, &content, false);
uint32_t size = 0;
uint8_t *serialized = serializeBinary(&bin, &size);
// ... write `serialized` (size bytes) to LittleFS/SD ...
```

```cpp
// a *different* sketch, later -- doesn't see the source at all
bindVariable(...);   // same external names this binary was compiled against
bindFunction(...);
// ... read the saved bytes back ...
Binary bin = deserializeBinary(buf, size);
executable exe = createExecutableFromBinary(&bin);
freeBinary(&bin);
int32_t result = 0;
callFunction(&exe, "someFunction", NULL, 0, &result);
freeExecutable(&exe);
```

See `examples/SaveScriptBinary`/`examples/LoadScriptBinary` for the full working pair.

## Binded functions

Same rule as v1: bound functions/variables travel with neither the source nor the saved binary. Whatever sketch loads the binary back needs to call `bindFunction()`/`bindVariable()` for every `external` name the script uses, exactly as the original compiling sketch did -- the relocation header matches them up by *name*, so as long as both sketches agree on the names, the two don't need to be the same sketch, or even know about each other.

# What you can do with the language

Like any normal language:
- loops (`while`, `for`)
- `break`, `continue`
- testing: `if`/`else`, the ternary `cond ? a : b`
- `++`/`--` for integers and pointers
- `+=`, `-=`, `/=`, `*=`
- pointers
- `^` for power (not XOR -- `x^2` is `x` squared)
- `>>` and `<<`
- explicit casts: `(int)(expr)`, `(float)(expr)` -- the parens around `expr` are required, `(int)x` alone is a parse error. int<->float conversion elsewhere is automatic.
- `&&`/`||`, and the `and`/`or` keyword spellings of the same thing
- built-ins: `printf`, `printfln` (real varargs, always available, no binding needed)

## Use of define

Unlike v1, the `#` is required -- a bare `define NAME value` is not valid syntax in v2:

```c
#define TOKEN 25

if (i < TOKEN)
{
   ...
}

// compiles as if you had written
if (i < 25)
{
   ...
}
```

**NB: no macros yet, same as v1 -- `#define` is a plain textual constant, not a function-like substitution.**

# Running scripts in the background

```cpp
exec.executeAsTask("function_name");
```

Runs one script function on its own FreeRTOS task instead of blocking the caller -- ESP32 only, see `examples/ExecuteAsTask`.

**This is the one place v2 is deliberately smaller than v1.** v1 has a full scheduler on top of this: `scriptRuntime`, a registry of several concurrently-running scripts addressable by name, `suspend()`/`kill()`/`restart()` with cross-task handshaking, and `sync()` for coordinating several scripts' output so they don't tear each other's frames. None of that exists in v2 yet -- `executeAsTask()` is fire-and-forget: no handle back, no way to stop it short of the script returning on its own, no registry to look other running scripts up in. If your project actually needs several independent, coordinated scripts running at once, that's still v1 territory for now. See [Known limitations](#known-limitations).

# Performance

I don't have a v2 apples-to-apples frame-rate comparison on real LED hardware yet the way v1's README does (that table was measured on my own 128x96/12,288-pixel panel; I haven't rebuilt that specific rig against v2). What I do have is real, QEMU-measured Xtensa cycle counts for a naive recursive `fib()`, which at least says something about per-call overhead:

| calls | cycles/call |
|:----|:----:|
| `fib(25)` -- 242,785 calls | ~5.3-6.5 |
| `fib(30)` -- 2,692,537 calls | ~5.3-6.5 |

(measured directly under QEMU via the Xtensa cycle-counter register, `test/qemu/gen_fib_timing.cpp`/`runner_fib_timing.c` -- the two figures agreeing across an 11x difference in call count is itself the check that this is a real, constant per-call cost and not a fluke.) Projected out, that's `fib(40)` (331,160,281 calls) in a bit over 8 real seconds at 240MHz on actual hardware.

I'd rather publish that honestly than reuse v1's old table with a different compiler underneath it. A real led-panel comparison is on the list.

# Advanced stuff

## Pointer to the executable, and interrupts

Same mechanism as v1: `_execaddr_` is an always-available variable holding a handle to the currently-running script, so registering an interrupt callback doesn't need anything script-specific beyond the target function's name:

```c
external void pinInterrupt(uint32_t exec, char *name, int pin);

int number = 0;
void increase()
{
   number++;
}

void main()
{
   pinInterrupt(_execaddr_, "increase", 23);
   while (true)
   {
      printfln("number:%d", number);
   }
}
```

`pinInterrupt` itself is just another `bindFunction()`-registered host function, real interrupt setup happens on the C++ side (`gpio_isr_handler_add()` etc.) -- see v1's README for a complete `setup_gpio_interrupt()` implementation, which ports over unchanged.

# Verification & testing infrastructure

This is the part that's actually new relative to v1, and the whole reason for the rewrite -- see [Introduction](#introduction).

- `test/host/` -- the compiler built and run as an ordinary host program, no ESP32 involved. `make run` for the main suite, `make check-optimized` (same suite at `-Os`, Arduino's default level), `make check-asan` (under AddressSanitizer), `make run-sc` (every real script from v1's own `sc_examples/`), `make run-large` (a size-budget check against a PSRAM-less ESP32).
- `test/qemu/` -- the actual compiled bytes, executed for real under Espressif's QEMU fork: arithmetic, external calls, recursion, argument passing, external variables, save/load, and the cycle-count projection quoted above. See `test/qemu/README.md` for exactly what's covered.

# Examples

| Example | Demonstrates |
| --- | --- |
| `SimpleScript` | The quick-start pattern above. |
| `ScriptPrintf` | A script printing directly with `printf()`/`printfln()`. |
| `Factorial` | `Arguments`, calling a function multiple times with different values. |
| `CallScriptFunction` | Calling a named function with a raw `int32_t[]` and using its return value. |
| `FibonacciTiming` | Timing a script's own execution with `micros()`. |
| `BouncingBalls` | Structs with methods/constructors, an array of structs, several bound host calls. |
| `StructsAndHostBindings` | Structs + `bindFunction()`/`bindVariable()`, more minimal. |
| `KeyboardCallback` | An `external` variable read by the script, re-entering a loaded script by name from a host event handler. |
| `LanguageBasics` | Every pipeline step spelled out individually -- printing the AST and generated assembly. |
| `SaveScriptBinary` / `LoadScriptBinary` | Compiling once, saving to flash, loading and running from a separate sketch. |
| `PrintBinaryHex` | Hex-dumping a compiled script's instruction bytes and relocation header. |
| `MultiEffectController` | A real, several-hundred-line multi-effect script actually compiled and run, every host binding real. |
| `ExecuteAsTask` | A script's `main()` on its own FreeRTOS task, so `loop()` keeps running concurrently. ESP32-only. |

# Known limitations

Real, current gaps -- not aspirational TODOs:

- **No multi-program task scheduler** -- see [Running scripts in the background](#running-scripts-in-the-background).
- **`import <stdlib-function>`** (e.g. `import rand`, v1's way of splicing in a small standard-library implementation at tokenize time) **doesn't work yet.** Declare the function `external` and bind a real host implementation instead.
- **No built-in `CRGB`/`hsv()`/LED-panel support** -- always via `bindFunction()`/`bindVariable()`, see [CRGB and the hue function](#crgb-and-the-hue-function).

## Calling `main()` vs. calling other functions

One internal difference worth knowing if a script uses `sync()`: the lower-level `runExecutable()`/`runExecutableWithArgs()` call `main()` specifically through an argument-marshaling wrapper and zero two data-region words `sync()` depends on -- and never surface a return value. `ScriptExecutable::execute()` calls everything (`main()` included) directly by address instead, which is what makes a return value available, but skips that wrapper's zeroing step. Unless a script calls `sync()`, `execute("main")` is equivalent and strictly more useful (real return value) -- prefer it, which is what every example in this README already does.

# Conclusion

v1 was my first real attempt at "can I build a language for this." v2 is what happens when the answer to that turns into "now can I actually trust it." Same goal as always -- fast, live-editable led animations on an esp32 without an IDE in the loop -- just built this time so a change to the compiler either provably still works or provably doesn't, instead of "seemed fine on my desk."

Issues and feature requests welcome, same as v1. As always, enjoy and have fun.
