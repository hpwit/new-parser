/* Shared by runner_effects.c (on the emulated ESP32) and reference.c (on
   the host): the host functions the effect scripts call, and the frame
   dump both sides print, so the two outputs can be compared byte for
   byte. MS_PER_FRAME sets how fast the fake millis() advances (the
   scripts run for 20000 ms); DUMP_EVERY which frames are printed. */
typedef struct
{
    uint8_t r, g, b;
} CRGB;
#define LED_WIDTH 128
#define LED_HEIGHT 96
static uint32_t target_rand(uint32_t s)
{
    static uint32_t state = 12345;
    state = state * 1103515245u + 12345u;
    return (state >> 8) % (s + 1);
}
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
    case 0: c.r = v; c.g = t; c.b = p; break;
    case 1: c.r = q; c.g = v; c.b = p; break;
    case 2: c.r = p; c.g = v; c.b = t; break;
    case 3: c.r = p; c.g = q; c.b = v; break;
    case 4: c.r = t; c.g = p; c.b = v; break;
    default: c.r = v; c.g = p; c.b = q; break;
    }
    return c;
}
static int g_frame = 0;
static CRGB *leds;
#ifndef MS_PER_FRAME
#define MS_PER_FRAME 100
#endif
#ifndef DUMP_EVERY
#define DUMP_EVERY 40
#endif
static uint32_t target_millis(void) { return (uint32_t)g_frame * MS_PER_FRAME; }
static void target_show(void)
{
    g_frame++;
    if (g_frame == 1 || g_frame % DUMP_EVERY == 0)
    {
        printf("FRAME %d\n", g_frame);
        for (int y = 0; y < LED_HEIGHT; y++)
        {
            for (int x = 0; x < LED_WIDTH; x++)
            {
                CRGB c = leds[y * LED_WIDTH + x];
                printf("%02x%02x%02x", c.r, c.g, c.b);
            }
            printf("\n");
        }
    }
}
