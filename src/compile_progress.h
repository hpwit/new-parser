#pragma once
#include <stdint.h>
// How far parseScript() / parseScriptToBinary() has got, for a host that
// shows it while a large script compiles (seconds to minutes on an esp32):
//
//   void showProgress(int percent, const char *stage, void *ctx)
//   {
//      printf("\rcompiling... %s %d%%   ", stage, percent);
//   }
//
//   setCompileProgress(showProgress, NULL);
//   ScriptExecutable exec = parseScript(script);    // showProgress called as it goes
//   setCompileProgress(NULL, NULL);                  // optional: no more calls
//
// percent goes from 0 to 100 and never goes back; the callback runs only
// when it moves on (at most once per percent, plus once per new stage), so
// it may draw or print without slowing the compiler down. stage names what
// the compiler is doing: "reading" (the text into tokens), "parsing",
// "generating" (the machine instructions), "optimizing", "assembling" (into
// bytes), then "done" with 100. A script with an error stops where the
// error is found: no "done", and parseScript() reports the error as usual.
//
// The callback runs on the task that compiles, inside the compiler: it must
// not compile, load or free a script itself. ctx is passed back unchanged.
// The percentages are weighted from the time each stage takes on real
// scripts (Valmora, Ironforge, Tetris), so the number moves about evenly.
typedef void (*CompileProgressFunction)(int percent, const char *stage, void *ctx);

void setCompileProgress(CompileProgressFunction fn, void *ctx);

// For the compiler itself: where each step ends, in percent, from the share
// of the time each takes compiling Valmora, Ironforge and Tetris...
#define PROGRESS_PARSED 21          // reading and parsing
#define PROGRESS_GENERATED 22       // the instructions generated
#define PROGRESS_OPTIMIZE1 36       // optimize()
#define PROGRESS_OPTIMIZE2 38       // optimizeSpeed()
#define PROGRESS_OPTIMIZE3 49       // preserveAcrossCalls()
#define PROGRESS_OPTIMIZED 75       // allocateLoopRegisters()
#define PROGRESS_ASM_LINES 85       // the assembler's first passes
#define PROGRESS_ASM_LINES_END 93   // each line into bytes
#define PROGRESS_ASSEMBLED 99       // the jumps, the data

// ...and a step reached, or part of one done.
void compileProgressStart();
bool compileProgressWanted();
void compileProgress(int percent, const char *stage);
void compileProgressPart(int from, int to, uint32_t done, uint32_t total, const char *stage);
// A pass that knows how far it is (a line of a total) but not where it
// stands in the whole compile: the caller sets the range, the pass steps.
void compileProgressRange(int from, int to, const char *stage);
void compileProgressStep(uint32_t done, uint32_t total);
