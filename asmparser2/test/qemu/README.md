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

This has been run against six representative scripts, all producing the
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

`run.sh` builds the two dumper programs against the real compiler
sources, generates each script's machine code as a C byte array, compiles
a QEMU-target runner around it with the real Xtensa toolchain, boots it
under QEMU, and checks the printed result against the expected value.
