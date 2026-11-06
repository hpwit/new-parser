#pragma once
#ifndef __BINARY_HEX__
#define __BINARY_HEX__
#include "asm_types.h"
#include <stdint.h>

// Neither v1 (ESPLiveScript) nor this port had a general-purpose "dump
// these compiled bytes" utility -- v1's closest relative,
// asm_parser.h's printparsdAsm(), disassembles parsedLines (the
// pre-assembly intermediate form, one struct per instruction/label) into
// address+opcode+mnemonic lines, and only ever ran behind a
// __TEST_DEBUG guard that's commented out at its one call site. Nothing
// in either version prints the actual compiled output -- createBinary()'s
// binary_data/function_data, or serializeBinary()'s flattened blob -- as
// raw bytes. These two fill that gap.

// Hex-dumps `size` bytes starting at `data`, `bytesPerLine` per line
// (default 16), each line prefixed with its offset from `data` in hex --
// a generic byte-buffer dump, not specific to compiled scripts. Works
// equally on a Binary's binary_data/function_data (see printBinaryHex()
// below) or a serializeBinary() blob (asm_serialize.h) -- anything
// that's just a byte buffer + length. Prints "(empty)" and returns
// immediately if data is NULL or size is 0. Uses printf() directly, same
// as every other diagnostic print in this codebase (compiler_error.cpp's
// display_error(), runtime_functions.cpp's built-in printf/printfln) --
// on a real sketch that resolves to Serial output the same way those do.
void printHex(const uint8_t *data, uint32_t size, uint32_t bytesPerLine = 16);

// Same output format as printHex(), but reads `data` a 32-bit word at a
// time instead of byte-by-byte -- required for dumping memory that isn't
// byte-addressable from the CPU's data bus, which on real Xtensa/ESP32
// silicon is exactly what MALLOC_CAP_EXEC/IRAM memory is: a plain
// `uint8_t` load from it (what printHex()'s data[i] does) faults
// (LoadStoreError), even though the same region is perfectly readable as
// instructions or via 32-bit-aligned loads. Use this instead of printHex()
// for any buffer that's real on-target executable memory -- in this
// codebase, that's specifically an executable's start_program (see
// asm_execute.h's printExecutableHex()), never a Binary's binary_data
// (still plain host/heap memory at that stage, before
// createExecutableFromBinary() copies it into MALLOC_CAP_EXEC). Assumes
// `size` is a multiple of 4 (true for compiled instruction buffers --
// Xtensa's own alignment padding, e.g. the nop/nop.n seen after every
// function's retw.n, guarantees this); trailing bytes past the last full
// word are not printed if it isn't.
void printHexWords(const uint32_t *data, uint32_t size, uint32_t bytesPerLine = 16);

// Convenience wrapper over printHex() for createBinary()'s own output:
// dumps the instruction bytes (bin->binary_data, bin->instruction_size --
// the same count test_large_script.cpp/every budget check in this repo
// already reports) and the relocation header (bin->function_data,
// bin->function_size) as two clearly labeled sections. Does nothing if
// bin is NULL or bin->error.error is set.
//
// Deliberately uses instruction_size here, not tmp_instruction_size --
// binary_data's underlying buffer is actually tmp_instruction_size bytes
// (instruction_size's worth of real code, followed by a scratch data-size
// work area createBinary() uses internally during assembly; see
// asm_parser.cpp's createBinary() and asm_serialize.cpp's
// serializeBinary(), which persists the full tmp_instruction_size region
// since that's what a reload needs). instruction_size is the
// conceptually meaningful "compiled program" byte count; if you want a
// byte-exact dump of exactly what serializeBinary() would persist
// instead, call printHex() directly on that function's output.
void printBinaryHex(Binary *bin);

#endif
