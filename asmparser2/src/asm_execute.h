#pragma once
#ifndef __ASM_EXECUTE__
#define __ASM_EXECUTE__
// The loader: takes a Binary from createBinary() (src/asm_parser.h),
// allocates real memory for it, patches every external/internal reference
// through binding.h's binded_assets/findLink, and lets the caller invoke
// the resulting machine code directly. Ported from the upstream
// ESPLiveScript execute_asm.h, scoped down to a single script with no
// arguments and no concurrent-task scheduling (upstream's execute.h is a
// ~900-line FreeRTOS multi-task scheduler this project doesn't need).
//
// Verified under QEMU (Espressif's esp32 machine model) against real
// generated code: both a plain arithmetic/control-flow script and a
// script calling an external function through this exact relocation
// scheme produced the correct results on an emulated Xtensa CPU. Actual
// physical hardware has not been tested.
#include "asm_types.h"
#include "arguments.h"

//static int32_t callXtensaDirect(void *entry, const int32_t *args, int nargs);
// Allocates memory for `bin`, relocates every external/internal reference
// via binded_assets, and copies the code in. On error, ex.error.error is
// non-zero and no memory has been leaked (nothing else is populated).
executable createExecutableFromBinary(Binary *bin);

// Diagnostic dump: prints every function this executable exposes as a
// callable entry point (i.e. everything callFunction()/
// runExecutableWithArgs() can find by name), one per line, with its
// address as an offset from ex->start_program -- the same "relative
// address" callFunction() itself adds to start_program to get the real
// call target. Call this any time after createExecutableFromBinary()
// has returned a loaded executable (ex->start_program/ex->functions
// populated); does nothing useful before that. Skips wrapper records
// (see isWrapperRecord()'s comment in asm_execute.cpp) -- they share
// their plain counterpart's name and aren't independently callable, so
// listing both would just show duplicate-looking names.
void printExecutableFunctions(executable *ex);

// Diagnostic dump: same idea as binary_hex.h's printBinaryHex(), but for
// a *loaded* executable instead of the pre-load Binary. Dumps the
// relocated instruction bytes actually sitting in ex->start_program
// (ex->binary_size bytes -- on-target this is real MALLOC_CAP_EXEC
// memory, already relocation-patched and cache-synced, unlike Binary's
// binary_data which still has zeroed placeholder slots/literals) and the
// data region at ex->data (ex->data_size bytes), as two clearly labeled
// sections. Does nothing if ex is NULL or ex->start_program is NULL (e.g.
// createExecutableFromBinary() failed).
void printExecutableHex(executable *ex);

// Calls any function declared in the script by name (e.g. "fib" for a
// script that declares `int fib(int n) {...}`), passing up to 6 int32_t
// (or float, reinterpreted as raw bits) arguments directly in registers
// a10..a15 -- the same calling convention compiled call8 callers use
// (e.g. how the script's own main() calls another script function), so
// this bypasses the argument-marshaling wrapper entirely and calls the
// function's real address directly. If `result` is non-NULL, writes
// whatever ends up in a10 after the call: the standard Xtensa windowed
// ABI's return-value register, which is also exactly where the compiled
// code for a script's `return expr;` leaves its value (see
// visitnode.cpp) -- so this doubles as reading the function's return
// value, when it has one; garbage otherwise. No-op (returns false) if
// not actually compiled for Xtensa, since a10..a15 aren't valid register
// names on any other target.
//
// A function declared to take N>0 parameters actually gets *two* header
// records (see asm_parser.cpp's createBinaryHeader): a *plain* one at
// the function's real address, and a *wrapper*, generated only because
// there are parameters, that loads them from a reserved stack-storage
// area into the right registers before call8-ing the real function --
// used by runExecutable()/runExecutableWithArgs() for main() (see their
// own comment for why), but NOT by this function, since the wrapper's
// own call8+retw.n never copies the callee's return value into a10 for
// its own return -- it exists purely to marshal arguments in, so calling
// it here would come back with whatever garbage was already in a10
// rather than the real result.
//
// Returns false if no declared function is named `name`.
bool callFunction(executable *ex, const char *name, const int32_t *args, int nargs, int32_t *result);

// Same as above, but taking a typed Arguments list (arguments.h, ported
// from upstream ESPLiveScript) instead of a raw int32_t array -- nicer
// when the caller wants to mix int and float arguments without having to
// reinterpret float bits by hand. Internally just does that
// reinterpretation and forwards to the array-based overload; still capped
// at 6 arguments (a10..a15), same as it is there.
bool callFunction(executable *ex, const char *name, Arguments *args, int32_t *result);

// Calls the script's main() with no arguments, through its wrapper (see
// callFunction()'s doc comment for plain-vs-wrapper) -- matches upstream
// execute_asm.h's own convention for the program's entry point, which
// has no return value to worry about losing.
void runExecutable(executable *ex);

// Calls the script's main() passing `nargs` 4-byte argument values,
// marshaled through the wrapper's reserved stack-storage area exactly
// like decodeBinaryHeader's case-2 external-call resolution and
// asm_execute.cpp's own comment describe. For calling any *other*
// declared function (and/or reading a return value), use callFunction()
// instead -- it's simpler (direct register passing, no wrapper or data
// buffer involved) and works for any name, not just "main".
bool runExecutableWithArgs(executable *ex, const int32_t *args, int nargs);

// Same as above, but taking a typed Arguments list -- see callFunction()'s
// Arguments overload for why.
bool runExecutableWithArgs(executable *ex, Arguments *args);

// Releases the memory createExecutableFromBinary allocated.
void freeExecutable(executable *ex);

#endif
