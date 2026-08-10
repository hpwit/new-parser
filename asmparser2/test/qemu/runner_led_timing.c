// Loads + relocates the compiled LED-matrix script by hand (see
// gen_led_timing.cpp's header comment for why not
// createExecutableFromBinary()) and measures real Xtensa CCOUNT cycles for
// this version of the script -- the user removed main()'s `while(true)`
// wrapper, so it now runs the nested x/y pixel loop exactly once and
// returns, instead of looping forever. That means there's no second
// show() call to diff against for a steady-state per-frame delta; instead
// this times init() alone (called once, standalone, before main() -- its
// own precompute loop is idempotent, so this doesn't change what main()'s
// own internal init() call below computes) and main() alone (which calls
// init() *again* internally, then does the actual render loop, then
// calls show() once), sampling CCOUNT right when show() fires so the
// init()-vs-render split inside that single main() call is visible too.
// atan2/hypot/sin8/hsv are given real, on-target implementations (not
// just stubs) so the measured cost reflects genuine per-pixel work; show()
// itself is intentionally near-zero-cost (just a CCOUNT read) since it has
// no real hardware (SPI/I2S DMA) to drive under QEMU.
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "script_bytes.h"

#define NUM_LEDS 12288

// Heap-allocated rather than static arrays: DATA_SIZE (~24KB) + the leds
// buffer (~36KB) overflow sim.elf.specs' small fixed dram_seg by a couple
// KB if statically sized -- runner_ball_repro.c's own heapCode malloc()
// already confirms the heap works fine in this environment, just not the
// tiny static BSS region.
static uint8_t *dataRegion;
static uint8_t *ledsBuffer;

static uint32_t showCcount = 0;
static int showCalled = 0;

static inline uint32_t readCcount(void)
{
    uint32_t c;
    __asm__ volatile("rsr.ccount %0" : "=r"(c));
    return c;
}

// Real per-pixel work, not stand-ins -- see this file's header comment.
static float targetAtan2(float y, float x) { return atan2f(y, x); }
static float targetHypot(float x, float y) { return hypotf(x, y); }

// A 256-entry lookup table, matching how a real embedded sin8 (e.g.
// FastLED's) is actually implemented -- computed once at startup so its
// setup cost isn't counted in any frame's measured time, only its
// per-call lookup cost is.
static uint8_t sin8Table[256];
static void buildSin8Table(void)
{
    for (int i = 0; i < 256; i++)
    {
        float rad = (float)i * (2.0f * (float)M_PI / 256.0f);
        int v = (int)(127.5f + 127.5f * sinf(rad));
        if (v < 0)
            v = 0;
        if (v > 255)
            v = 255;
        sin8Table[i] = (uint8_t)v;
    }
}
static uint32_t targetSin8(uint32_t theta) { return sin8Table[theta & 0xff]; }

// Plain integer HSV->RGB (a standard six-sector formula, not
// FastLED's exact rainbow palette -- color accuracy doesn't matter here,
// only that it's a real, representative amount of per-pixel work).
// Packs into the same r|g<<8|b<<16 layout the script's own generated
// code unpacks via extui (confirmed against the compiled assembly).
static uint32_t targetHsv(uint32_t h, uint32_t s, uint32_t v)
{
    uint8_t hue = (uint8_t)h;
    uint8_t sat = (uint8_t)s;
    uint8_t val = (uint8_t)v;
    uint8_t region = hue / 43;
    uint8_t remainder = (hue - (region * 43)) * 6;
    uint8_t p = (val * (255 - sat)) >> 8;
    uint8_t q = (val * (255 - ((sat * remainder) >> 8))) >> 8;
    uint8_t t = (val * (255 - ((sat * (255 - remainder)) >> 8))) >> 8;
    uint8_t r, g, b;
    switch (region)
    {
    case 0: r = val; g = t; b = p; break;
    case 1: r = q; g = val; b = p; break;
    case 2: r = p; g = val; b = t; break;
    case 3: r = p; g = q; b = val; break;
    case 4: r = t; g = p; b = val; break;
    default: r = val; g = p; b = q; break;
    }
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16);
}

static void targetShow(void)
{
    showCcount = readCcount();
    showCalled = 1;
    printf("show() called, ccount=%u\n", showCcount);
    fflush(stdout);
}

static void callVoid(void *entry)
{
    register void *r8 __asm__("a8") = entry;
    __asm__ volatile("callx8 a8\n\t" : : "r"(r8) : "memory", "a10");
}

int main()
{
    printf("START led_timing\n");
    fflush(stdout);

    buildSin8Table();

    dataRegion = (uint8_t *)calloc(1, DATA_SIZE);
    ledsBuffer = (uint8_t *)calloc(1, NUM_LEDS * 3);

    unsigned char *heapCode = (unsigned char *)malloc(SCRIPT_CODE_SIZE);
    memcpy(heapCode, script_code, SCRIPT_CODE_SIZE);

    for (int i = 0; i < NUM_RELOC_SLOTS; i++)
    {
        uint32_t content = (uint32_t)dataRegion + reloc_slot_bincode[i];
        *(uint32_t *)(heapCode + reloc_slot_offset[i]) = content;
    }

    for (int i = 0; i < NUM_DATA3_RECORDS; i++)
        memcpy(dataRegion + data3_dest_addr[i], data3_blob + data3_blob_offset[i], data3_size[i]);

    for (int i = 0; i < NUM_VAR_SLOTS; i++)
    {
        if (strcmp(var_name[i], "leds") == 0)
            *(uint32_t *)(heapCode + var_slot_offset[i]) = (uint32_t)(uintptr_t)ledsBuffer;
        else
        {
            printf("RESULT unknown external variable '%s' -- runner doesn't implement it\n", var_name[i]);
            fflush(stdout);
            return 1;
        }
    }

    for (int i = 0; i < NUM_EXT_SLOTS; i++)
    {
        uint32_t content = 0;
        if (strcmp(ext_name[i], "atan2") == 0)
            content = (uint32_t)(uintptr_t)targetAtan2;
        else if (strcmp(ext_name[i], "hypot") == 0)
            content = (uint32_t)(uintptr_t)targetHypot;
        else if (strcmp(ext_name[i], "sin8") == 0)
            content = (uint32_t)(uintptr_t)targetSin8;
        else if (strcmp(ext_name[i], "hsv") == 0)
            content = (uint32_t)(uintptr_t)targetHsv;
        else if (strcmp(ext_name[i], "show") == 0)
            content = (uint32_t)(uintptr_t)targetShow;
        else
        {
            printf("RESULT unknown external '%s' -- runner doesn't implement it\n", ext_name[i]);
            fflush(stdout);
            return 1;
        }
        *(uint32_t *)(heapCode + ext_slot_offset[i]) = content;
    }

#if HAS_INIT
    printf("calling init() standalone (precomputes rMapAngle/rMapRadius, one 128x96 pass)...\n");
    fflush(stdout);
    uint32_t initBefore = readCcount();
    callVoid(heapCode + INIT_ENTRY_OFFSET);
    uint32_t initAfter = readCcount();
    uint32_t initCycles = initAfter - initBefore;
    printf("RESULT init(): %u cycles = %.3f ms\n", initCycles, (double)initCycles / (240000000.0 / 1000.0));
    fflush(stdout);
#endif

    printf("calling main() -- re-runs init(), then the render loop, then show() once...\n");
    fflush(stdout);
    uint32_t mainBefore = readCcount();
    callVoid(heapCode + MAIN_ENTRY_OFFSET);
    uint32_t mainAfter = readCcount();

    if (!showCalled)
    {
        printf("RESULT FAIL -- main() returned without ever calling show()\n");
        fflush(stdout);
        return 1;
    }

    uint32_t toShowCycles = showCcount - mainBefore;
    uint32_t afterShowCycles = mainAfter - showCcount;
    printf("RESULT main()-entry to show(): %u cycles = %.3f ms (includes main()'s own internal init() call)\n",
           toShowCycles, (double)toShowCycles / (240000000.0 / 1000.0));
    printf("RESULT show() to main()-return: %u cycles = %.3f ms\n",
           afterShowCycles, (double)afterShowCycles / (240000000.0 / 1000.0));
#if HAS_INIT
    if (toShowCycles > initCycles)
    {
        uint32_t renderOnly = toShowCycles - initCycles;
        printf("RESULT estimated render-loop-only cost (main()-entry-to-show() minus standalone init() cost): "
               "%u cycles = %.3f ms (%.1f fps if called every frame)\n",
               renderOnly, (double)renderOnly / (240000000.0 / 1000.0),
               1000.0 / ((double)renderOnly / (240000000.0 / 1000.0)));
    }
#endif
    fflush(stdout);
    return 0;
}
