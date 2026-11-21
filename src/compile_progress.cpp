#include <stdint.h>
#include <string.h>
#include "compile_progress.h"

static CompileProgressFunction progressFunction = NULL;
static void *progressContext = NULL;
static int lastPercent = -1;
static const char *lastStage = NULL;
static int rangeFrom = 0;
static int rangeTo = 0;
static const char *rangeStage = "";

void setCompileProgress(CompileProgressFunction fn, void *ctx)
{
    progressFunction = fn;
    progressContext = ctx;
}

void compileProgressStart()
{
    lastPercent = -1;
    lastStage = NULL;
    rangeFrom = 0;
    rangeTo = 0;
    rangeStage = "";
}

bool compileProgressWanted()
{
    return progressFunction != NULL;
}

void compileProgressRange(int from, int to, const char *stage)
{
    rangeFrom = from;
    rangeTo = to;
    rangeStage = stage;
    compileProgress(from, stage);
}

void compileProgressStep(uint32_t done, uint32_t total)
{
    compileProgressPart(rangeFrom, rangeTo, done, total, rangeStage);
}

void compileProgress(int percent, const char *stage)
{
    if (progressFunction == NULL)
        return;
    if (percent > 100)
        percent = 100;
    if (percent < lastPercent)
        percent = lastPercent;
    // once per percent, and once more when the stage changes
    if (percent == lastPercent && stage == lastStage)
        return;
    lastPercent = percent;
    lastStage = stage;
    progressFunction(percent, stage, progressContext);
}

void compileProgressPart(int from, int to, uint32_t done, uint32_t total, const char *stage)
{
    if (progressFunction == NULL)
        return;
    if (total == 0 || done >= total)
    {
        compileProgress(to, stage);
        return;
    }
    compileProgress(from + (int)((uint64_t)(to - from) * done / total), stage);
}
