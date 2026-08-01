#pragma once
#ifndef __OPTIMIZE__
#define __OPTIMIZE__
#include "stackfunctions.h"

// Peephole-optimizes a buffer of generated assembly lines in place:
// eliminates redundant reloads of a register that already holds the value
// (per register a3..a10), drops dead float reloads, folds "op ...; mov
// aY,aX" pairs into a single instruction writing aY directly, propagates
// register-to-register moves (movr) forward into later uses/branches, and
// removes store-then-immediate-reload pairs (s32i/l32i, s8i/l8ui, ssi/lsi).
void optimize(Text *text);
#endif
