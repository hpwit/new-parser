# ⚡ ESPLiveScript 2: changes

What changed, newest first. The [README](README.md) documents the library as it is now; this page is what is new and how much it brings.

## Unreleased

### ✅ `checkScript()`: the language only, no code

`checkScript(script)` reads and parses a script without generating, optimizing or assembling any code: true when its language is right, else `lastCompileError()` says where and why, as after a compile. It is what an editor's "check" key wants: on `MultiEffectController` it needs about 160 KB of heap at its peak where a compile needs 260 KB, and it takes the parsing's share of a compile's time. Everything the parser used is freed before it returns, the script right or wrong; and a failed parse no longer leaves part of the parser's node pool allocated (16 to 25 KB), after `checkScript()` or `parseScript()`. Host test in `run-large`.

### 🏷️ Named parameters in `bindFunction()`

`bindFunction()`'s argument types may name the parameters, as in C: `"int x,int y"`, `"uint8_t *buf, int len"`, or only some (`"int ,int h"`). The names are dropped and the types read as before, so old bindings keep working and new ones can carry names for an editor's hints. Before, a name broke the binding ("Expecting )"), and a name containing a type's letters (`print`) could even change the type it was read as. Host test `runNamedBindingParametersTest`; every example compiles to the same bytes.

## 2.1.1 (2026-11-28)

Packaging only, the code is the same as 2.1.0. An example's local build (`examples/AssemblyGPIO/build`, 24 MB of `.elf`, `.map` and `.bin`) had been committed and would have been installed with the library: it is gone, and every example's `build/` folder is now ignored. The library is under the MIT licence (`LICENSE`).

## 2.1.0 (2026-11-28)

Faster code and a faster compiler. On the ESP32-S3, byte-array loops and 16-bit fills are vectorized with the chip's PIE instructions, and `__ASM__` functions can use 55 of them; zero-overhead loops, smaller instructions and better register use speed up all code. The compiler is 2 to 3 times faster than in 2.0.0 on a computer (on an esp32, `MultiEffectController` compiles in 6.9 s instead of 10.6 s), and on an ESP32-S3 with PSRAM large scripts compile in half the time again (Wickwire 332 s to 131 s). New in the language: `for (x in array)`, `int a[] = {...}`, pointer arithmetic in elements, `?:` and comparisons as values. New in the API: `setCompileProgress()`, `setScriptCodeMemory()` and `SCRIPT_CODE_AUTO`, `createExecutableFromStream()`, `setExecutableAllocator()`. Many fixes (`>>` on ints, casts, numbers above 65535, a call's result moved out of its loop, errors in nested blocks...). And new examples, up to a Game Boy emulator that runs at the real machine's speed. Every example compiles to the same or better code; the details follow.

### 🎮 A Game Boy emulator as a script

[matouch/GameBoy](examples/matouch/GameBoy): an emulator of the first Game Boy in 1342 lines of script, processor, cartridge chips MBC1, MBC3 and MBC5, timer, interrupts, picture, buttons. It lists the `.gb` files on the SD card or the FFat partition (and on SolarOS in `/flash` and `/sdcard`) and runs the one chosen at the real Game Boy's speed on the ESP32-S3: 59.7 frames a second with Super Mario Land and Link's Awakening, about 49 drawn. It passes Blargg's `cpu_instrs` and `instr_timing` and draws `dmg-acid2` exactly; the board and a computer reach the same state frame after frame. 14 KB of code, compiled on the board in 15 s. → [Game Boy](README_GAMES.md#game-boy-an-emulator-of-the-first-game-boy), [Built for speed](README.md#-built-for-speed)

### ⏱️ Large scripts compile twice as fast on an ESP32-S3

Three of the compiler's lists were emptied from the front, one item at a time, and each removal moved everything after it: the tokens read ahead (the list grows through a script to about 6000 tokens, every operator adding one more), the assembly lines as the assembler took them, and the blank lines the optimizer removes. For GT 32 that was 2.6 GB of memory moved during one compile. A computer does it in milliseconds; in PSRAM, where an S3 compiler keeps its working memory, it took over a minute. The tokens read now wait behind an index and go in one move, the assembler frees each line in place, and the blank lines are removed in a single pass. Measured on the board, same code to the byte:

|Script|Before|Now|
|:----|----:|----:|
|Tetris|9.4s|7.1s|
|PrismBreak|26.0s|15.4s|
|NeonRacer|28.4s|18.1s|
|MazeChase|30.2s|17.3s|
|XenoWing|41.6s|32.4s|
|Driftwing|94.3s|52.2s|
|Lumenfall|124.0s|61.2s|
|GT 32 (CitySandbox)|185.1s|81.7s|
|Wickwire|331.9s|130.7s|

On an esp32 without PSRAM, where the same moves are in internal RAM, `MultiEffectController` compiles in 6.9 s instead of 8.0 s, and short scripts take the same time. Every example, game and fuzz regression script compiles to the same bytes as before; the compiler's heap peak is unchanged (host test `run-large`).

### 🧮 ESP32-S3 vector instructions (PIE) in `__ASM__` functions

The assembler knows 55 of the S3's `ee.` instructions: 128-bit loads and stores (`ee.vld.128.ip`, `.xp`, 64-bit halves, broadcasts), saturating add and subtract, min, max, compares and multiply on 8-, 16- and 32-bit lanes, bitwise operations and lane moves, on the registers `q0` to `q7`. Their encodings were taken from GNU as (`xtensa-esp32s3-elf-as`), bit by bit, and 440 of them are checked against it in the host tests; a script using them runs as expected on QEMU's esp32s3. For another chip they are an error.

### 🚀 Byte-array loops vectorized on the ESP32-S3

A loop like a game of life's rule, `for (int i = 1; i < 127; i++) { uint8_t n = hs[a, i] + hs[c, i] + hs[b, i]; copy[j, i] = (n == 3) | ((n == 4) & copy[j, i]); }`, is compiled to PIE code on the S3: 16 elements per pass, loads at any address (`ee.ld.128.usar.ip` + `ee.src.q`), 16-bit lanes so that the result is the scalar one exactly (the S3's vector adds saturate), and element-by-element code for the start, until the stored address is a multiple of 16, and for the end. The shape it takes: numbers as bounds, `uint8_t` arrays at `i` plus a number, `uint8_t` temporaries, one store, `+ - & |` and comparisons, the array written read only where it is written. 5 to 7 times fewer instructions per cell in the game of life; other loops compile as before (of the examples, only ProceduralEffects' row clear is taken). Checked on QEMU's esp32s3 against C: the game of life over 20 generations and 140 random loops, 127 of them vectorized (`test/qemu_s3`). On the board (ESP32-S3, matouch, `GameOfLifeVec`, 400x240 cells): a generation takes 7.2 ms instead of 34.5 ms (800x480: 30.3 instead of 140.2), with the same checksums over 400 generations; with the drawing, 18.9 ms a frame against 41.8 ms for the original `GameOfLife` script.

`__ASM__` functions are now assembled exactly as written: the optimizer, which reads the code's text, used to rewrite them too (it renamed `a5` to `a15` in `AssemblyFunctions`' loop) and would have reordered `ee.` instructions it can't follow.

### 🔁 `for (x in array)` and `int a[] = {...}`

```c
int notes[] = {60, 62, 64, 65};
for (n in notes) printfln("%d", n);
for (int n : notes) { }
```

An array given its values can leave its size out: before, `int a[] = {...}` silently made an array of one element and wrote the other values over the next globals. Too many values for a declared size (`int a[2] = {1, 2, 3};`) is now an error. Arrays of structures take one `{...}` per element (`voice voices[] = {{60, 0.5}, {64, 1.0}};`), in the C layout with its padding, a missing field being 0. `ffloat` arrays given values now hold them; before, `1.5` was read as an integer.

`for (x in a)` and the C++ form `for (int x : a)` go through an array of one dimension. They compile to the same code as the counted `for` they stand for, zero-overhead loop included. Over structures the variable is the element itself, as `for (auto &v : a)` in C++: `v.gain = 0;` changes the array. Example: `ForInArrays`.

Strings in arrays and structures: `char *colors[] = {"red", "green"};` is an array of strings. Before, any `T *a[N]` was an array of `T`, so `colors[1] = "x"` kept one byte of the address. A structure can have a `char *name;` field (it was refused), given its string in the braces too: `{{"apple", 3}}`. An element or a field goes to a function taking a `char *` as one, and `for (c in colors)` declares `c` a `char *`.

Two fixes found on the way: one character of a string passed to a function (`emit(s[0])`) found no function, and storing into a `char *` local deep in a large frame gave an `addi` past 127 that the assembler refused.

Hex numbers in initializers were stored as 0: `uint8_t font[] = {0x18, 0x3C};`, a structure's `{0x100, 0xA5}` and `CRGB(0xff, 0x80, 0)` all read their values as decimal and stopped at the `x`. So did `case 0x10:`. They now read hex as expressions always did.

### 🧭 Pointers: their value, and arithmetic in elements

```c
uint16_t *gp = buf;
uint16_t *d = gp;     // d holds what gp holds
uint16_t *p = gp + 2; // &buf[2]
p++; p += 3; p = p - 1;
int n = p - gp;       // elements between them
```

- A global or bound pointer (`uint16_t *gp;`, `external uint16_t *led16;`) read as a value gave its own address: `uint16_t *d = gp;` read whatever followed `gp` in memory, and writes through `d` went there too. `gp[i]` was right.
- `p + n`, `n + p`, `p - n` and `p += n` moved by `n` bytes; they move by `n` elements, as in C (`++` and `--` already did). `p - q` between two pointers is the number of elements between them, an `int`, and can be passed as one.
- A `float *` computed from an array (`float *f = fl + 2;`) lost the addition.
- `gs = pts;` with a global structure pointer `pt *gs;` stored the address at the wrong offset: `gs[i].x` then crashed.
- A global `char *word = "pointer";` held the characters themselves, like `char word[]`: it is now a pointer to them, so `word + 3` and `word = "zz";` work.

Every example, game and SolarOS script compiles to the same code as before, except DisplayIcon (its two `char *` texts), whose frames are unchanged.

### ⚡ A faster compiler, smaller and faster code

The compiler's line store is a hash instead of lists it scanned for every line it added or removed: compiling takes 2.2 to 3.2 times less time on a computer (Wickwire 3.2 s -> 1.0 s), and `MultiEffectController` compiles on an esp32 without PSRAM in 8.1 s instead of 10.6 s. On a board with PSRAM the compiler's working memory goes there, so a compile no longer competes with the sketch for internal RAM.

The generated code:

- `add`, `mov`, small `addi`/`movi` and `l32i`/`s32i` use their 16-bit forms: 9 to 17% less code (Wickwire 75 KB -> 64 KB);
- `a[i]` with 2, 4 or 8-byte elements is one `addx2`/`addx4`/`addx8` instead of two or three instructions;
- `/` and `%` by a power of two are shifts and masks instead of the divider (signed ones still round toward zero, as in C);
- a load and the instruction using it are moved apart when an independent instruction can sit between them (the one-cycle load-use stall);
- a number the code reads from memory (`4.0`, `2.0`, a constant over 2047) is read once before a loop instead of at each iteration;
- the first instruction of a branch target after a jump no longer straddles a 4-byte word (a cycle each time the branch is taken);
- counted loops without a call inside, `for` and `while` alike, become the Xtensa zero-overhead loop (`loop`): the processor counts the iterations and jumps back by itself, so the limit is not reloaded, compared and branched on at each iteration;
- on the ESP32-S3, a script's globals are kept out of the 64 KB block of internal RAM its code runs from: a loop reading them from there lost up to two cycles an iteration, depending on the address the script happened to load at (6% on a Mandelbrot).

On an esp32 at 240 MHz, against 2.0.0: `fib(40)` 21.72 s -> 20.37 s, Game of Life 7.15 -> 6.20 ms per generation, Mandelbrot 41.9 -> 39.1 ms (`float`) and 31.5 -> 27.1 ms (`ffloat`, now faster than fixed point written in C); the balls, the rainbow and the GPIO speed are within 0.5% (their loops call the host). On the ESP32-S3: `fib(40)` -7.5%, Mandelbrot 800x480 1753 -> 1614 ms, Game of Life 800x480 204 -> 171 ms. Every frame and checksum is the same as before. Host suite, QEMU frame-exact effects and the fuzzer (49 regressions, 800 new programs, every one of them with converted loops) pass.

### ⏳ `setCompileProgress()`: how far a compile has got

```cpp
void showProgress(int percent, const char *stage, void *ctx) { Serial.printf("%3d%% %s\n", percent, stage); }
setCompileProgress(showProgress, NULL);
ScriptExecutable exec = parseScript(script);
```

The compiler calls the function as it works: a percentage from 0 to 100 that never goes back, and the stage (`reading`, `parsing`, `generating`, `optimizing`, `assembling`, `done`). The stages are weighted from their time on Valmora, Ironforge, Tetris and NeonRacer, and the long ones (parsing, two optimizer passes, the assembler) report as they go, so the number follows the clock to within a few percent; 70 to 100 calls a compile. The generated code is unchanged (every SolarOS example script compiles to the same bytes). New example [CompileProgress](examples/CompileProgress), README [Watching a long compile](README.md#watching-a-long-compile), host test.

### 🐛 Numbers above 65535 in a script

A decimal number in a script was read into 16 bits: `uint16_t cache[378880]` reserved 6784 elements, so the globals declared after it shared its memory (Valmora crashed as soon as it filled its tile cache), and `x == 65537` was compiled as a comparison with 1 (`bnei a2,1`). The number is read as 32 bits now. Host tests for both; the SolarOS example scripts compile to the same bytes as before.

### 🗺️ Valmora: an adventure in the style of the early role-playing games

[matouch/Valmora](examples/matouch/Valmora): a world map, three towns and 30 people, four torch-lit dungeons, every picture (32x32 tiles of 16 colours each out of 4096), map and line of talk in a WAD on the FFat partition, made by its `tools`. The GameLauncher runs it too (`valmora.bin`, `valmora.wad` beside it): it binds SolarOS's names (`display`, `stride`, `updateDisplay`, `enableGraphicMode`, `keyHeld`, `delay`), keeps one WAD at a time, and puts a game's code in PSRAM when internal RAM is short (`SCRIPT_CODE_AUTO`). See the README's [Valmora](README_GAMES.md#valmora-an-adventure-in-the-style-of-the-early-role-playing-games).

### 🧠 `SCRIPT_CODE_AUTO`: internal RAM, else PSRAM

`setScriptCodeMemory(SCRIPT_CODE_AUTO)` puts a script's code in executable internal RAM when a block that big is free, else in PSRAM (ESP32-S3 with PSRAM; false elsewhere). `executableCodeMemory(&exe)` says where a loaded script's code went.

### 🧠 `setScriptCodeMemory()`: a script's code in PSRAM (ESP32-S3)

`setScriptCodeMemory(SCRIPT_CODE_PSRAM)` puts the code of the scripts loaded next in PSRAM instead of executable internal RAM: written and relocated at its data address, run through the instruction bus's view of the same memory (`+0x06000000`), with ESP-IDF's `esp_cache_msync()` between the two, as ESP-IDF's ELF loader does. The code is then fetched through the instruction cache: on the matouch board, Ironforge (40.5 KB of code) runs about as fast from PSRAM as from internal RAM. It returns false, and changes nothing, on a chip that can't (the ESP32, a host). The ESP-IDF component now requires `esp_mm`. Host test `setScriptCodeMemory()`.

### 📼 `createExecutableFromStream()`: a saved binary loaded from its file

```cpp
File f = FFat.open("/game.bin");
ScriptExecutable exec = createExecutableFromStream(
    [](void *ctx, uint8_t *buf, size_t len) -> size_t { return ((File *)ctx)->read(buf, len); }, &f);
```

`createExecutableFromBuffer()` needs the whole file in memory, then copies the code and the relocation table out of it before relocating: a binary was held three times while it loaded (Ironforge: 59 KB file, 40.5 KB of code). Streaming copies the code from the file straight into its executable block, 256 bytes at a time, and relocates it there; only the relocation table and the literals after the code are read into the heap, and only while loading. On 67 example scripts the streamed load gives the same code (but for the data address it holds), globals and functions as the buffer one. Host test `loadExecutableFromStream()`, with a file cut short and one that is not a binary.

### 🧱 `setExecutableAllocator()`: the host chooses where the code goes

A script's machine code needs one contiguous block of executable internal RAM. In a busy firmware (SolarOS on the ESP32-S3-Touch-LCD-7) no free piece was as big as Ironforge's 40.5 KB, even right after a reboot, and the script could not load. `setExecutableAllocator(alloc, release)` lets the program hand out a block it set aside at start (see the README's "Where the code goes"); without it nothing changes. The load error now says which allocation failed and by how much: `could not allocate 40552 bytes of executable memory for this script (largest MALLOC_CAP_EXEC block: ... bytes)`. Host test `setExecutableAllocator()`.

### 🔁 A call's result is not moved out of its loop

```c
for (int i = 0; i < n; i++)
   N[ix(f(), 4), 1] = 1;   // wrote row 0 every time
```

The optimizer moves computations that do not change in a loop to before it. It took a value read from a register a call writes (a8..a15, the floats) as unchanged, because a call is not counted as writing anything: the row offset `ix(...) * 5` (`addx4 a5,a10,a10`, a10 being the call's result) was computed once before the loop, from whatever a10 held then. A loop with a call now keeps such lines inside. Found by the fuzzer (seed 251); regression `test/fuzz/regressions/call_result_hoisted_from_loop`. Of the examples, only two ProceduralEffects scripts change: the register allocator keeps a different constant in a register, the same size.

### ➗ `>>` keeps the sign of an int

```c
int m = -54;
int h = m >> 1;   // -27, as in C (it gave 2147483621)
```

`>>` always shifted in zeros (`srl`), even for an `int`, so a negative value became a huge positive one. It now keeps the sign (`sra`) unless the value shifted holds a `uint32_t` (`(u + 1) >> 8` still shifts in zeros, as C's conversions give). `uint8_t` and `uint16_t` values are never negative, so nothing changes for them. The examples that shift ints compile to the same size, with `sra` in place of `srl`; where they could shift a negative value the result is masked afterwards (`& 248`), or bounds-checked as a tile coordinate, so they behave as before. Regression `test/fuzz/regressions/signed_shift_right`.

### 🎯 A cast as the left operand keeps its operator

```c
int i = ((int)(x * 64.0)) & 63;   // was (int)(x * 64.0): the & was lost
```

A cast of a float expression passed the type of its operands (float) to the expression around it instead of its own, so `& | << >> %` after it were done as float operations, which dropped `&`, `|` and the shifts. The cast now gives its own type. The examples that use casts compile to smaller code with the same results (NeonRacer 336 bytes less); the others are byte-identical. Regression `test/fuzz/regressions/cast_left_operand`.

### ❓ `?:` and comparisons as values, as in v1

```c
int speed = boost > 0 ? 6 : 3;
color = hp < 25 && blink ? RED : WHITE;
ok = x > 2;                 // 1 or 0
return a < b ? a : b;
setLevel(n > 9 ? 9 : n);
```

The right side of an assignment, an initializer, a `return` value and a call argument only took arithmetic: a comparison, `&&` / `||`, or a `?:` whose condition is one, failed with `Expecting ;` (or `Expecting )`) unless put in parentheses (`(x > 2) ? 1 : 0`). They are now full expressions, as in v1 and C; `?:` keeps the lowest precedence and groups to the right. Host tests run the generated code (`r == 123169`, `r == 42`).

### 🧹 No crash after a parse error in nested blocks

A parse error deep inside `if` / `for` blocks freed part of the parse tree twice while cleaning up the failed compile (a use-after-free, then a corrupt node pool on the board). Each subtree is now freed once. Host test `runNestedErrorCleanupTest`.

### 🔁 Compiling again after an error

A compile that failed kept what it had generated so far, so compiling the corrected script failed in turn, with `label @_helper(num) is already declared` for every function the broken script had defined before its error; only restarting the board helped. A failed `parseScript()` / `parseScriptToBinary()` now frees everything a good compile frees (the parser's state and the generated assembly), right after copying the error into `lastCompileError()`, which is the place to read it from. Host test `runRecompileAfterErrorTest`; the suite runs clean under AddressSanitizer. → [Showing the error your own way](README.md#showing-the-error-your-own-way)
### 🧱 Structures as parameters of your C++ functions

```cpp
bindStruct(mode_definition);
bindFunction((char *)"void", (char *)"setMode", (char *)"mode", (void *)setMode);   // void setMode(mode *m)
```

`bindFunction()` used to turn every parameter type it did not know into a number, so a function taking a structure could not be called (`Function not found: setMode(mode); there is setMode(number)`). A structure bound with `bindStruct()` is now recognised, by value (`"mode"`) or by pointer (`"mode *"`); the function receives its address. → [bindFunction()](README.md#bindfunction)

### 🔗 Load errors that name the missing bindings

```cpp
ScriptExecutable exec = createExecutableFromBuffer(buf, size);
if (!exec.isExeExists())
   printf("%s\n", lastLoadError());   // missing binding: led16, show16()
```

Loading a saved binary in a program that has not bound some of its names used to report only one of them, on the console. The loader now lists every missing binding, once each, functions with `()`, and the new `lastLoadError()` gives that text (or `not a valid binary: ...`, or the out-of-memory message) to a sketch that wants to show it itself; `""` after a load that worked. [LoadScriptBinary](examples/LoadScriptBinary) prints it. → [Saving executables](README.md#saving-executables)

### 🎛️ Parameters for a script run as a task

```c
void countTask(int times, float pause) { ... }
```

```cpp
Arguments args;
args.add(5);
args.add(0.5f);
exec.executeAsTask("countTask", &args);
```

`executeAsTask()` can now give the function its parameters, as `execute(name, &args)` does: an `Arguments` list (ints and floats keep their type; a string, structure or array goes by address, to a `char *` or pointer parameter) or a raw `int32_t[]`. Up to 6 values, copied before the call returns; the existing `executeAsTask(name, ...)` is unchanged. [TaskPointerParameters](examples/TaskPointerParameters) passes a string, an array and a structure by address, [TaskParameters](examples/TaskParameters) starts functions from commands typed in the Serial Monitor, and [matouch/BouncingBalls](examples/matouch/BouncingBalls) takes the number of balls as `main(int n)`'s parameter. → [Parameters for a task](README.md#parameters-for-a-task)

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

### 🖥️ Examples for the ESP32-S3-Touch-LCD-7

[RainbowScript, BouncingBalls, GameOfLife, Mandelbrot and Tetris](examples/matouch) for the ESP32-S3-Touch-LCD-7, a 7-inch 800x480 RGB touch screen: each script draws straight into the screen's frame buffer. Run on the board, Tetris played with a USB keyboard. Tetris is a whole game in about 400 lines of script; the README shows frames from it and what it uses from the language. → [Tetris: a whole game in a script](README_GAMES.md#tetris-a-whole-game-in-a-script) [CreateTetrisBinary](examples/matouch/CreateTetrisBinary) compiles it and saves the binary to the microSD card, [ExecuteTetrisBinary](examples/matouch/ExecuteTetrisBinary) plays it from there: loaded in 3 ms instead of compiled in 9.9 s, run on the board. → [On the ESP32-S3-Touch-LCD-7](README_GAMES.md#on-the-esp32-s3-touch-lcd-7)

### 🕹️ MazeChase

[MazeChase](examples/matouch/MazeChase), a maze game for the ESP32-S3-Touch-LCD-7 in about 1100 lines of script: an orb, four drones with their own strategies, power cells, a bonus apple, score, lives, levels, and a demo that plays itself until Space is pressed. The README shows frames from it. → [MazeChase: a maze game with four hunters](README_GAMES.md#mazechase-a-maze-game-with-four-hunters)

### 🏁 NeonRacer and the GameLauncher

[NeonRacer](examples/matouch/NeonRacer), an anti-gravity race on a neon track drawn in perspective (curves, hills, boost pads, three rivals, 3 laps) with 3D ships (lit polygons, rolling in the turns), in about 1050 lines of script. [GameLauncher](examples/matouch/GameLauncher) lists the compiled games on the FFat partition and runs the one you pick, with Escape back to the menu; its `data` folder has Tetris, MazeChase and NeonRacer as binaries. The README says how to make the partition's image and write it. → [NeonRacer: a race in perspective](README_GAMES.md#neonracer-a-race-in-perspective), [GameLauncher: games from the FFat partition](README_GAMES.md#gamelauncher-games-from-the-ffat-partition)

### 🛸 XenoWing

[XenoWing](examples/matouch/XenoWing), a horizontal shoot'em up with pixel art in flash: an organic manta ship over an alien world in five parallax layers (a ringed planet, far mountains, a forest of strange trees, a crystal ridge, plants in front), waves of bio creatures, a boss, power-ups and spore bombs, in about 1250 lines of script. The art comes from a Python tool in the example and reaches the script through bound pointers. Its binary is in the GameLauncher, which carries the art too. → [XenoWing: a shoot'em up with real art](README_GAMES.md#xenowing-a-shootem-up-with-real-art)

### 🌍 ChromaQuest

[ChromaQuest](examples/matouch/ChromaQuest), three grey worlds made from public-domain NASA photographs (the Moon with the Earth rising, Mount Sharp on Mars, Saturn): a lantern drone shoots the colour thieves and catches their red, green and blue drops, and the script gives each colour back to the photograph, mixing its grey and its colour channel by channel every frame. About 1000 lines of script; in the GameLauncher too. → [ChromaQuest: colour back to three worlds](README_GAMES.md#chromaquest-colour-back-to-three-worlds)

### 🛠️ Assembly functions, as in v1

`__ASM__ int f(int a, int b) { "entry a1,32" "add a2,a2,a3" "retw.n" }`: a function in Xtensa assembly, called from the script like any other. It worked already; it is documented now (calling convention, variables through `l32r @_name` and bound ones through `movExt @_ext_name`, labels), with two examples, [AssemblyFunctions](examples/AssemblyFunctions) and [AssemblyGPIO](examples/AssemblyGPIO), and a fuzz regression against gcc. The assembler learned `min`, `max`, `minu`, `maxu`, `nsau`, `nsa`, `clamps`, `sext`, `mul16u`, `mul16s`, `memw`, `extw`, `isync`, `nop` and special registers by name (`rsr a2,ccount`), each checked byte for byte against GNU as. Fixed on the way: a label starting with `b` in an assembly function crashed the optimizer, and an assembler error now shows the line as written instead of its number in the generated code. → [Assembly functions](README.md#assembly-functions-__asm__)

### 🎮 XenoWing: the ship moves while a key is held

A press used to set the ship going until the same key came again, so stopping at a given height was hard. The keyboard library only reported presses; with an optional release hook (`hid_keyboard_release_char`, see the README's keyboard section), the ship now moves while a direction is held and stops when it is let go, standalone and from the GameLauncher.

### 🧹 The compiler gives its memory back after each compile

After a compile, the parse tree, the tokens and their text, and the assembler's loop list stayed in the heap until the next compile: about 21 KB in some 600 small blocks after Tetris, scattered through the heap. With several compiles in a row, a script's code and data (which need one free block) could no longer find room: on the ESP32-S3, MazeChase, NeonRacer and XenoWing failed to load after Tetris had been compiled. They are freed as soon as the binary is made now (after a failed compile, the state stays until the next one: the error's position points into it), and a line the code generator dropped as a repeat is freed instead of leaked (a few hundred bytes per float-heavy script). Measured: after five Tetris compiles, the ESP32's free heap is 250.5 KB instead of 230.8 KB (QEMU esp32 and esp32-S3 alike), and the S3 compiles and loads all five matouch games in a row. The generated code is the same.

### 🐛 % in a float expression

`float f = 57 % 13;` was 57: in an expression computed as a `float`, `%` between ints gave its left value (there is no float remainder). It is the integer remainder now, then a float, as in C. `/` there still divides as floats, as before (the README says so). A new fuzz regression (`float_modulo`) checks it against gcc. Of the existing examples, only DisplayIcon used it (`sin(3.14 * (m % 100) / 50)`, which worked by luck: the sine repeats every 100).

### 🐛 == and != between unsigned values

`if (rand(400) == 0)`, `rand` returning a `uint32_t`, did not assemble: the compiler asked for `bneu`/`bequ`, which Xtensa doesn't have (equality doesn't depend on the sign). They are `bne`/`beq` now; `<` and `>=` stay unsigned. A new fuzz regression (`unsigned_equality`) checks it against gcc.

### 🧱 PrismBreak, Escape in every game, GIFs

[PrismBreak](examples/matouch/PrismBreak), a brick breaker with patterned bevelled bricks, a scrolling diamond background, capsules (wider paddle, three balls, slower ball, a life) and five layouts, in about 1030 lines of script; its binary is in the GameLauncher. In every game Escape now gives the game up (in the launcher it comes back to the menu) and the arrows work as Z Q S D. The README shows each game moving: GIFs rendered frame by frame on the emulated ESP32-S3. → [PrismBreak: a brick breaker](README_GAMES.md#prismbreak-a-brick-breaker)

### 🐛 Negative values in array initializers

`int dys[4] = {-1, 0, 1, 0};` stored 0 for the -1: each value of a global initializer was read from its own text, and `-1` is a minus sign over a number. Signs are now read, for integer and `float` arrays. Found by MazeChase, whose drones walked into walls; a new fuzz regression (`negative_initializers`) checks it against gcc.

### 🐛 Binary `&`

`if (bits & 1)` crashed the compiler (an assertion) and `x = a & 3` did not compile: a single `&` was only read as an address-of. It is now the bitwise and after an operand, as `|` already was the bitwise or. Like `|`, it binds like `*` (`a + b & 4` is `a + (b & 4)`). Found while writing Tetris; a new fuzz regression (`bitwise_and`) checks it against gcc. No script that compiled before changes.

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
