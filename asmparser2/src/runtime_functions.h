#pragma once
#ifndef __RUNTIME_FUNCTIONS__
#define __RUNTIME_FUNCTIONS__
// Built-in host-side implementations of upstream ESPLiveScript's
// "always available" script functions -- ported from execute_asm.h's
// artiPrintf()/artiPrintfln() and the INIT_PARSER class that
// auto-registers them via addExternalFunction() before any script is
// compiled.
//
// v2 doesn't use a global static object for the registration (unlike
// upstream's `INIT_PARSER initialization_parser;`) -- that pattern
// relies on binded_assets (binding.cpp) already being constructed by
// the time INIT_PARSER's own constructor runs, which C++ doesn't
// actually guarantee across translation units (the "static
// initialization order fiasco"). registerBuiltinRuntimeFunctions()
// instead runs lazily, called from two places: Parser::parse() itself
// (parser.cpp), covering every path that compiles a script in this
// process (parseScript() and the low-level Parser/createBinary/
// createExecutableFromBinary pipeline both go through it); and
// asm_execute.cpp's createExecutableFromBinary() itself, covering a
// process that only ever *loads* a previously-compiled binary
// (createExecutableFromBuffer(), or a hand-rolled deserializeBinary() +
// createExecutableFromBinary()) and so never calls Parser::parse() at
// all -- without that second call site, such a process's binded_assets
// would never gain printf/printfln, and relocating a saved script that
// calls either would fail with "external function ... not found" despite
// them being documented as always available. Both call sites are safe
// to hit unconditionally -- registerBuiltinRuntimeFunctions() guards
// itself with a static bool, so registering twice in one process is a
// no-op the second time.
//
// Verified under QEMU with a real, compiled call site (not just that it
// assembles): printfln("i:%d 3*i:%d", a, b) with a=5, b=15 produced the
// exact expected output through this exact registration.
void registerBuiltinRuntimeFunctions();

// safe_mode's runtime half. A script compiled with `safe_mode` calls the
// built-in safeModeError(name, size, index, handle) when an indexed store
// is out of bounds (see visitnode.cpp's emitSafeBoundsCheck()). It
//   1. records the failure in the executable's runtimeError,
//   2. reports it -- through onScriptRuntimeError if the host installed
//      one, otherwise as one line on stdout:
//      "Execution error: index 12 is out of bounds for array 'buf' (size 10)"
//   3. abandons the script: control returns to the host call that entered
//      it (execute(), callFunction(), runExecutable()...), which returns
//      false.
// Like printf/printfln it is registered automatically; a host can bind
// its own "safeModeError" before compiling to replace the whole thing.
struct runtime_error_struct;
extern void (*onScriptRuntimeError)(const runtime_error_struct *error);

#endif
