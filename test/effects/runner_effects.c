/* Bare-metal QEMU runner for one effect script (see run.py): applies the
   relocations in script_bytes.h (from ../fuzz/compile_script.cpp), binds
   the host functions and the leds[] buffer (guarded on both sides), runs
   main(), and prints the frames common.h's show() dumps. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "script_bytes.h"
#include "common.h"

static void callVoid(void *entry)
{
    register void *r8 __asm__("a8") = entry;
    __asm__ volatile("callx8 a8\n\t" : : "r"(r8) : "memory", "a10");
}

static uint8_t dataRegion[DATA_SIZE];
#define GUARD_BYTES 64
#define GUARD_PATTERN 0xA5
static uint8_t ledsBacking[GUARD_BYTES + LED_WIDTH * LED_HEIGHT * sizeof(CRGB) + GUARD_BYTES];
static float target_sin(float a) { return sinf(a); }
static float target_atan2(float y, float x) { return atan2f(y, x); }
static float target_hypot(float x, float y) { return hypotf(x, y); }

int main()
{
    memset(ledsBacking, GUARD_PATTERN, sizeof(ledsBacking));
    leds = (CRGB *)(ledsBacking + GUARD_BYTES);
    memset(leds, 0, LED_WIDTH * LED_HEIGHT * sizeof(CRGB));
    for (int i = 0; i < NUM_RELOC_SLOTS; i++)
        *(uint32_t *)(script_code + reloc_slot_offset[i]) = (uint32_t)dataRegion + reloc_slot_bincode[i];
    for (int i = 0; i < NUM_DATA3_RECORDS; i++)
        memcpy(dataRegion + data3_dest_addr[i], data3_blob + data3_blob_offset[i], data3_size[i]);
    for (int i = 0; i < NUM_VAR_SLOTS; i++)
    {
        if (strcmp(var_name[i], "leds") != 0)
        {
            printf("RESULT unknown external variable %s\n", var_name[i]);
            return 1;
        }
        *(uint32_t *)(script_code + var_slot_offset[i]) = (uint32_t)(uintptr_t)leds;
    }
    for (int i = 0; i < NUM_EXT_SLOTS; i++)
    {
        uint32_t c = 0;
        if (!strcmp(ext_name[i], "rand")) c = (uint32_t)(uintptr_t)target_rand;
        else if (!strcmp(ext_name[i], "hsv")) c = (uint32_t)(uintptr_t)target_hsv;
        else if (!strcmp(ext_name[i], "sin")) c = (uint32_t)(uintptr_t)target_sin;
        else if (!strcmp(ext_name[i], "atan2")) c = (uint32_t)(uintptr_t)target_atan2;
        else if (!strcmp(ext_name[i], "hypot")) c = (uint32_t)(uintptr_t)target_hypot;
        else if (!strcmp(ext_name[i], "millis")) c = (uint32_t)(uintptr_t)target_millis;
        else if (!strcmp(ext_name[i], "show")) c = (uint32_t)(uintptr_t)target_show;
        else
        {
            printf("RESULT unknown external function %s\n", ext_name[i]);
            return 1;
        }
        *(uint32_t *)(script_code + ext_slot_offset[i]) = c;
    }
#if HAS_FOOTER
    callVoid(script_code + FOOTER_ENTRY_OFFSET);
#endif
    callVoid(script_code + MAIN_ENTRY_OFFSET);
    printf("FRAMES %d\n", g_frame);
    for (int i = 0; i < GUARD_BYTES; i++)
        if (ledsBacking[i] != GUARD_PATTERN || ledsBacking[GUARD_BYTES + LED_WIDTH * LED_HEIGHT * 3 + i] != GUARD_PATTERN)
        {
            printf("RESULT FAIL guard\n");
            return 1;
        }
    printf("RESULT DONE\n");
    fflush(stdout);
    return 0;
}
