#pragma once
#ifndef __ASM_SERIALIZE__
#define __ASM_SERIALIZE__
#include "asm_types.h"
#include <stddef.h>
#include <stdint.h>

// Flattens a Binary (createBinary()'s output) into one self-contained
// byte blob suitable for writing to a file/flash partition and loading
// back later -- even from a *different* sketch than the one that
// compiled it. External references stay exactly as the *names*
// createBinary() already encoded in function_data ("@_ext_NAME" /
// "@_NAME(sig)"), resolved only at createExecutableFromBinary() time via
// binding.h's findLink() -- so the loading sketch just needs to call
// bindVariable()/bindFunction() for those same names before loading; it
// does not need the same pointers, or even the original script source.
//
// Returns a malloc'd buffer (caller owns it) and, via outSize, its
// length. Returns NULL (outSize untouched) if bin->error.error is set or
// bin's buffers are NULL.
uint8_t *serializeBinary(Binary *bin, uint32_t *outSize);

// Reconstructs a Binary from serializeBinary()'s output, with fresh
// malloc'd copies of binary_data/function_data -- createExecutableFromBinary()
// mutates binary_data in place, so these must not alias the input
// buffer, which is never modified or retained. Sets .error.error /
// .error_message on truncated or corrupt input.
Binary deserializeBinary(const uint8_t *buf, uint32_t size);

// Reads up to len bytes of a saved binary into buf; returns how many it
// read (fewer only at the end, or on an error). ctx is what the caller
// passed along: a FILE *, an fs::File *, ...
typedef size_t (*ScriptReadFunction)(void *ctx, uint8_t *buf, size_t len);

// Reads only the header of serializeBinary()'s output (the sizes) through
// read, leaving the reader at the code: what loadExecutableFromStream()
// starts with. bin's buffers stay NULL. Sets .error.error /
// .error_message on a short or wrong header.
void readBinaryHeader(ScriptReadFunction read, void *ctx, Binary *bin);

// Frees the buffers a Binary owns (binary_data/function_data). Both
// createBinary() and deserializeBinary() heap-allocate them the same
// way, and neither is freed automatically once consumed by
// createExecutableFromBinary().
void freeBinary(Binary *bin);

#endif
