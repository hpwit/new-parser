# ESPLiveScript2

A from-scratch C++ port of [ESPLiveScript](https://github.com/hpwit/asmparser) (v1) -- a
small, C-like scripting language that compiles directly to real Xtensa
machine code and runs on an ESP32, with no interpreter overhead. You
write scripts as plain strings in your sketch (or load them from flash,
or over the network), compile them on-device in milliseconds, and call
into them like any other function.

v1's own README explains the motivation in full (interpreted scripting
languages like Lua/Gravity were too slow for real-time LED animation on
a 12,000+ pixel panel; compiling to real machine code isn't). This
document covers what v2 actually is and does, which differs from v1 in
a few real ways covered at the end.

## Why a rewrite

v2 is not a refactor of v1 -- it's an independent reimplementation of
the same idea, built around one goal v1 didn't have: the ability to
verify the compiler itself, not just trust that scripts happen to work
on a board in front of you. That shows up in a few concrete ways:

- The entire compiler (tokenizer, parser, assembler, loader) builds and
  runs as an ordinary host program, with no ESP32 hardware or Arduino
  framework involved -- see `test/host/`.
- A real Xtensa CPU emulator (QEMU, Espressif's ESP32/ESP32-S3 machine
  models) executes the *actual compiled bytes* of representative
  scripts and checks real results, not just that the compiler didn't
  crash -- see `test/qemu/`.
- Every real-world example script from v1's own `sc_examples/` corpus
  is compiled and checked against this pipeline -- see
  `test/sc_examples/`.

None of that changes what you write in a script day to day. It's why
you can trust that what's described below actually works, rather than
"works on my board."

## Quick start

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
      // main() itself, no return value used.
      exec.execute("main");

      // fact() directly, with a real argument and a real result back.
      Arguments args;
      args.add(6);
      int32_t result = 0;
      if (exec.execute("fact", &args, &result))
      {
         printf("fact(6) = %d\n", result);
      }
   }
}

void loop()
{
}
```

`parseScript()` does the whole parse -> assemble -> load pipeline in one
call and hands back a `ScriptExecutable` that owns the compiled,
loaded script. No manual cleanup needed: when `exec` goes out of scope,
the loaded executable (and every intermediate buffer the pipeline
allocated) is freed automatically.

`isExeExists()` tells you whether compiling and loading succeeded.
`execute(name, ...)` calls any function declared in the script by name
-- `main`, or any other one -- and can hand back its real return value,
here via the `Arguments` overload (more on that next).

(A script can also print directly via `printfln("i:%d", i)` -- no
`bindFunction()`/`external` declaration needed, it's always available;
see [Built-in printf / printfln](#built-in-printf--printfln) below.)

## Calling a function with arguments, and getting a value back

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
   if (!exec.isExeExists())
   {
      return;
   }

   Arguments args;
   args.add(5);
   int32_t result = 0;
   if (exec.execute("fact", &args, &result))
   {
      printf("factorial of 5 is %d\n", result);
   }
}

void loop()
{
}
```

`Arguments` (`arguments.h`) is a typed list of `int`/`float` values --
build it with `add()`, `clear()` it to reuse for another call. There's
also a raw-array overload, `execute(name, int32_t args[], nargs,
&result)`, if you'd rather not build an `Arguments` object.

Calling a function this way -- by name, not through `main()`'s own
argument-passing convention -- is what actually gets you a return
value. `main()` itself can be called the exact same way (`execute("main")`,
with or without arguments) and also returns a real value in v2; see
[Calling `main()` vs. calling other
functions](#calling-main-vs-calling-other-functions) for the one
internal difference that can matter.

See `examples/Factorial` and `examples/CallScriptFunction` for the full
working sketches this is drawn from, and `examples/FibonacciTiming` for
the same pattern used to benchmark a script's own performance with
`micros()`.

## Built-in printf / printfln

Scripts can call `printf(fmt, ...)` and `printfln(fmt, ...)` directly,
with no `bindFunction()` call and no `external` declaration needed:

```cpp
char script[] = R"EOF(
void main()
{
   int a = 5;
   printfln("i:%d 3*i:%d", a, 3 * a);
}
)EOF";
```

They're registered automatically the first time anything is compiled
(`registerBuiltinRuntimeFunctions()`, called from inside `Parser::parse()`,
see `src/runtime_functions.h`/`.cpp`) -- real, genuinely variadic host
functions (`vprintf` underneath), matching v1's own `artiPrintf`/
`artiPrintfln`. `printfln` appends a trailing `\r\n`; `printf` doesn't.

If your own code already calls `bindFunction()` for a function named
`printf`/`printfln` before parsing (e.g. to capture output somewhere
other than stdout), that binding wins -- the built-in one only fills in
a name that isn't already bound.

## Talking to your own C++ code

A script can call real functions and read/write real variables in your
sketch -- `bindFunction()`/`bindVariable()` (`binding.h`) register them
*before* parsing:

```cpp
#include "script_executable.h"
#include "binding.h"

int brightnessPercent = 100;

void scriptPrintln(int v)
{
   printf("script says: %d\n", v);
}

char script[] = R"EOF(
external void println(int v);
external int brightness;

void main()
{
   println(brightness);
}
)EOF";

void setup()
{
   Serial.begin(115200);

   bindFunction((char *)"void", (char *)"println", (char *)"int", (void *)scriptPrintln);
   bindVariable((char *)"int", (char *)"brightness", NULL, (void *)&brightnessPercent);

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

`bindFunction(returnType, name, argTypesCommaSeparated, functionPointer)`
and `bindVariable(type, name, arraySizeOrNull, variablePointer)`. The
script must still declare each bound name `external` itself (as above)
-- binding alone registers the host pointer, but the assembler needs
the explicit declaration to reserve a jump-table slot for the call; see
`examples/StructsAndHostBindings` and `examples/BouncingBalls` for
larger, working examples of this (structs with methods, external
functions, external arrays).

The library doesn't provide a built-in `CRGB`/`hsv()` -- if you're
driving LEDs, bind your own `hsv()`/`leds` the same way (see
`examples/BouncingBalls`), the same as any other host function or
array.

## Calling back into a script from an event handler

Because `execute()` re-enters the loaded script by name on demand, a
script's executable can be kept around and called into repeatedly --
e.g. once per keypress, once per sensor reading:

```cpp
#include "script_executable.h"
#include "binding.h"

char script[] = R"EOF(
external int key_char;

int keyboard()
{
   int c = key_char;
   if (c >= 97)
   {
      if (c <= 122)
      {
         c = c - 32;
      }
   }
   return c;
}

void main()
{
}
)EOF";

int hostKeyChar = 0;
ScriptExecutable *g_exec = NULL;

void setup()
{
   Serial.begin(115200);
   bindVariable((char *)"int", (char *)"key_char", NULL, (void *)&hostKeyChar);

   // `static` gives this the lifetime it needs to still be valid in
   // loop() -- see examples/KeyboardCallback for the full explanation
   // of why this specific shape (not a plain global, later assigned)
   // matters.
   static ScriptExecutable holder = parseScript(script);
   g_exec = &holder;
}

void loop()
{
   if (g_exec != NULL && g_exec->isExeExists() && Serial.available())
   {
      hostKeyChar = Serial.read();
      int32_t result = 0;
      if (g_exec->execute("keyboard", &result))
      {
         Serial.println((char)result);
      }
   }
}
```

See `examples/KeyboardCallback` for the complete sketch.

## The language

A loose, more strongly-typed C-like syntax:

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

**Types**: `int` (32-bit), `s_int` (16-bit signed), `uint8_t`,
`uint16_t`, `uint32_t`, `float`, `bool` (`true`/`false`), `char`,
`CRGB`, `CRGBW`.

**Control flow / operators**: `if`/`else`, `while`, `for`, `break`,
`continue`, the ternary `cond ? a : b`, `&&`/`||` and the `and`/`or`
keyword forms, `++`/`--`, `+=`/`-=`/`*=`/`/=`, `<<`/`>>`, `^` for
*power* (not XOR -- `x^2` is `x` squared), explicit casts written as
`(int)(expr)` (the parens around `expr` are required -- `(int)x` alone
is a parse error), implicit int<->float conversion elsewhere.

**Constants**: `#define NAME value` (the `#` is required -- a bare
`define NAME value` is not valid syntax, despite appearing in some
older example scripts).

**Arrays**, including multi-dimensional via comma indexing:

```c
int array[23];
int array2D[height, width];
int array3D[depth, height, width];

int h = array3D[2, 12, 23];
```

**Structs**, with fields, methods, and a constructor (same name as the
struct):

```c
struct ball
{
   float vx, vy, xc, yc, r;
   int color;

   void updateBall()
   {
      xc += vx;
      ...
   }

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

**Calling out to your own code**: `external` declarations, resolved at
load time against whatever you registered with `bindFunction()`/
`bindVariable()` (see above) -- e.g. `external void show();`,
`external CRGB leds[height, width];`.

**Inline assembly**, for the rare case a script needs to drop to real
Xtensa instructions directly (e.g. reading the cycle counter for a
custom `millis()`):

```c
uint32_t __baseTime[1];
__ASM__ void setTime()
{
   "entry a1,32"
   "rsr a14,234"
   "l32r a5,@___baseTime"
   "s32i a14,a5,0"
   "retw.n"
}
```

**JSON-driven variables**: `json "path.to.value" as <type> name;`
declares a script variable that gets populated from a JSON document at
runtime (see `json_binding.h` -- the structural half needs no
dependency; actually applying a JSON document needs ArduinoJson,
opted into with `-D__JSON_OPTION__`).

**Interrupts**: `pinInterrupt(_execaddr_, "function_name", pin)`, bound
the same way as any other external function -- `_execaddr_` is an
always-available variable holding a handle to the currently running
script, so registering a callback doesn't need anything script-specific
beyond the function name.

## Saving and loading compiled scripts

A compiled script's machine code can be flattened to a self-contained
byte buffer, written to flash, and loaded back later -- by a
*completely different* sketch, as long as it registers the same
external names:

```cpp
// SaveScriptBinary.ino -- compiles, never runs the script itself.
Binary bin = createBinary(&footer, &header, &content, false);
uint32_t size = 0;
uint8_t *serialized = serializeBinary(&bin, &size);
// ... write `serialized` (size bytes) to a file, e.g. via LittleFS ...
```

```cpp
// LoadScriptBinary.ino -- a separate sketch, doesn't see the source.
bindVariable(...);   // same external names, this sketch's own bindings
bindFunction(...);
// ... read the saved bytes back from a file ...
Binary bin = deserializeBinary(buf, size);
executable exe = createExecutableFromBinary(&bin);
freeBinary(&bin);
int32_t result = 0;
callFunction(&exe, "someFunction", NULL, 0, &result);
freeExecutable(&exe);
```

The relocation header stores external references by *name*, not
address, so the compiling and executing sketches only need to agree on
those names -- see `examples/SaveScriptBinary`/`examples/LoadScriptBinary`
for the full pair, and `asm_serialize.h` for `serializeBinary()`/
`deserializeBinary()`.

This is also the one case where `parseScript()`/`ScriptExecutable`
don't apply -- they hide the intermediate `Binary` (freeing it once the
executable is loaded), and saving needs to serialize that `Binary`
directly. Use the lower-level pipeline (`Parser`/`createBinary()`/
`createExecutableFromBinary()`, all still available -- see
`examples/LanguageBasics` for every individual step spelled out,
including printing the AST and generated assembly) for that case.

## Calling `main()` vs. calling other functions

Internally, `execute()`/`callFunction()` call a function directly by
address, exactly the way compiled script code calls another script
function -- which is what makes a real return value available.
`runExecutable()`/`runExecutableWithArgs()` (still available, lower-
level) call `main()` specifically through an argument-marshaling
wrapper and additionally zero two data-region words some scripts'
`sync()` depends on; that wrapper never surfaces a return value. Unless
a script calls `sync()`, calling `main()` via `execute("main")` is
equivalent and does give you its return value -- prefer it.

## Verification & testing infrastructure

- `test/host/` -- the compiler built and run as an ordinary host
  program (no ESP32 involved):
  - `make run` -- the main hand-written test suite.
  - `make check-optimized` -- the same suite built with `-Os`, matching
    Arduino's default optimization level (catches bugs `-O0` alone
    hides -- an include guard colliding with a GCC builtin macro was
    found exactly this way).
  - `make check-asan` -- the same suite under AddressSanitizer (catches
    memory bugs plain `malloc` silently tolerates -- a heap-use-after-
    free from an unsafe `vect<T>` copy was found exactly this way).
  - `make run-sc` -- compiles every real-world script from v1's own
    `sc_examples/` corpus.
  - `make run-large` -- compiles a several-hundred-line synthetic
    script and checks the resulting binary against a size budget for
    an ESP32 *without* PSRAM.
  - `make run-json` -- the ArduinoJson-dependent half of JSON binding
    (needs `ARDUINOJSON_DIR=...`).
- `test/qemu/` -- the *compiled bytes* executed for real under QEMU
  (Espressif's ESP32 and ESP32-S3 machine models), checking actual
  results: arithmetic, external calls, recursion, argument passing,
  external variables, save/load, and a real-hardware cycle-count
  projection for a naive `fib(40)`/`fib(50)`. See `test/qemu/README.md`
  for exactly what is and isn't covered this way (notably: float
  instructions hang this QEMU fork, so anything float-heavy is only
  host-structurally verified, not execution-verified).

## Examples

| Example | Demonstrates |
|---|---|
| `SimpleScript` | The quick-start pattern above. |
| `ScriptPrintf` | A script printing directly with `printf()`/`printfln()` -- no `bindFunction()`/`external` needed. |
| `Factorial` | `Arguments`, calling a function multiple times with different values. |
| `CallScriptFunction` | Calling a named function with a raw `int32_t[]` and using its return value. |
| `FibonacciTiming` | Timing a script's own execution with `micros()`. |
| `BouncingBalls` | Structs with methods/constructors, an array of structs, several `bindFunction()`-registered host calls. |
| `StructsAndHostBindings` | Structs + `bindFunction()`/`bindVariable()`, more minimal. |
| `KeyboardCallback` | An `external` variable read by the script, re-entering a loaded script by name from a host event handler. |
| `LanguageBasics` | Every pipeline step spelled out individually, printing the AST and generated assembly -- useful for understanding or debugging the compiler itself. |
| `SaveScriptBinary` / `LoadScriptBinary` | Compiling once, saving to flash, loading and running from a separate sketch. |

## Known limitations

Real, current gaps -- not aspirational TODOs:

- **No task scheduler.** v1 has a multi-task FreeRTOS scheduler
  (`executeAsTask()`, `scriptRuntime`, `suspend()`/`kill()`, task
  synchronization via `sync()`). v2 deliberately scopes this out --
  it's a single script, called directly, no concurrent-task machinery.
- **`import <stdlib-function>` (e.g. `import rand`) doesn't work.** v1
  splices in a real implementation from a small standard library at
  tokenize time; v2 has the same mechanism sketched but not wired up.
  Declare the function `external` and bind a real host implementation
  instead.
- **Float instructions can't be verified under QEMU in this
  environment** (the emulator hangs) -- doesn't affect real hardware,
  only how deeply this repo's own test suite can verify float-heavy
  scripts before you run them yourself.
- No built-in `CRGB`/`hsv()`/LED-panel support -- always via
  `bindFunction()`/`bindVariable()`, same as any other host
  integration.
