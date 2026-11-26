#pragma once
#ifndef __VECTORIZE__
#define __VECTORIZE__
// Loops over byte arrays turned into ESP32-S3 PIE code (see vectorize.cpp).
#include "nodetoken.h"

// Called by _visitforNode(): true when the loop was compiled to vector
// code (a call to a generated assembly function), false to compile it as
// usual. Only for an ESP32-S3; on a computer when ASM_VECTORIZE is set
// (the tests), or after setLoopVectorize(true).
bool vectorizeLoop(NodeToken *forNode);

// On by default for an ESP32-S3 target, off elsewhere.
void setLoopVectorize(bool on);

// A new compile: generated function names start again.
void resetLoopVectorize();

#endif
