#!/bin/sh
# Full-system tests: each sketch directory here is built as a real ESP32
# Arduino application (arduino-cli, esp32 core) and booted from its flash
# image under Espressif's QEMU (esp32 machine). Unlike test/qemu/, nothing
# is hand-assembled: scripts are compiled on the emulated chip by the real
# library, so this covers the loader, executable memory and the host-side
# runtime too. Each sketch prints "RESULT PASS" or "RESULT FAIL".
#
# Usage: QEMU=/path/to/qemu-system-xtensa ./run.sh [sketch ...]
# Needs arduino-cli with the esp32:esp32 core installed. Flash mode must be
# DIO: QEMU's flash model does not support QIO, the core's default.
set -e

: "${QEMU:?set QEMU to the Espressif qemu-system-xtensa binary path}"
FQBN="${FQBN:-esp32:esp32:esp32:FlashMode=dio,FlashFreq=40}"
TIMEOUT="${TIMEOUT:-60}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
mkdir -p "$BUILD_DIR"

if [ $# -eq 0 ]; then
    set -- $(cd "$SCRIPT_DIR" && ls -d */ | sed 's#/##' | grep -v '^build$')
fi

failed=0
for sketch in "$@"; do
    out="$BUILD_DIR/$sketch"
    rm -rf "$out"
    mkdir -p "$out"
    echo "== $sketch: building"
    arduino-cli compile --fqbn "$FQBN" --output-dir "$out" "$SCRIPT_DIR/$sketch" > "$out/build.log" 2>&1 ||
        { cat "$out/build.log"; echo "$sketch: BUILD FAILED"; failed=1; continue; }

    echo "== $sketch: running under QEMU"
    "$QEMU" -machine esp32 -nographic \
        -drive file="$out/$sketch.ino.merged.bin",if=mtd,format=raw > "$out/output.log" 2>&1 &
    pid=$!
    i=0
    while [ $i -lt "$TIMEOUT" ] && ! grep -q "RESULT" "$out/output.log"; do
        sleep 1
        i=$((i + 1))
    done
    kill -9 "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true

    sed -n '/^START/,/^RESULT/p' "$out/output.log"
    if grep -q "RESULT PASS" "$out/output.log"; then
        echo "$sketch: PASS"
    else
        echo "$sketch: FAIL (see $out/output.log)"
        failed=1
    fi
done
[ $failed -eq 0 ] && echo "All full-system QEMU checks passed."
exit $failed
