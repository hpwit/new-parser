# asmparser2 under plain ESP-IDF

Everything under `examples/` at the repo root is an Arduino sketch (`.ino`,
built via the Arduino IDE/`arduino-cli`, depending on `library.properties`).
This folder is the same set of demonstrations rewritten as native ESP-IDF
projects -- no Arduino core involved at all.

That split is possible because `src/` was never actually Arduino-specific
to begin with: `script_executable.h`'s `executeAsTask()` and
`asm_execute.cpp`'s exec-memory allocation already talk to FreeRTOS,
`esp_heap_caps.h`, and `xtensa/hal.h` directly (see their own comments),
guarded by `#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)`. Arduino's
own core defines that `ESP32` macro; a plain ESP-IDF project doesn't, so
`components/asmparser2/CMakeLists.txt` below defines it explicitly --
correct unconditionally here, since this library only ever targets Xtensa
ESP32/S2/S3 in the first place (the assembler encodes real Xtensa
instructions).

Every project in this folder was built end-to-end (`idf.py build`, target
`esp32`) against a real ESP-IDF v5.4-dev tree as part of writing it -- not
just written to look right. `keyboard_callback`/`execute_as_task` also
build against `esp32s2`/`esp32s3` unchanged (same `ESP32` guard, no
per-chip code in any example).

## Layout

```
exampleESPIDF/
  components/
    asmparser2/CMakeLists.txt   -- wraps ../../../src as one IDF component
  simple_script/                -- one self-contained idf.py project per example
  language_basics/
  script_printf/
  structs_and_host_bindings/
  call_script_function/
  factorial/
  fibonacci_timing/
  print_binary_hex/
  keyboard_callback/
  execute_as_task/
  two_scripts/
  save_script_binary/
  load_script_binary/
```

Each example is a full, independent ESP-IDF project (its own
`CMakeLists.txt` + `main/`) rather than one project with many source
files, matching how ESP-IDF's own `examples/` tree is organized --
`idf.py build`/`flash`/`monitor` inside any one of these subfolders works
on its own, without touching the others. Every one of them points
`EXTRA_COMPONENT_DIRS` at the shared `components/` folder so they all
build against the exact same `asmparser2` component wrapping the real
`../../src` sources -- nothing is copied, so editing the library and
rebuilding any example picks the change up immediately.

## Building any example

```sh
. $IDF_PATH/export.sh        # once per shell
cd exampleESPIDF/simple_script   # or any other example directory
idf.py set-target esp32          # esp32 / esp32s2 / esp32s3 all work
idf.py build
idf.py -p /dev/tty.usbserial-XXXX flash monitor
```

`build/`, `sdkconfig`, and `sdkconfig.old` are gitignored (see
`.gitignore` in this folder) -- `idf.py build` regenerates them.

## What each example demonstrates

| Example | Ported from | What it shows |
|---|---|---|
| `simple_script` | `SimpleScript.ino` | `parseScript()`/`ScriptExecutable` -- the batteries-included API |
| `language_basics` | `LanguageBasics.ino` | The manual `parse -> createBinary -> createExecutableFromBinary` pipeline, plus AST/generated-assembly dumps |
| `script_printf` | `ScriptPrintf.ino` | The built-in `printf()`/`printfln()` -- no binding needed |
| `structs_and_host_bindings` | `StructsAndHostBindings.ino` | Structs with member functions; `bindFunction()`/`bindVariable()` |
| `call_script_function` | `CallScriptFunction.ino` | Calling a *named* script function (not just `main()`) and using its return value |
| `factorial` | `Factorial.ino` | `execute()`'s `Arguments` overload, called repeatedly with different arguments |
| `fibonacci_timing` | `FibonacciTiming.ino` | Timing `fib(40)` on-device with `esp_timer_get_time()` |
| `print_binary_hex` | `PrintBinaryHex.ino` | `printBinaryHex()`/`printHex()` -- inspecting a compiled script's raw bytes |
| `keyboard_callback` | `KeyboardCallback.ino` | An `external` variable the script reads, re-entering a script function on demand |
| `execute_as_task` | `ExecuteAsTask.ino` | `ScriptExecutable::executeAsTask()` -- running a script's `main()` on its own FreeRTOS task |
| `two_scripts` | `TwoScripts.ino` | `ScriptExecutable::free()` -- compiling/running two different scripts sequentially |
| `save_script_binary` + `load_script_binary` | `SaveScriptBinary.ino` + `LoadScriptBinary.ino` | `parseScriptToBinary()`/`createExecutableFromBuffer()` -- compile once, save to flash, load and run from a completely separate program later |

`BouncingBalls.ino` and `MultiEffectController.ino` (the two LED-matrix
scripts) aren't ported here -- nothing about them is Arduino-specific
(neither uses FastLED or any other Arduino-only library; `leds[]` is a
plain host-simulated pixel buffer in both), so porting them is
straightforward if you need them, just not done as part of this set.

## Notable adaptations (not just a mechanical `setup()`/`loop()` swap)

- **No `setup()`/`loop()`.** Plain ESP-IDF doesn't call any function
  repeatedly the way Arduino's `loop()` does -- `app_main()` either
  returns after doing its one-shot work (`simple_script`, `factorial`,
  ...) or contains its own `while (true)` (`keyboard_callback`,
  `execute_as_task`), whichever the original `.ino`'s `loop()` needed.
- **No `Serial.begin()`.** ESP-IDF's `printf()` already goes to the
  console UART by default.
- **`keyboard_callback`**: Arduino's `Serial.available()`/`Serial.read()`
  don't exist under plain ESP-IDF. The equivalent is `stdin` (the same
  console UART) put into non-blocking mode with
  `fcntl(fileno(stdin), F_SETFL, O_NONBLOCK)`, then polled with
  `fgetc()`/`EOF` each pass through the loop -- the same pattern
  ESP-IDF's own console examples use.
- **`execute_as_task`**: `delay(ms)` (Arduino) becomes
  `vTaskDelay(pdMS_TO_TICKS(ms))` (FreeRTOS directly).
- **`fibonacci_timing`**: `micros()` (Arduino) becomes
  `esp_timer_get_time()` (ESP-IDF's own microsecond-resolution monotonic
  clock -- the same one `asm_execute.cpp`'s own opt-in
  `ASM_PARSER_DEBUG` timing already uses).
- **`save_script_binary`/`load_script_binary`**: Arduino's `LittleFS`
  library becomes ESP-IDF's built-in `esp_spiffs.h`
  (`esp_vfs_spiffs_register()`), mounted at `/spiffs`; once mounted,
  plain POSIX `fopen()`/`fwrite()`/`fread()`/`fclose()` work exactly like
  `LittleFS`'s `File` API did. This needs a SPIFFS partition, which the
  default single-factory-app partition table doesn't have -- both
  projects carry their own `partitions.csv` (a `storage` SPIFFS partition
  alongside `factory`) and `sdkconfig.defaults`
  (`CONFIG_PARTITION_TABLE_CUSTOM_FILENAME`, plus a 4MB flash size sized
  for `factory` + `storage` to actually fit). Flash `save_script_binary`
  first, then `load_script_binary` *without* erasing flash in between --
  exactly like the `.ino` originals, and for the same reason: the second
  project reads what the first one wrote to the same SPIFFS partition.
  Both projects' `partitions.csv`/`sdkconfig.defaults` are identical so
  the partition layout lines up on the same device across both flashes.

## The `-Wno-error=format`/`-Wno-error=misleading-indentation` in `components/asmparser2/CMakeLists.txt`

`src/`'s `printf()`-family calls were written against Arduino-ESP32's
newlib, where `uint32_t` is `unsigned int`. Plain ESP-IDF's newlib for
this same toolchain has it as `long unsigned int` instead -- identical 32
bits, but a distinct type as far as `-Wformat` is concerned, and
ESP-IDF's default build treats that mismatch (plus one unrelated
`-Wmisleading-indentation` warning in `binding.cpp`) as an error. Neither
is a real bug, and "fixing" `src/`'s format strings would be a change to
the library itself, not something an examples folder should do as a side
effect -- so it's scoped off for just this one component instead. Every
example's own `main.cpp` stays warning-clean under ESP-IDF's default
flags with no suppression: values printed with `%d`/`%u` are cast to
`(int)`/`(unsigned)` at the call site instead (see e.g.
`call_script_function/main/main.cpp`), since `int32_t`/`uint32_t` hit the
exact same mismatch there.
