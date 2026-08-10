#pragma once
#ifndef __SCRIPT_OPTIMIZE_H__
#define __SCRIPT_OPTIMIZE_H__
#include "stackfunctions.h"

// Peephole-optimizes a buffer of generated assembly lines in place:
// eliminates redundant reloads of a register that already holds the value
// (per register a3..a10), drops dead float reloads, folds "op ...; mov
// aY,aX" pairs into a single instruction writing aY directly, propagates
// register-to-register moves (movr) forward into later uses/branches, and
// removes store-then-immediate-reload pairs (s32i/l32i, s8i/l8ui, ssi/lsi).
void optimize(Text *text);

// Runs after optimize() -- extends optimize()'s own Pass 1 (redundant-
// reload elimination) to registers a11..a15, which it doesn't cover.
// register_numl (visitnode.cpp) allocates expression-evaluation scratch
// registers starting at a15 downward, so a11..a15 -- not a3..a10 -- is
// where this compiler's own generated code actually spends most of its
// register traffic; optimize()'s Pass 1 stopping at a10 leaves a real,
// common class of redundant reloads (e.g. the same `movi aY,N`/`l32r
// aY,@_global` reappearing a few lines later with nothing in between
// touching aY) sitting in every script that indexes two different arrays
// with a shared sub-expression, which is a common enough shape (a script
// computing one index and storing to two parallel arrays with it) to be
// worth a dedicated pass. See optimize.cpp's optimizeSpeed() for why this
// needs its own call8/callExt invalidation rule, not just a wider loop
// bound on the existing one.
void optimizeSpeed(Text *text);
#endif
