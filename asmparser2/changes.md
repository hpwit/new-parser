# ⚡ ESPLiveScript 2: changes

What changed recently, newest first. The [README](README.md) documents the library as it is now; this page is what is new and how much it brings.

## 🧩 Structures, the whole way

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

## 🗂️ A script scheduler, like v1's `scriptRuntime`

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

## 🚨 Compile errors you can show

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

## ⚡ Much faster code

The compiler now keeps loop constants and array addresses in registers, moves work out of loops, compiles `a && b` as short-circuit branches like C, and only saves float registers around a call when it has to. Measured on an esp32 at 240MHz:

|Test|Before|Now|Native C++|
|:----|----:|----:|----:|
|Mandelbrot `float`|61.7ms|**41.9ms**|33.5ms|
|Mandelbrot `ffloat`|49.3ms|**31.5ms**|30.0ms (fixed point)|
|Game of Life 128x128, per generation|13.7ms|**7.13ms**|5.95ms|
|60 bouncing balls `ffloat`|7.04ms|**6.86ms**|7.02ms (fixed point)|

Scripts now run at 1 to 1.3 times the time of gcc-compiled code, and the same pictures come out, checksum for checksum. → [Performance](README.md#performance)

## 🧹 No more memory lost at each compile

Every successful compile used to lose about 300 bytes. Now compiling again and again keeps the free heap flat, and a large script needs about 20KB less heap while it compiles.

## 🐛 More bugs found and fixed

The differential fuzzer (random programs run through the compiler and as plain C) found three more miscompiles (in four failing programs), all fixed and kept as regression tests; a new frame-by-frame test covers float values kept across calls. → [Testing](README.md#testing)

## 📚 New examples

[KeyboardTest](examples/KeyboardTest) (a USB keyboard driving a script, → [walkthrough](README.md#a-usb-keyboard-driving-a-script)), [StructArguments](examples/StructArguments), [HostStructs](examples/HostStructs), [LibraryStruct](examples/LibraryStruct), [SchedulerDemo](examples/SchedulerDemo), [CompileErrors](examples/CompileErrors), [RainbowCompare](examples/RainbowCompare), [BouncingBallsCompare](examples/BouncingBallsCompare), [DirectGPIOSpeed](examples/DirectGPIOSpeed), and a [general purpose sample](README.md#more-than-a-led-language) that has nothing to do with leds.
