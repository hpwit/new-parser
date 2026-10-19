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

`printf`/`printfln` need no setup at all -- they're built into the compiler, not something you bind (see [Talking to your own code](#interaction-with-pre-compiled-functions) for functions that do need binding). The same is true of `alloc`/`free`/`writeInt`/`readInt`, a simple dynamic-memory design covering this DSL's lack of pointer-dereference syntax -- see [MemoryAllocator](examples/MemoryAllocator) and the built-ins list below.

**NB: `execute()` doesn't only work on `main` -- any function declared in the script can be called by name, and (unlike v1) it hands you back its real return value if you ask for one. See [the next section](#the-function-you-call-can-have-input-parameters).**

## Checking for errors

`ScriptExecutable::isExeExists()` is `true` if parsing, assembling, and loading all succeeded and the script has at least one callable function. Check it before calling anything else. On failure, `parseScript()` already printed why -- the parse error's exact line/position for a syntax error, or an assembler/loader error message otherwise -- so there's no separate error object to inspect the way v1's `Executable::error` worked.

## Freeing an executable

`ScriptExecutable` already frees the compiled binary automatically when it goes out of scope, so most of the time there's nothing to call. `exec.free()` (matching v1's own `free()`) exists for the case that isn't scope-shaped: you're done with the *compiled script* before you're done with the *variable* -- e.g. compiling a second script right after the first, and wanting the first one's memory back now, not whenever the enclosing function eventually returns:

```cpp
ScriptExecutable exec1 = parseScript(script1);
if (exec1.isExeExists())
{
   exec1.execute("main");
}
exec1.free(); // release script1's memory now

ScriptExecutable exec2 = parseScript(script2);
if (exec2.isExeExists())
{
   exec2.execute("main");
}
exec2.free();
```

**NB: `ScriptExecutable` still can't be copied or reassigned (only constructed once, directly, from `parseScript()`) -- see its class comment in `script_executable.h`. So "reusing" one for a second script means calling `free()` on the existing object, then compiling a *second*, separately-named `ScriptExecutable` for the next script, not assigning a fresh `parseScript()` result back into the first one, the way the example above does.**

`free()` is safe to call more than once, and safe to just let the destructor handle instead if scope-based cleanup already does what you want -- calling it explicitly is purely an option for reclaiming memory sooner. See [TwoScripts](examples/TwoScripts) for the complete sketch.

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

Put `safe_mode` (the same keyword as v1, no `#`) anywhere in the script and every write to an array declared with explicit dimensions gets a bounds check:

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

When an index is out of bounds, the script stops before the write happens:

- the error is reported, by default as one line on the console:
  `Execution error: index 10 is out of bounds for array 'array' (size 10)`
- the whole script is abandoned, not just the function that failed: control goes straight back to the host call that entered it, and `execute()` returns `false`;
- the details stay available on the executable until the next call:

```cpp
ScriptExecutable exec = parseScript(script);
if (!exec.execute("main") && exec.hasRuntimeError())
{
   const runtime_error_struct &e = exec.runtimeError();
   // e.array == "array", e.size == 10, e.index == 10
}
```

To report errors your own way (a display, a log, a web page), install a handler before running the script; it is called instead of the console message, just before the script is abandoned:

```cpp
#include "runtime_functions.h"

void onBoundsError(const runtime_error_struct *e)
{
   Serial.printf("%s[%d] out of range (size %u)\n", e->array, (int)e->index, (unsigned)e->size);
}

onScriptRuntimeError = onBoundsError;
```

What is checked:

- writes to global and `external` arrays declared with explicit dimensions, e.g. `int buf[10];`, `external CRGB leds[96, 128];`;
- a negative index, which fails like any other;
- for a multi-dimensional array, the flattened index against the total element count, so `leds[96, 0]` fails but an out-of-range column that still lands inside the array does not.

What is not checked: reads, plain pointers (`uint8_t *p`) and arrays declared without a size (`external uint8_t raw[];`), since there is no known bound for those.

`safe_mode` applies to the script it appears in only. Another script compiled afterwards is unchecked unless it says `safe_mode` too.

**NB: each checked write costs three extra instructions (load the bound, compare, branch), so turn it on while you are hunting an indexing bug rather than by default.** See [SafeMode](examples/SafeMode) for a complete sketch.

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
ffloat    : 32-bit fixed point, written like a float -- see below
CRGB
CRGBW
```

## Fixed point: `ffloat`

`ffloat` is a number with 16 integer bits and 16 fraction bits -- about ±32768, in steps of 1/65536 (0.000015) -- that you write exactly like a float. The compiler turns the arithmetic into integer instructions:

```c
ffloat m = 2.3 / 4 + 2;     // one constant: 2.575, computed at compile time
ffloat k = m * 0.5;         // a fixed-point multiply, no float instruction
if (k > 1.25)               // an integer comparison against 1.25 * 65536
   k = k / 3;
int i = m;                  // 2 (toward zero, as from a float)
```

- Literals and all-constant subexpressions become fixed-point constants at compile time (`2.3 / 4 + 2` is the single value 168755).
- `+`, `-`, `%`, the comparisons, `++`/`--` (by 1.0) and `abs` are plain integer instructions.
- `*` takes the 64-bit product and shifts it back by 16 (`mulsh`/`mull`, then `ssai`/`src`); `/` calls a small fixed-point division routine added to the script only if it divides (truncated toward zero; dividing by zero gives the largest value of the result's sign).
- Conversions: an `int` meeting an `ffloat` is shifted in; an `ffloat` assigned to an `int` truncates toward zero; to and from `float` they convert with `float.s`/`trunc.s`, keeping 15 of the 16 fraction bits.
- An expression is computed as `ffloat` when that is its type -- an `ffloat` variable assigned to, an `ffloat` argument or return value -- or when it holds an `ffloat` value and no `float` variable or call. As with every type in this language, the type of the result decides how the arithmetic is done: `ffloat r = 7 / 2;` is 3.5, and `int k = m * 2;` converts `m` to an int first.
- Results outside ±32768 wrap around, like an int overflowing.
- `printfln` passes an `ffloat` as its raw 32-bit value; print `(int)(x * 100)` or convert it to a `float` first.

See [MandelbrotFfloat](examples/MandelbrotFfloat): the float Mandelbrot with `float` replaced by `ffloat`, next to [MandelbrotFixedPoint](examples/MandelbrotFixedPoint), the same picture with the scaling done by hand. ffloat's picture differs from the float one on 107 of 12,288 pixels, the hand-written 12-bit one on 341.

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
};

ball Balls[max_nb_balls];
...
Balls[i].updateBall();
```

`varname d;` is equivalent to `varname d = varname();`; a constructor that takes arguments works the same way you'd expect: `varname d2 = varname(23);`.

**NB 1: you can have arrays of structs -- `new_type arr[10];`.**

**NB 2: a function has to be declared before it's called, same as v1 -- no forward references (yet).**

**NB 3: the closing `}` of a struct definition needs a trailing `;`, same as C/C++ -- `struct new_type {...};`, not `struct new_type {...}`.**

**NB 3: fields inside a struct still need to go from largest to smallest (`float`/`uint32_t`/`int`, then `s_int`/`uint16_t`, then `uint8_t`/`CRGB`/`CRGBW`) -- same memory-alignment reason as v1. `examples/BouncingBalls`'s own `ball` struct above is written in exactly this order for that reason.**

# Saving executables

Same idea as v1 -- compile once, write the machine code out, load and run it later without the source, possibly from a completely different sketch.

```cpp
uint8_t *serialized = parseScriptToBinary(script, &size);
```

is the one-call version -- parses, assembles, and flattens the result into one self-contained buffer, without loading or running it. Same `script_executable.h` header as `parseScript()`, and the same error handling: `NULL` back (with `size` set to `0`) and the specific parse/assembler error already printed, on any failure.

```cpp
// compiling sketch -- never runs the script itself
uint32_t size = 0;
uint8_t *serialized = parseScriptToBinary(script, &size);
if (serialized != NULL)
{
   // ... write `serialized` (size bytes) to LittleFS/SD ...
   free(serialized);
}
```

```cpp
// a *different* sketch, later -- doesn't see the source at all
bindVariable(...);   // same external names this binary was compiled against
bindFunction(...);
// ... read the saved bytes back ...
ScriptExecutable exec = createExecutableFromBuffer(buf, size);
if (exec.isExeExists())
{
   int32_t result = 0;
   exec.execute("someFunction", &result);
}
```

`createExecutableFromBuffer()` is `parseScriptToBinary()`'s load-side counterpart -- the one-call version of `deserializeBinary()` + `createExecutableFromBinary()` + `freeBinary()`. Same deal as before: register `bindVariable()`/`bindFunction()` for every `external` name the saved script uses *first* (that's what actually resolves them), and check `isExeExists()` before using the result (the specific deserialize/loader error is already printed on failure). It hands back a `ScriptExecutable`, same as `parseScript()` -- no manual `freeExecutable()` needed, and `execute()`/`executeOnly()` work exactly the same way.

See `examples/SaveScriptBinary`/`examples/LoadScriptBinary` for the full working pair.

**NB: `parseScriptToBinary()` is the one place `ScriptExecutable` doesn't apply on the compiling end -- it hands back bytes, not an executable (the whole point is deferring loading/running to later, possibly elsewhere). The loading end, `createExecutableFromBuffer()`, hands back a real `ScriptExecutable` just like `parseScript()` does. If you need lower-level access instead -- e.g. to print the AST or generated assembly before serializing, or to hold the raw `executable` yourself -- the individual `Parser`/`createBinary()`/`serializeBinary()`/`deserializeBinary()`/`createExecutableFromBinary()` steps both convenience functions call are all still available; see `examples/LanguageBasics` for every step spelled out.**

## Binded functions

Same rule as v1: bound functions/variables travel with neither the source nor the saved binary. Whatever sketch loads the binary back needs to call `bindFunction()`/`bindVariable()` for every `external` name the script uses, exactly as the original compiling sketch did -- the relocation header matches them up by *name*, so as long as both sketches agree on the names, the two don't need to be the same sketch, or even know about each other.

# What you can do with the language

Like any normal language:
- loops (`while`, `for`)
- `break`, `continue`
- testing: `if`/`else`, the ternary `cond ? a : b`
- `switch`/`case`/`default`, as in C: the value is evaluated once, execution jumps to the matching `case` (else to `default`, else past the end) and runs on through the following cases until a `break`. `break` leaves the switch; `continue` still goes to the enclosing loop. Case values are integer constants (a number, negative, hex, or a `#define`d constant); the value can be any integer expression, not a float.

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
     int t = millis() / 100;   // a braced body can declare its own variables
     drawPlasma(t);
  }
  }
  ```
- blocks on their own, `{ ... }`, with their own local variables
- `++`/`--` for integers and pointers
- `+=`, `-=`, `/=`, `*=`
- pointers
- strings as `char *`: functions returning one -- a script function or a bound host function (`external char *name(int i);`) -- `char *` parameters, and any `char *` (a string literal, variable, `char` array or a call's result) passed as an argument, e.g. to `printfln`'s `%s` or to a host function. See [StringFunctions](examples/StringFunctions):

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

  The string itself is not copied: a returned literal lives in the script's data for as long as the script is loaded. Indexing a `char *` local (`s[0]`) is not supported yet.
- `^` for power (not XOR -- `x^2` is `x` squared)
- `>>` and `<<`
- explicit casts: `(int)(expr)`, `(float)(expr)` -- the parens around `expr` are required, `(int)x` alone is a parse error. int<->float conversion elsewhere is automatic.
- `&&`/`||`, and the `and`/`or` keyword spellings of the same thing
- built-ins: `printf`, `printfln` (real varargs, always available, no binding needed)
- built-ins for dynamic memory: `alloc(int size) -> uint32_t`, `free(uint32_t p)`, `writeInt(uint32_t p, int offset, int value)`, `readInt(uint32_t p, int offset) -> int` -- also always available, no binding needed. The DSL has no pointer-dereference syntax of its own, so a script-held `uint32_t` returned by `alloc()` is just a real heap address, read/written through these four accessors rather than `*(int*)(buf+offset)`. See [MemoryAllocator](examples/MemoryAllocator).

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

Runs one script function on its own FreeRTOS task instead of blocking the caller -- ESP32 only, see `examples/ExecuteAsTask`. The task can then be controlled, as with v1's `suspend()`/`restart()`/`kill()`:

```cpp
exec.pause();    // running -> paused: the task stops where it is
exec.restart();  // paused -> running: carries on from where it stopped
exec.kill();     // running or paused -> stopped: the task is deleted

exec.state();    // ScriptState::notStarted, running, paused or stopped
exec.isRunning();
exec.isPaused();
```

Each control returns `false` and does nothing when it doesn't apply (pausing a paused script, restarting a running one...). `stopped` also covers a function that returned on its own. `executeAsTask()` on a script whose task is still running or paused kills that task first and starts a fresh one; destroying the `ScriptExecutable` (or calling `free()`) kills its task too.

The task is stopped wherever it happens to be -- possibly inside a host function the script called. Paused there, it keeps holding whatever that function holds (`Serial`, `printf`'s lock, a mutex of yours) until `restart()`; killed there, it never releases it. If that matters, give the script a host function to call in its loop that checks a flag and returns or blocks at a point you choose.

What v1 has on top of this and v2 doesn't: a registry of several concurrently-running scripts addressable by name (`scriptRuntime`), and `sync()` for coordinating several scripts' output so they don't tear each other's frames. If your project needs several coordinated scripts running at once, that's still v1 territory for now. See [Known limitations](#known-limitations).

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

`_handle_` is a variable declared in every script (however it is compiled) holding the address of the `ScriptExecutable` running this script (or, for a script loaded through the raw `parse -> createBinary -> createExecutableFromBinary` pipeline with no `ScriptExecutable` wrapper, the address of the `executable` itself), so registering an interrupt callback doesn't need anything script-specific beyond the target function's name:

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

(v1 called this variable `_execaddr_` and gave it a small scheduler-registry index rather than a real address -- see `asm_execute.cpp`'s `fillHandleWord()` for what v2 does instead, now that `_execaddr_` itself has been dropped. A script that still declares `uint32_t _handle_;` itself compiles unchanged.)

`pinInterrupt` itself is just another `bindFunction()`-registered host function, real interrupt setup happens on the C++ side (`gpio_isr_handler_add()` etc.) -- see `examples/PinInterrupt` for a complete one. Its handler calls back with `callFunction((executable *)handle, name, NULL, 0, NULL)`: use that (or `ScriptExecutable::executeOnly()`), not `execute()`, which re-runs the script's global initializers first and would reset its globals on every interrupt.

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
| `TwoScripts` | Compiling and running two different scripts one after another, freeing the first with `exec.free()` before compiling the second. |
| `FibonacciTiming` | Timing a script's own execution with `micros()`. |
| `BouncingBalls` | Structs with methods/constructors, an array of structs, several bound host calls. |
| `StructsAndHostBindings` | Structs + `bindFunction()`/`bindVariable()`, more minimal. |
| `KeyboardCallback` | An `external` variable read by the script, re-entering a loaded script by name from a host event handler. |
| `LanguageBasics` | Every pipeline step spelled out individually -- printing the AST and generated assembly. |
| `SaveScriptBinary` / `LoadScriptBinary` | Compiling once, saving to flash, loading and running from a separate sketch. |
| `PrintBinaryHex` | Hex-dumping a compiled script's instruction bytes and relocation header. |
| `PrintFibonacciAssembly` | Printing the generated Xtensa assembly for a recursive `fib()` before assembling/loading it, then running `fib(10)`. |
| `MultiEffectController` | A real, several-hundred-line multi-effect script actually compiled and run, every host binding real. |
| `GameOfLife` | Conway's Game of Life on a 2D `external CRGB leds[h, w]` matrix -- `hsv()`-coloured generations, `CRGB(0,0,0)` for dead cells, a small self-contained automaton. |
| `ProceduralEffects` | Five separately compiled procedural effects for a 128x96 matrix -- a table-driven integer plasma, a fire simulation, an animated Voronoi pattern, a six-fold mandala and spinning gradient squares -- run in turn from `loop()`. [Frames of each](examples/ProceduralEffects/README.md). |
| `MandelbrotFfloat` | The float Mandelbrot with `float` replaced by `ffloat`: the compiler does the fixed-point arithmetic. |
| `MandelbrotFixedPoint` | The Mandelbrot set on a 128x96 matrix computed with integers only -- the classic float script rewritten in 12-bit fixed-point arithmetic, with the conversion rules explained. |
| `ExecuteAsTask` | A script's `main()` on its own FreeRTOS task, so `loop()` keeps running concurrently, and `pause()`/`restart()`/`kill()`/`state()` on that task. ESP32-only. |
| `PinInterrupt` | A GPIO interrupt calling back into the running script: `pinInterrupt(_handle_, "increase", 23)`. ESP32-only. |
| `DirectGPIO` | Setting up and driving pins from a script by writing the GPIO and IO_MUX registers directly, bound as variables; one sketch for ESP32 and ESP32-S3. |
| `SwitchCase` | `switch`/`case`/`break`/`default`: a state machine, cases sharing a body, a deliberate fall-through, and `break` vs `continue` inside a loop. |
| `StringFunctions` | `char *` in and out of functions: script and host functions returning strings, `char *` parameters, `%s`. |
| `MemoryAllocator` | `alloc()`/`free()`/`writeInt()`/`readInt()`, the built-in dynamic-memory design covering this DSL's lack of pointer-dereference syntax. |
| `SafeMode` | `safe_mode` bounds checking: an out-of-range write stops the script, reports which array and index, and `execute()` returns false. |

# Known limitations

Real, current gaps -- not aspirational TODOs:

- **No multi-program task scheduler** (each script's own task can be paused, restarted and killed, but there is no registry of scripts and no `sync()`) -- see [Running scripts in the background](#running-scripts-in-the-background).
- **`import <stdlib-function>`** (e.g. `import rand`, v1's way of splicing in a small standard-library implementation at tokenize time) **doesn't work yet.** Declare the function `external` and bind a real host implementation instead.
- **No built-in `CRGB`/`hsv()`/LED-panel support** -- always via `bindFunction()`/`bindVariable()`, see [CRGB and the hue function](#crgb-and-the-hue-function).

## Calling `main()` vs. calling other functions

One internal difference worth knowing if a script uses `sync()`: the lower-level `runExecutable()`/`runExecutableWithArgs()` call `main()` specifically through an argument-marshaling wrapper and zero two data-region words `sync()` depends on -- and never surface a return value. `ScriptExecutable::execute()` calls everything (`main()` included) directly by address instead, which is what makes a return value available, but skips that wrapper's zeroing step. Unless a script calls `sync()`, `execute("main")` is equivalent and strictly more useful (real return value) -- prefer it, which is what every example in this README already does.

# Conclusion

v1 was my first real attempt at "can I build a language for this." v2 is what happens when the answer to that turns into "now can I actually trust it." Same goal as always -- fast, live-editable led animations on an esp32 without an IDE in the loop -- just built this time so a change to the compiler either provably still works or provably doesn't, instead of "seemed fine on my desk."

Issues and feature requests welcome, same as v1. As always, enjoy and have fun.
