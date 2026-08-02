# QEMU-based execution verification

The host test suite in `test/host/` proves the compiler produces
*plausible* Xtensa machine code (correct opcodes, correctly-reserved jump
table, etc.) but can't actually run it -- Xtensa bytes executed as native
ARM64/x86_64 instructions would just crash. This directory runs the real
generated code on an emulated Xtensa CPU and checks the actual runtime
result, using:

- `xtensa-esp32-elf-gcc` (Espressif's crosstool-NG build) to compile a tiny
  C wrapper that embeds the compiler's raw output bytes in IRAM and calls
  into them.
- Espressif's QEMU fork (`-machine esp32`), which emulates ESP32's actual
  memory map and peripherals -- mainline QEMU's generic `sim` xtensa
  machine boots but hangs in an interrupt storm with this crt0, since it
  doesn't implement the timer/interrupt controller ESP-IDF's startup code
  configures.

This has been run against ten representative scripts, all producing the
mathematically correct result:

1. Arithmetic + control flow (nested for/if with +=/-=): computed `-4`,
   matching both the hand-derived expected value and the host-side
   `MiniXtensa` interpreter's result.
2. An external function call (`setResult(a*6)` with `a=7`): the host C
   function received `42`, proving the full jump-table relocation scheme
   (`addInstr`'s reservation bookkeeping + `asm_execute.cpp`'s
   `decodeBinaryHeader`) works end to end, not just that it assembles.
3. Fibonacci with the input hardcoded in script text: `fib=55`.
4. Fibonacci with the input passed as a real `main(int n)` argument,
   through the argument-marshaling wrapper: `fib(10)=55`.
5. Calling a *named*, non-`main` function directly (bypassing the
   wrapper, reading its return value from the standard register): the
   same `fib(10)=55`, via the plain (non-wrapper) record.
6. Reading an *external variable* (`external int key_char;`) from a
   named, argument-free function, with the jump table slot patched at
   runtime exactly as `decodeBinaryHeader`'s case-1 handling does:
   correctly uppercases `'a'`/`'z'` to `'A'`/`'Z'` while leaving other
   characters unchanged.
7. Four pure-integer helper functions (`gcd`, `fib`, `isPrime`,
   `clampInt`) extracted by name from the *actual* compiled bytes of
   `test/host/fixtures/multi_effect_controller.sc` -- the real,
   several-hundred-line script `test/host/test_large_script.cpp`'s
   `make run-large` budget-checks, not a reimplementation -- each called
   directly (`gen_large_script.cpp`/`runner_large_script.c`) and checked
   against a hand-derived expected value (`gcd(48,18)=6`,
   `fib(12)=144`, `isPrime(29)=true`/`isPrime(28)=false`,
   `clampInt(300,0,255)=255`/`clampInt(-5,0,255)=0`). This is *not* a
   claim that the whole script runs correctly end to end -- see "Not
   verified" below for exactly what this does and doesn't cover.
8. A real-hardware-cycle-count projection for `examples/FibonacciTiming.ino`'s
   naive recursive `fib()`, for both `fib(40)` (331,160,281 calls) and
   `fib(50)` (40,730,022,147 calls -- ~123x more) (`gen_fib_timing.cpp`/
   `runner_fib_timing.c`). Both are far too many calls to actually
   simulate under QEMU's software emulation in a reasonable time -- so
   instead this reads Xtensa's CCOUNT special register (`rsr.ccount`, the
   same technique the sc_examples corpus's own millis()/elapseMillis()
   __ASM__ functions use) around calls to the *actual compiled* fib() at
   two much smaller, tractable depths (fib(25): 242,785 calls; fib(30):
   2,692,537 calls -- an 11x difference), checks the resulting cycles-
   per-call figures agree within 25% (repeated runs on both esp32 and
   esp32s3, ~15 total, mostly landed within 3-6%, occasionally spiked to
   ~11-18% -- likely periodic timer-interrupt jitter in the runtime, not
   the fib() codegen itself, since the underlying per-call cost stayed in
   a narrow 5.8-6.5 cycles/call band throughout regardless of what that
   ratio read), and projects both fib(40)'s and fib(50)'s real-hardware
   time from that -- not a guess, since every fib() call does identical
   work regardless of recursion depth. Consistently lands around
   6-6.5 cycles/call, projecting to roughly 8-9 seconds for fib(40) and
   roughly 17-19 minutes for fib(50) on real hardware at the ESP32's
   default 240 MHz. (fib(50) itself, 12,586,269,025, would also overflow
   the script's 32-bit `int` return type if actually computed -- moot
   here since fib(50) is never actually called, only projected.)

9. A global *struct array*'s element addressing, both writing a distinct
   value into each of three elements (`arr[0].count=10; arr[1].count=20;
   arr[2].count=30;`) and reading each back through a runtime-computed
   index (`int getCount(int i) { return arr[i].count; }`), called
   directly at three different `i` values (`gen_arr_index.cpp`/
   `runner_arr_index.c`): `arr[0].count=10 arr[1].count=20
   arr[2].count=30` -- three distinct, correct values, not the same
   (wrong) address three times over. Also the first case here to
   exercise an *internal* (non-external) global variable at all -- every
   other case with one (case 7's gcd/fib/isPrime/clampInt) was
   deliberately chosen to have none. That turned out to need its own
   runtime relocation step this runner doesn't get from
   `createExecutableFromBinary()` (which this scheme never actually
   calls, unlike a normal load): an internal global's `l32r @_name`
   literal is a placeholder, patched at load time the same way an
   external variable's slot is (case 6) -- except *every* header
   reservation this script has (`_handle_`/`_execaddr_`/`_sync`, each
   function's own stack-scratch slot, and the array itself) turns out to
   need the identical treatment, not just the array -- eight relocation
   records total for this script, each patched by hand in
   `runner_arr_index.c` before the compiled code ever runs, mirroring
   exactly what `asm_execute.cpp`'s `decodeBinaryHeader` does for a real
   load. Confirmed the hard way: patching only the array's own slot left
   the runner hanging with no output at all; `qemu-system-xtensa -d
   guest_errors` was what actually revealed why ("Invalid write at addr
   0x0" -- one of the *other* seven slots, still unpatched).

   This exercise also found and fixed a real bug of its own:
   `visitnode.cpp`'s indexed struct-array codegen (`_visitglobalVariableNode`)
   combined the computed, index-scaled offset into the base address via
   `addi` (a genuine immediate) instead of `add` (register-to-register) --
   passing a register *number* into `addi`'s immediate slot instead of
   using that register's actual runtime value, so the real offset
   `movi`/`mull` had just computed was silently discarded and every
   array element resolved to the same fixed, generally-wrong address
   regardless of index. Affected both `arr[i].field` reads and
   `arr[i].method()` self-pointers (the latter needed its own,
   independent fix first -- a stale `struct_name` in `parser.cpp` that
   made every method call after the first struct definition anywhere in
   a script hardcode its self-pointer as a fixed local stack offset,
   ignoring both the actual variable and any index). See
   `test/host/test_parser.cpp`'s `runStructArrayMethodSelfPointerTest`
   for the host-side regression test checking the generated instruction
   text directly.
10. `ScriptExecutable::execute()` calling a script's top-level
    initialization (`@__footer` -- most commonly a global struct array's
    element constructors) before the requested function, matching
    upstream ESPLiveScript's own `execute()` -- and the new
    `executeOnly()` correctly *not* doing so (`gen_footer_check.cpp`/
    `runner_footer_check.c`). A script-side `poison()` writes a sentinel
    (-1) directly into a global struct's field, bypassing its
    constructor entirely; `footer` then `getCount()` (matching
    `execute()`'s order) must read back 99 (the constructor's real
    value, overwriting the sentinel), while `getCount()` alone (matching
    `executeOnly()`'s order, footer skipped) must still read back -1.
    Both passed. Since `callXtensaDirect()` (`asm_execute.cpp`) is a
    no-op on a non-Xtensa host, `ScriptExecutable::execute()` itself
    can't be meaningfully exercised from a host-side generator the way
    every other case's generator works -- this instead calls the exact
    same underlying primitive (`callFunction()`, by address, via
    `callx8`) `execute()`/`executeOnly()` are themselves thin wrappers
    over, in the same order the fixed implementation calls them, so
    what's verified is the identical runtime behavior on real Xtensa
    hardware semantics. (Cross-compiling the whole parser/codegen
    pipeline for real Xtensa, to call `ScriptExecutable` from *inside* a
    bare-metal runner and test the wrapper class itself end to end, was
    attempted first and hit unrelated build-configuration errors --
    `NodeToken` members not resolving under the cross-compiler's exact
    flags -- a separate problem from what this case exists to verify.)
    Also the second case here (after case 9) to exercise an internal
    global variable, and so the second to need the same by-hand
    relocation-record patching `runner_arr_index.c` does -- see case 9's
    writeup for the full explanation.

This exercise also found and fixed a real bug: `addInstr()` was silently
dropping `.bytes` reservation lines instead of reserving 4 bytes of
instruction-stream space for them, so the jump table was never reserved
and every `l32r`/`callExt` pointed at the wrong address. See
`src/asm_parser.cpp`'s `addInstr()` for the fix and
`test/host/test_parser.cpp`'s "generates executable binary: external
function call" test for the host-side regression test.

**Not verified**: actual physical ESP32 hardware (no device available in
this environment), scripts using structs or more than two functions at
once, and -- found while trying to add a case for arguments.h's
Arguments class (mixed int/float arguments) -- anything involving a
*float* arithmetic or comparison instruction (e.g. `olt.s`, which `if (b
> 1.5)` compiles to). A runner executing so much as a single float
comparison hangs under this QEMU fork with no output at all, even after
explicitly enabling the FP coprocessor via `wsr.cpenable` (which real
ESP-IDF startup code does during boot, and this runner otherwise never
reaches, being bare-metal); the identical int-only comparison in an
otherwise-identical runner works correctly. Not root-caused further --
possibly the FP coprocessor isn't emulated by this QEMU fork at all, or
needs more than CPENABLE. This means neither float-argument-passing nor
any script using float comparisons (e.g. examples/BouncingBalls.ino,
separately blocked by the `__div` stdlib-injection gap noted there) can
currently be QEMU-verified -- only host-structurally-verified, same as
structs/multi-function scripts above.

Case 7 above is a narrower verification than it might look like: it does
*not* mean `multi_effect_controller.sc` runs correctly end to end under
QEMU. That script has ~40 functions, two struct types with constructors/
methods, and float-heavy render paths (particle physics, `sin`/`hypot` in
the plasma effect) -- squarely in the two "not verified" categories just
above (structs/many-functions, and float instructions specifically hang
this QEMU fork). Running its `while(true)` main loop would also need ten
different `external`/auto-declared host functions (`hsv`, `show`, `rand`,
`delay`, ...) each bridged or jump-table-patched by hand in a bare-metal
runner, which wasn't attempted. What case 7 *does* prove: the four
functions it calls happen to be pure integer arithmetic/recursion with no
external-call or global-variable dependencies, so they're callable
directly with zero bridging (same scheme as cases 3-5), and running the
*actual* compiled bytes of the real fixture (not a reimplementation)
confirms those specific bytes execute correctly on real Xtensa hardware
semantics.

## ESP32-S3

The same eight cases were also re-run against QEMU's `esp32s3` machine
(via `xtensa-esp32s3-elf-gcc`, `TARGET_MACHINE=esp32s3 ./run.sh`) and
produced byte-for-byte identical results to the plain ESP32 run above.
This is expected, not a coincidence to double-check further: the compiler
only ever emits the base Xtensa integer/windowed-register ISA (`call8`,
`entry`, `retw.n`, `l32r`, `blt`/`bge`(`u`), etc.) common to both the LX6
core (ESP32) and LX7 core (ESP32-S3) -- none of ESP32-S3's LX7-specific
additions (e.g. its SIMD/vector extensions) are used or needed anywhere
in codegen. The float-instruction QEMU hang documented above reproduces
identically on `esp32s3` too (same underlying QEMU fork), so the same
"not verified" carve-out applies to both targets equally.

## Setup

```sh
# Xtensa GCC toolchain (~100MB) -- same one the ESP32 Arduino core uses.
curl -L -o /tmp/xtensa-toolchain.tar.gz \
  https://github.com/espressif/crosstool-NG/releases/download/esp-12.2.0_20230208/xtensa-esp32-elf-12.2.0_20230208-aarch64-apple-darwin.tar.gz
mkdir -p /tmp/xtensa-toolchain && tar xzf /tmp/xtensa-toolchain.tar.gz -C /tmp/xtensa-toolchain

# Espressif's QEMU fork (~4MB) -- mainline QEMU's xtensa "sim" machine
# does not work for this (see above). Check
# https://github.com/espressif/qemu/releases/latest for the current tag
# and pick the aarch64-apple-darwin (or your platform's) xtensa-softmmu
# asset.
curl -L -o /tmp/qemu-esp.tar.xz \
  https://github.com/espressif/qemu/releases/download/esp-develop-9.2.2-20260417/qemu-xtensa-softmmu-esp_develop_9.2.2_20260417-aarch64-apple-darwin.tar.xz
mkdir -p /tmp/qemu-esp && tar xJf /tmp/qemu-esp.tar.xz -C /tmp/qemu-esp
brew install libgcrypt   # runtime dependency of the prebuilt binary, macOS only
```

## Run

```sh
XTENSA_GCC=/tmp/xtensa-toolchain/xtensa-esp32-elf/bin/xtensa-esp32-elf-gcc \
QEMU=/tmp/qemu-esp/qemu/bin/qemu-system-xtensa \
./run.sh
```

To run the same suite against ESP32-S3 instead, point `XTENSA_GCC` at an
`xtensa-esp32s3-elf-gcc` and set `TARGET_MACHINE=esp32s3` (the QEMU fork
above already supports both `-machine esp32` and `-machine esp32s3`, no
separate download needed):

```sh
TARGET_MACHINE=esp32s3 \
XTENSA_GCC=/tmp/xtensa-toolchain/xtensa-esp32s3-elf/bin/xtensa-esp32s3-elf-gcc \
QEMU=/tmp/qemu-esp/qemu/bin/qemu-system-xtensa \
./run.sh
```

`run.sh` builds the two dumper programs against the real compiler
sources, generates each script's machine code as a C byte array, compiles
a QEMU-target runner around it with the real Xtensa toolchain, boots it
under QEMU, and checks the printed result against the expected value.
