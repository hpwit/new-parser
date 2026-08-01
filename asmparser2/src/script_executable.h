#pragma once
#ifndef __SCRIPT_EXECUTABLE__
#define __SCRIPT_EXECUTABLE__
// A v1-style convenience layer for sketches that don't need the
// individual parse -> createBinary -> createExecutableFromBinary steps
// spelled out (see any examples/*.ino file for what those look like) --
// matches the ergonomics of upstream ESPLiveScript's
// Parser::parseScript()/Executable::isExeExists()/Executable::execute():
//
//   ScriptExecutable exec = parseScript(script);
//   if (exec.isExeExists())
//   {
//      exec.execute("main");
//   }
//   // no explicit cleanup needed -- ~ScriptExecutable() frees the
//   // loaded executable automatically when exec goes out of scope.
#include "asm_execute.h"
#include "arguments.h"

// Owns a compiled+loaded script; ~ScriptExecutable() frees it via
// freeExecutable() automatically.
//
// Deliberately NOT copyable *or* movable, unlike a typical modern C++
// RAII type -- and that's a real constraint, not stylistic caution:
// `executable` (asm_types.h) holds vect<globalcall>/vect<jsonVariable>
// members, and vect<T> (vect.h) has a destructor (frees its backing
// array) but no matching copy/move constructor or assignment operator --
// a Rule-of-Three gap that predates this file. Assigning or copy/move-
// constructing an `executable` (or anything containing one) invokes the
// compiler-generated *shallow* member-wise copy, so two objects end up
// sharing the same vect<T> backing-array pointer; whichever is destroyed
// first frees it out from under the other (confirmed via
// AddressSanitizer: a heap-use-after-free in isWrapperRecord(), from an
// earlier version of this class that tried to hand-roll safe move
// semantics by assigning `exe = other.exe`). The only thing that's
// actually safe with these types is direct-initializing a fresh variable
// from a prvalue function-call expression, e.g. `executable exe =
// createExecutableFromBinary(&bin);` (every example does this) or
// `return ScriptExecutable(&bin);` (parseScript() does this) --
// C++17 guarantees that construction is elided, never actually invoking
// copy/move at all. Deleting copy *and* move here makes any attempt to
// do something else (store one in a container, return a named local
// instead of a fresh prvalue, etc.) a compile error instead of a latent
// crash. If you need to hold several compiled scripts at once, store
// each behind its own separate ScriptExecutable-holding scope/variable
// (each gets its own parseScript() call) rather than trying to move one
// around.
class ScriptExecutable
{
public:
    ScriptExecutable() {}
    // Constructs by calling createExecutableFromBinary(bin) directly in
    // the member-initializer list (so `exe` is elision-constructed from
    // that call's prvalue, per the class comment above), then frees
    // bin's own buffers (binary_data/function_data -- see freeBinary(),
    // asm_serialize.h) since createExecutableFromBinary() only ever
    // copies out of them, and reports a loader error if there was one --
    // this has to happen here, after `exe` exists, rather than in
    // parseScript() itself, precisely because getting `exe`'s value out
    // to check it would require the same unsafe copy/assignment this
    // class exists to avoid.
    explicit ScriptExecutable(Binary *bin);
    ~ScriptExecutable() { freeExecutable(&exe); }

    ScriptExecutable(const ScriptExecutable &) = delete;
    ScriptExecutable &operator=(const ScriptExecutable &) = delete;
    ScriptExecutable(ScriptExecutable &&) = delete;
    ScriptExecutable &operator=(ScriptExecutable &&) = delete;

    // Matches upstream's Executable::isExeExists() -- true if parsing,
    // assembling, and loading all succeeded and there's at least one
    // callable function. Not const-qualified: vect<T>::size() (vect.h)
    // isn't const either, matching this codebase's style throughout.
    bool isExeExists();

    // Calls a declared function by name (main() included -- see
    // callFunction()'s own doc comment on why that works even for it)
    // via callFunction(), so `result` (if non-NULL) receives a real
    // return value -- unlike runExecutable()/runExecutableWithArgs(),
    // which only exist for main() and never surface one. Three
    // overloads mirroring callFunction()'s own: no arguments, a typed
    // Arguments list, or a raw int32_t[].
    bool execute(const char *name, int32_t *result = NULL);
    bool execute(const char *name, Arguments *args, int32_t *result = NULL);
    bool execute(const char *name, const int32_t *args, int nargs, int32_t *result = NULL);

    executable exe;
};

// Parses, assembles, and loads `script` in one call -- the v2 equivalent
// of upstream's Parser::parseScript(). On any failure (parse, assemble,
// or load error), the returned ScriptExecutable's isExeExists() is
// false; the specific error is also printed (via display_error() for a
// parse error, or a plain printf for an assembler/loader error), the
// same way every hand-written example already reports it. Frees every
// intermediate buffer this pipeline allocates that isn't part of the
// final loaded executable -- the strdup'd copy of `script` the tokenizer
// needs to own, and (via ScriptExecutable's constructor) Binary's
// binary_data/function_data, a real if small one-time-per-compile leak
// in every plain compile-from-source example in this repo (freeBinary()
// is already used by LoadScriptBinary.ino/test_parser.cpp's save/load
// path, but nowhere else). Only the returned ScriptExecutable's own
// executable survives, freed automatically when it's destroyed.
ScriptExecutable parseScript(const char *script);

#endif
