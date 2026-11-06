#pragma once
#ifndef __ASM_PARSER__
#define __ASM_PARSER__
// The assembler: turns the text lines in a Text buffer (content/header/
// footer, as produced by visitnode.cpp) into real Xtensa machine code.
// Ported from the upstream ESPLiveScript asm_parser.h, using vect<T>/Text
// and C string functions instead of std::vector/std::string.
//
// optimize() is expected to have already run on `content` (Parser::parse()
// does this) -- unlike upstream, createBinary() here does not call it
// again internally.
#include "asm_types.h"
#include "stackfunctions.h"

// Produces the final machine code + relocation header for a compiled
// script. Consumes (empties) footer/header/content in the process, same
// as upstream -- snapshot them first if you need the text afterward.
Binary createBinary(Text *_footer, Text *_header, Text *_content, bool display);

#endif
