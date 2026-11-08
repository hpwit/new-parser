# ⚡ ESPLiveScript 2: changes

What changed, newest first. The [README](README.md) documents the library as it is now; this page is what is new and how much it brings.

## Unreleased

### 📄 Variables filled from JSON, as in v1

```c
json dp.value as int g2;
json "name" as char label[16];
json "speed" as int speed = 50;
```

```cpp
#include <ArduinoJson.h>
exec.execute("main", "{\"dp\":{\"value\":6},\"name\":\"first run\"}");
```

v1's `json` declarations and `execute(name, json)` work again, with v1's unquoted paths, plus `executeOnly(name, json)` and `updateJson(json)`. The support comes on by itself when the sketch includes `<ArduinoJson.h>` (6 or 7). Compared with v1, each variable is written at its own size (a `uint8_t` used to get 4 bytes, overwriting its neighbours), a string is cut to its array and ended, a JSON array fills an array, `ffloat` works, a quoted path can index an array (`"list.1"`), and a key the document lacks leaves its variable alone. Saved binaries record each variable's size; older ones still load. → [Filling variables from JSON](README.md#filling-variables-from-json), [JsonInput](examples/JsonInput)

### 🔄 New settings for a running script

A script running as a task sees the values `updateJson()` writes at its next read of each variable. [JsonLiveSettings](examples/JsonLiveSettings) shows how to apply several settings at once, between two frames, with a function the script calls at the top of its loop. → [Changing the settings of a running script](README.md#changing-the-settings-of-a-running-script)

### 🐛 printfln with many values

Found while writing that example, both fixed:
- the 7th value and those after it never reached a called function: their stores were removed by the optimizer as never read (`printfln` with 8 values printed garbage for the last two);
- a string passed as the 4th or 5th value after the format arrived as one byte of its address, and `%s` crashed the board.

- a script function whose name contains `Args` (`withArgs()`) was taken for a printf-like one and matched any function starting the same way (`withJson()`: *Function already declared*).

The other scripts compile to exactly the same code as before.

### 🕹️ v1's display-icon script runs

`sc_examples/dsiplayicon.sc`, which v1 itself rejected, compiles and runs: a `uint8_t` array now goes to a `char *` parameter as in C (tried when nothing matches exactly), and a `CRGB` array to a `CRGB *` parameter (its call signature used to read `void *`). The new [DisplayIcon](examples/DisplayIcon) example runs it; its first 100 frames, from the emulated ESP32, are in [dsiplayicon.gif](examples/DisplayIcon/dsiplayicon.gif), and a real esp32 computes the same frames (same checksums). "Function not found" messages are no longer cut short on long signatures.

### 📖 Arguments, JSON variables or bound variables?

A new README section compares the three ways of giving values to a script: where each value lives, when a change reaches the script, what the script's writes do, and what each costs, measured on an esp32 (a call with 3 arguments 6.1 µs, with a 3-key JSON 112 µs, a bound variable nothing to pass). It also explains what happens when your C++ code changes a bound variable while the script runs (seen at the script's next read; declare it `volatile`). → [Arguments, JSON variables or bound variables?](README.md#arguments-json-variables-or-bound-variables)

### 🐛 Unsized arrays

An array bound or declared without a size (`bindVariable("CRGB", "leds", "[]", leds)`, `external CRGB leds[];`) was taken for a pointer and dereferenced when indexed, since pointers are, and crashed at the first `leds[i] = ...`. It is an array again. The README's `bindVariable()` reference now says how an array and a pointer are each bound.

### 🧭 Pointer bindings with the script's dimensions

`bindVariable("CRGB *", "leds", NULL, &ptr)`, the pointer form, now works with scripts that declare `external CRGB leds[96, 128];`: they read the pointer each time, then use their dimensions, so `leds[y, x]` and `leds[i]` both go where `ptr` points (the script's array declaration used to win, and it wrote over the pointer and the memory after it). Indexing a single value (`leds[i]` on `external CRGB leds;`) is now an error instead of writing that value whatever `i` was, and `leds[y, x]` without dimensions says to declare them.

### 📖 Binding an array or a pointer

A README section explains, with one example run on an esp32, how an array (`"CRGB"`, `"[]"`, its own address) and a pointer (`"CRGB *"`, the pointer variable's address) are bound, what the script does with each, and why binding an array as `"CRGB *"` crashes since 2.0.0. And `leds[3].red` on a `CRGB` (not supported) now says *Member does not exist* instead of an assertion that rebooted the board. → [Binding an array or a pointer](README.md#binding-an-array-or-a-pointer)

## 2.0.0 (2026-11-07)

The first tagged release of ESPLiveScript 2. Until now the library said 1.3.0, a number it inherited and never changed.

### 🏎️ Constant indexes cost nothing

A constant index is now part of the load or store instruction itself: `arr[3]`, `q[1]` through a pointer, `parts[2].h` and `p[2].x` through a struct pointer each compile to one instruction (`l32i a2,a6,12`) instead of computing `index × size` at run time. And a structure passed by value no longer copies its padding bytes. Measured on an esp32 at 240MHz, 20000 calls each ([the bench](README.md#constant-indexes-and-structure-copies)):

|Code|Before|2.0.0| |
|:----|----:|----:|:----|
|struct fields through a pointer, constant indexes|6.20ms|**3.10ms**|2.0× faster|
|array cells with constant indexes|8.29ms|**5.36ms**|1.55× faster|
|calls passing a structure by value|2.52ms|**2.27ms**|10% faster|

The benchmarks of the [Performance](README.md#performance) section compile to exactly the same code as before (they use none of this) and run at the same speed, to the microsecond.

### 🖨️ `%f` in a script's `printf`

`printfln("x = %.2f", x)` now prints the float. A float passed as the first value after the format arrived as nothing at all, and the floats after it were read by C's `%f` as a double, so neither printed right. Widths, precisions, `%e` and `%g` work too.

### 🔎 "Function not found" says why

```
error   : Function not found: setTag(P *, number); there is setTag(P *, char)
error   : Function not found: blink(number); no function has that name
error   : Function not found: make() can't give a structure: functions can't return one yet
```

The message shows what the call asked for and the functions of that name that exist, with their parameter types, in `showCompileError()` and `lastCompileError()` alike. → [Calling a function the wrong way](README.md#calling-a-function-the-wrong-way)

### ⌨️ A USB keyboard driving a script

[KeyboardTest](examples/KeyboardTest), confirmed on an ESP32-S3 with a real keyboard: every key runs the script's `keyboard()` function, reading the keyboard library's own `key_return` structure through `BIND_STRUCT`. The README walks through it, including the few lines the keyboard library needs. → [A USB keyboard driving a script](README.md#a-usb-keyboard-driving-a-script)

### 🧪 Tests in minutes, not hours

QEMU doesn't stop by itself when a test program ends, and the fuzzer and the effects check waited out their whole timeout every time (20 s per fuzzed program, 300 s per effect). They now stop it as soon as the program is done: 40 fuzzed programs in 12 s instead of over 100 s, the whole effects check in 14 s instead of about 5 minutes. The `fib()` timing check runs with QEMU's `-icount`, so it no longer fails when the computer is busy.

### 📦 Release

Version 2.0.0, tagged `v2.0.0`. The library sits at the root of the repository, and its `library.properties` follows the Arduino Library Manager rules (an old scratch sketch moved from `examples/` to `extras/`).

### 🧩 Structures, the whole way

Structures now behave like C's, in the script and across the border with your sketch:

```c
void step(ball *b)            // by pointer: b->x, b[i].x
{
   b->x = b->x + b->dx;
}

int distance2(ball b, int x)  // by value: the function gets its own copy
{
   b.x = b.x - x;
   return b.x * b.x;
}

step(&balls[2]);              // &variable, &array[i], &variable.field
```

- **Function arguments**: by value or by pointer, with `->`, `&` and `*`.
- **C memory layout**: fields are aligned exactly as the ESP32 compiler aligns them, so a script and your sketch read the same bytes.
- **`bindStruct()`**: define a structure once in your sketch and every script knows it. Bind variables or arrays of that type and the script works directly on your sketch's data.
- **`BIND_STRUCT()`**: the same for a structure from a library's header (the USB keyboard's `key_return`, the C library's `struct tm`): its definition is made from the C type and checked against C's layout.
- **Your sketch can call a script function with a pointer to its own structure**, e.g. a keyboard handler `void keyboard(key_event *k)`.
- Pointer fixes on the way: a pointer passed as the first argument (or declared as a local) was overwritten by `p[i] = v`, and reading `*p` gave the wrong value.

Every case is checked against the same code compiled by GCC on an emulated ESP32. → [Structures](README.md#structures)

### 🗂️ A script scheduler, like v1's `scriptRuntime`

Run several scripts at the same time, each on its own FreeRTOS task, and manage them by name:

```cpp
scriptScheduler.add("fire", fireScript);   // compile and register
scriptScheduler.run("fire");               // main() on its own task
scriptScheduler.pause("fire");             // ... restart(), kill(), remove()
scriptScheduler.list();
```

```
 #  name             state      CPU%   stack   code   data core prio  function
 1  busy             running    82.1    7296    176     32    1    1  main
 2  blink            running     0.0    7336     64     24  any    1  main
    core 0 load 0.0%   core 1 load 83.7%
```

`list()` shows what every script is doing and **how much CPU it uses**, measured by FreeRTOS itself, with each core's load. It is FreeRTOS all the way down (tasks, suspend/resume/delete, a mutex, run-time statistics) and uses no STL. → [The script scheduler](README.md#the-script-scheduler), [SchedulerDemo](examples/SchedulerDemo)

### 🚨 Compile errors you can show

```cpp
ScriptExecutable exec = parseScript(script);
if (exec.isExeExists())
   exec.execute("main");
else
   showCompileError();
```

```
error   : Expecting ;
line 5  :    a = a + 1;
             ^
```

`showCompileError()` prints the error with the line and a `^` where it was found; `lastCompileError()` gives you the message, line and position to put on a display or a web page. → [Checking for errors](README.md#checking-for-errors), [CompileErrors](examples/CompileErrors)

### ⚡ Much faster code

The compiler now keeps loop constants and array addresses in registers, moves work out of loops, compiles `a && b` as short-circuit branches like C, and only saves float registers around a call when it has to. Measured on an esp32 at 240MHz:

|Test|Before|Now|Native C++|
|:----|----:|----:|----:|
|Mandelbrot `float`|61.7ms|**41.9ms**|33.5ms|
|Mandelbrot `ffloat`|49.3ms|**31.5ms**|30.0ms (fixed point)|
|Game of Life 128x128, per generation|13.7ms|**7.13ms**|5.95ms|
|60 bouncing balls `ffloat`|7.04ms|**6.86ms**|7.02ms (fixed point)|

Scripts now run at 1 to 1.3 times the time of gcc-compiled code, and the same pictures come out, checksum for checksum. → [Performance](README.md#performance)

### 🧹 No more memory lost at each compile

Every successful compile used to lose about 300 bytes. Now compiling again and again keeps the free heap flat, and a large script needs about 20KB less heap while it compiles.

### 🐛 More bugs found and fixed

The differential fuzzer (random programs run through the compiler and as plain C) found three more miscompiles (in four failing programs), all fixed and kept as regression tests; a new frame-by-frame test covers float values kept across calls. → [Testing](README.md#testing)

### 📚 New examples

[KeyboardTest](examples/KeyboardTest) (a USB keyboard driving a script, → [walkthrough](README.md#a-usb-keyboard-driving-a-script)), [StructArguments](examples/StructArguments), [HostStructs](examples/HostStructs), [LibraryStruct](examples/LibraryStruct), [SchedulerDemo](examples/SchedulerDemo), [CompileErrors](examples/CompileErrors), [RainbowCompare](examples/RainbowCompare), [BouncingBallsCompare](examples/BouncingBallsCompare), [DirectGPIOSpeed](examples/DirectGPIOSpeed), and a [general purpose sample](README.md#more-than-a-led-language) that has nothing to do with leds.
