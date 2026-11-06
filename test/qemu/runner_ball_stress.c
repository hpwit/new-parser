#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include "script_bytes.h"

typedef struct
{
    uint8_t r, g, b;
} CRGB;

static void callVoid(void *entry)
{
    register void *r8 __asm__("a8") = entry;
    __asm__ volatile("callx8 a8\n\t" : : "r"(r8) : "memory", "a10");
}

/* Real backing storage for this script's data region -- every type-0
   slot (not just Balls[]'s own) needs patching into it, see
   gen_arr_index.cpp's own header comment for why. */
static uint8_t dataRegion[DATA_SIZE];

/* leds[height,width] -- real backing storage for the external array,
   guard-padded on both sides with a recognizable canary pattern so an
   out-of-bounds write (the leading hypothesis briefly considered, then
   ruled out by hand for this exact script's r=8 case -- see the commit
   history) would actually be caught here instead of silently
   corrupting whatever heap memory happens to sit next to a plain
   fixed-size array. */
#define LED_WIDTH 128
#define LED_HEIGHT 96
#define GUARD_BYTES 64
#define GUARD_PATTERN 0xA5
static uint8_t ledsBacking[GUARD_BYTES + LED_WIDTH * LED_HEIGHT * sizeof(CRGB) + GUARD_BYTES];
static CRGB *leds = (CRGB *)(ledsBacking + GUARD_BYTES);

static int checkLedGuards(void)
{
    for (int i = 0; i < GUARD_BYTES; i++)
    {
        if (ledsBacking[i] != GUARD_PATTERN)
            return 0;
        if (ledsBacking[GUARD_BYTES + LED_WIDTH * LED_HEIGHT * (int)sizeof(CRGB) + i] != GUARD_PATTERN)
            return 0;
    }
    return 1;
}

/* Deterministic, non-trivial rand() -- unlike a fixed mock always
   returning the same value, this exercises drawBall()'s index math
   (xc/yc/r-derived) against genuinely varying inputs, the same as the
   real hardware's rand() would, just reproducibly. */
static uint32_t target_rand(uint32_t s)
{
    static uint32_t state = 12345;
    state = state * 1103515245u + 12345u;
    return (state >> 8) % (s + 1);
}

/* Real (not stubbed) HSV->RGB conversion -- same algorithm
   MultiEffectController.ino's own scriptHsv() uses -- so the resulting
   leds[] contents are genuinely varied data, not just three constant
   bytes, and the CRGB-return-by-value ABI (extui-decoded on the script
   side, see visitnode.cpp) gets exercised the same way the real
   hardware binding does. */
static CRGB target_hsv(int h, int s, int v)
{
    CRGB c;
    if (s <= 0)
    {
        c.r = c.g = c.b = (uint8_t)v;
        return c;
    }
    uint8_t region = (uint8_t)h / 43;
    uint8_t remainder = ((uint8_t)h - (region * 43)) * 6;
    uint8_t p = (uint8_t)(((uint32_t)v * (255 - s)) >> 8);
    uint8_t q = (uint8_t)(((uint32_t)v * (255 - (((uint32_t)s * remainder) >> 8))) >> 8);
    uint8_t t = (uint8_t)(((uint32_t)v * (255 - (((uint32_t)s * (255 - remainder)) >> 8))) >> 8);
    switch (region)
    {
    case 0:
        c.r = v;
        c.g = t;
        c.b = p;
        break;
    case 1:
        c.r = q;
        c.g = v;
        c.b = p;
        break;
    case 2:
        c.r = p;
        c.g = v;
        c.b = t;
        break;
    case 3:
        c.r = p;
        c.g = q;
        c.b = v;
        break;
    case 4:
        c.r = t;
        c.g = p;
        c.b = v;
        break;
    default:
        c.r = v;
        c.g = p;
        c.b = q;
        break;
    }
    return c;
}

/* pf(float) -- the second external call ball()'s constructor makes,
   right after printfln(), exactly the sequence that used to trigger
   the stale-register-tracking bug (see optimize.cpp's Pass 4 fix). */
static void target_pf(float v)
{
    (void)v;
}

/* Mirrors runtime_functions.cpp's artiPrintfln() exactly -- genuine C
   varargs, called through the same register-passed "char*,Args"
   convention every other Args-typed external uses. */
static void target_printfln(const char *format, ...)
{
    va_list argp;
    va_start(argp, format);
    vprintf(format, argp);
    printf("\r\n");
    va_end(argp);
}

int main()
{
    memset(ledsBacking, GUARD_PATTERN, sizeof(ledsBacking));

    printf("START ball_stress\n");
    fflush(stdout);

    for (int i = 0; i < NUM_RELOC_SLOTS; i++)
    {
        uint32_t content = (uint32_t)dataRegion + reloc_slot_bincode[i];
        *(uint32_t *)(script_code + reloc_slot_offset[i]) = content;
    }

    for (int i = 0; i < NUM_DATA3_RECORDS; i++)
        memcpy(dataRegion + data3_dest_addr[i], data3_blob + data3_blob_offset[i], data3_size[i]);

    for (int i = 0; i < NUM_VAR_SLOTS; i++)
    {
        uint32_t content = 0;
        if (strcmp(var_name[i], "leds") == 0)
            content = (uint32_t)(uintptr_t)leds;
        else
        {
            printf("RESULT unknown external variable '%s' -- runner doesn't implement it\n", var_name[i]);
            fflush(stdout);
            return 1;
        }
        *(uint32_t *)(script_code + var_slot_offset[i]) = content;
        printf("patched var '%s' slot @%u -> %p\n", var_name[i], var_slot_offset[i], (void *)(uintptr_t)content);
    }

    for (int i = 0; i < NUM_EXT_SLOTS; i++)
    {
        uint32_t content = 0;
        if (strcmp(ext_name[i], "rand") == 0)
            content = (uint32_t)(uintptr_t)target_rand;
        else if (strcmp(ext_name[i], "hsv") == 0)
            content = (uint32_t)(uintptr_t)target_hsv;
        else if (strcmp(ext_name[i], "pf") == 0)
            content = (uint32_t)(uintptr_t)target_pf;
        else if (strcmp(ext_name[i], "printfln") == 0)
            content = (uint32_t)(uintptr_t)target_printfln;
        else
        {
            printf("RESULT unknown external '%s' -- runner doesn't implement it\n", ext_name[i]);
            fflush(stdout);
            return 1;
        }
        *(uint32_t *)(script_code + ext_slot_offset[i]) = content;
        printf("patched ext '%s' slot @%u -> %p\n", ext_name[i], ext_slot_offset[i], (void *)(uintptr_t)content);
    }
    fflush(stdout);

#if HAS_FOOTER
    printf("calling footer() -- constructs Balls[200], each via rand()x5 + printfln()+pf()...\n");
    fflush(stdout);
    callVoid(script_code + FOOTER_ENTRY_OFFSET);
    printf("footer() returned OK\n");
    fflush(stdout);
#endif

    printf("calling main() -- clamps num, printfln(), then 200 bounded while-loop iterations "
           "of the 128x96 hsv() pixel fill + 20x updateBall()/drawBall()...\n");
    fflush(stdout);
    callVoid(script_code + MAIN_ENTRY_OFFSET);
    printf("main() returned OK\n");
    fflush(stdout);

    if (!checkLedGuards())
    {
        printf("RESULT FAIL (leds[] guard bytes corrupted -- an out-of-bounds write happened)\n");
        fflush(stdout);
        return 1;
    }

    printf("RESULT PASS\n");
    fflush(stdout);
    return 0;
}
