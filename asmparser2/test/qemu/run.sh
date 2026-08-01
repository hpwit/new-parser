#!/bin/sh
# QEMU-based execution verification. See README.md for what this proves
# and how to obtain XTENSA_GCC/QEMU. Usage:
#   XTENSA_GCC=/path/to/xtensa-esp32-elf-gcc QEMU=/path/to/qemu-system-xtensa ./run.sh
set -e

: "${XTENSA_GCC:?set XTENSA_GCC to the xtensa-esp32-elf-gcc binary path}"
: "${QEMU:?set QEMU to the qemu-system-xtensa binary path}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$SCRIPT_DIR/../../src"
BUILD_DIR="$SCRIPT_DIR/build"
mkdir -p "$BUILD_DIR"

echo "== building dumper tools against the real compiler sources =="
g++ -std=c++17 -g -O0 -I"$SRC_DIR" -o "$BUILD_DIR/gen_arithmetic" "$SCRIPT_DIR/gen_arithmetic.cpp" "$SRC_DIR"/*.cpp
g++ -std=c++17 -g -O0 -I"$SRC_DIR" -o "$BUILD_DIR/gen_external_call" "$SCRIPT_DIR/gen_external_call.cpp" "$SRC_DIR"/*.cpp
g++ -std=c++17 -g -O0 -I"$SRC_DIR" -o "$BUILD_DIR/gen_fibonacci" "$SCRIPT_DIR/gen_fibonacci.cpp" "$SRC_DIR"/*.cpp
g++ -std=c++17 -g -O0 -I"$SRC_DIR" -o "$BUILD_DIR/gen_fibonacci_arg" "$SCRIPT_DIR/gen_fibonacci_arg.cpp" "$SRC_DIR"/*.cpp
g++ -std=c++17 -g -O0 -I"$SRC_DIR" -o "$BUILD_DIR/gen_named_function" "$SCRIPT_DIR/gen_named_function.cpp" "$SRC_DIR"/*.cpp
g++ -std=c++17 -g -O0 -I"$SRC_DIR" -o "$BUILD_DIR/gen_keyboard" "$SCRIPT_DIR/gen_keyboard.cpp" "$SRC_DIR"/*.cpp
g++ -std=c++17 -g -O0 -I"$SRC_DIR" -o "$BUILD_DIR/gen_saveload" "$SCRIPT_DIR/gen_saveload.cpp" "$SRC_DIR"/*.cpp

run_case() {
    name="$1"
    generator="$2"
    runner_src="$3"
    case_dir="$BUILD_DIR/$name"
    mkdir -p "$case_dir"

    echo "== $name: generating machine code =="
    "$BUILD_DIR/$generator" | grep -v "parser.cpp parse line" > "$case_dir/script_bytes.h"

    echo "== $name: compiling with the real Xtensa toolchain =="
    "$XTENSA_GCC" -mlongcalls -specs=sim.elf.specs -O0 \
        -I"$case_dir" -o "$case_dir/runner.elf" "$SCRIPT_DIR/$runner_src"

    echo "== $name: running under QEMU =="
    "$QEMU" -machine esp32 -nographic -kernel "$case_dir/runner.elf" > "$case_dir/output.log" 2>&1 &
    qemu_pid=$!
    sleep 3
    kill -9 "$qemu_pid" 2>/dev/null || true
    wait "$qemu_pid" 2>/dev/null || true

    cat "$case_dir/output.log"
    if grep -q "PASS" "$case_dir/output.log"; then
        echo "$name: PASS"
    else
        echo "$name: FAIL (see $case_dir/output.log)"
        exit 1
    fi
}

run_case "arithmetic" "gen_arithmetic" "runner_arithmetic.c"
run_case "external_call" "gen_external_call" "runner_external.c"
run_case "fibonacci" "gen_fibonacci" "runner_fibonacci.c"
run_case "fibonacci_arg" "gen_fibonacci_arg" "runner_fibonacci_arg.c"
run_case "named_function" "gen_named_function" "runner_named_function.c"
run_case "keyboard" "gen_keyboard" "runner_keyboard.c"
run_case "saveload" "gen_saveload" "runner_saveload.c"

echo "All QEMU execution checks passed."
