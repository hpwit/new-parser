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
//
// Also removes a `retw.n` immediately following another `retw.n` (dead --
// unreachable, and never a jump target since a label in between resets
// the check): _visitdefFunctionNode() (visitnode.cpp) always appends its
// own closing retw.n after a function's body regardless of whether the
// body's last statement was already a `return`, so every function ending
// in `return` compiles with one dead retw.n.
//
// And removes an `ssai N` when SAR already holds N (consecutive ffloat
// multiplies each set it to 16), forgetting SAR at labels, calls and any
// other SAR write.
void optimizeSpeed(Text *text);

// Physically deletes every blanked-out line optimize()/optimizeSpeed()
// leave behind (blankCurrent()/replaceText(i, " ") mark a line dead in
// place -- " " or "" -- rather than shifting every later index by
// erasing it there, since a peephole pass is still walking the buffer by
// position when it blanks something). Run once, after both passes are
// completely done, so nothing still depends on a blanked slot's position.
// Purely cosmetic/for-inspection -- createBinary() already tolerates
// (and skips) blank lines on its own, so this changes what
// content.display() prints, not what gets assembled.
void removeBlankLines(Text *text);

// Makes every value that is held in a call-clobbered register across a
// call survive it. Expression temporaries are handed out from a15/f15
// downward, and nothing in the code generator moves one out of the way
// when the rest of the expression contains a call: in `20 + rand(200)`
// the 20 sits in a15 while rand() runs, and in
// `readInt(b, 0) + readInt(b, 4)` the first result does. A windowed call
// gives the callee the caller's a8..a15 as its own a0..a7, and every
// float register is caller-saved, so whether the value survived depended
// on what the callee happened to use.
//
// For each call, each such register that is read again before being
// rewritten is stored to a frame slot just before the call and loaded
// back right after it; the function's `entry` is enlarged to make room.
// Since each save/restore pair sits tight around its own call, all calls
// in a function share the same slots.
//
// Runs last, on the final text, so no other pass has to know about the
// extra loads and stores. Run on `content` and on `footer` (the global
// initializers' function).
void preserveAcrossCalls(Text *text);
#endif
