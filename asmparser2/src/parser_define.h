#pragma once
#ifndef _PARSER_DEFINE_
#define _PARSER_DEFINE_

#define _START_2 32
#define _STACK_SIZE (_START_2 + 6 * 4)
#define _MAX_FOR_DEPTH_REG 4
#define _MAX_FOR_DEPTH_REG_2 2
#define _TRIGGER 20

// PARSER_DEBUG_MODE is opt-in, same convention as asm_execute.cpp's
// ASM_PARSER_DEBUG: a plain `#define PARSER_DEBUG_MODE` in a sketch's
// own .ino/.cpp does NOT reach this header -- Arduino (and any other
// per-file-compiled build) compiles every .cpp as its own translation
// unit, so a #define in one file's source never affects another file's
// preprocessing. To actually turn this on, define PARSER_DEBUG_MODE as
// a project-wide build flag instead (e.g. platformio.ini's
// build_flags, a boards.txt/board_build.flags entry, or
// add_compile_definitions() in an ESP-IDF CMakeLists.txt) so it reaches
// every translation unit that includes this header, parser.cpp's
// included.
#ifdef PARSER_DEBUG_MODE
#define PARSER_LOG(...) {printf("[%s %s line:%d] ",__FILE__, __FUNCTION__,__LINE__); printf(__VA_ARGS__);printf("\n");}
#else
#define PARSER_LOG(...)
#endif


#define __TEST_DEBUG
#define __MEM_PARSER

#define EOF_TEXT 0
#define EOF_TEXTARRAY 9999
#define EOF_VARTYPE 14
#endif