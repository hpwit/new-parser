/* Valmora on a Mac: the script compiled as C with the SolarOS bindings
 * stubbed, keys read from a file, frames saved as PPM.
 *
 *   cc -O2 -o host host.c && ./host ../data/valmora.wad keys.txt outdir
 *   cc -O2 -DSCRIPT='"valmoralow.sc"' -o hostlow host.c &&
 *       ./hostlow valmoralow.wad keys.txt outdir [arg]
 *
 * The shots are what the panel shows: at pixel size 2, the top-left 400x240
 * of the frame buffer, each pixel 2x2.
 *
 * keys.txt: tokens separated by spaces or new lines: a letter (z q s d m f
 * p y n, or 1..5), ENTER, ESC, SPACE, wait:N (N reads with no key), and
 * shot:name (saves the last frame shown as outdir/name.ppm). For tests:
 * go:map,x,y  gold:n  level:n  shards:bits  time:n (the world's turn). */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static uint16_t frames[2][800 * 480];
static int back;
uint16_t *display = frames[0];
int stride = 800;
uint8_t *wad;
int wadLoaded;
static uint32_t now;
static long shown;
static char **keys;
static int nkeys, ki, waiting;
static const char *outdir;
static uint32_t seed = 12345;
static int pixel = 1;
int argc_ = 0;
static int arg0 = 0;
static void (*cheat)(const char *t);

static void save(const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", outdir, name);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n800 480\n255\n");
    const uint16_t *b = frames[back ^ 1];
    for (int i = 0; i < 800 * 480; i++) {
        uint16_t s = b[((i / 800) / pixel) * 800 + (i % 800) / pixel];
        uint16_t v = (uint16_t)((s << 8) | (s >> 8));
        unsigned char px[3] = {(unsigned char)(((v >> 11) & 31) * 255 / 31),
                               (unsigned char)(((v >> 5) & 63) * 255 / 63),
                               (unsigned char)((v & 31) * 255 / 31)};
        fwrite(px, 1, 3, f);
    }
    fclose(f);
    printf("shot %s (frame %ld, %u ms)\n", name, shown, now);
}

void updateDisplay(void)
{
    back ^= 1;
    display = frames[back];
    now += 16;
    shown++;
    if (shown > 200000) {
        printf("too many frames\n");
        exit(2);
    }
}
uint32_t millis(void) { return ++now; }
void delay(uint32_t ms) { now += ms; }
uint32_t host_rand(uint32_t n)
{
    seed = seed * 1103515245u + 12345u;
    return n ? (seed >> 8) % n : 0;
}
int keyHeld(int c) { (void)c; return 0; }
void enableGraphicMode(void) {}
void setPixelsize(int n) { pixel = n; }
int arg(int i) { return i == 0 ? arg0 : 0; }
void printfln(const char *f, ...)
{
    va_list a;
    va_start(a, f);
    vprintf(f, a);
    va_end(a);
    printf("\n");
}
int wadFind(const char *name)
{
    int32_t n, dir;
    memcpy(&n, wad + 4, 4);
    memcpy(&dir, wad + 8, 4);
    for (int i = 0; i < n; i++) {
        const uint8_t *e = wad + dir + 16 * i;
        if (strncmp((const char *)e + 8, name, 8) == 0) {
            int32_t p;
            memcpy(&p, e, 4);
            return p;
        }
    }
    printf("no lump %s\n", name);
    exit(3);
}
int readKey(void)
{
    if (waiting > 0) {
        waiting--;
        return 0;
    }
    while (ki < nkeys) {
        char *t = keys[ki++];
        if (!strncmp(t, "shot:", 5)) {
            save(t + 5);
            continue;
        }
        if (strchr(t, ':') && strncmp(t, "wait:", 5)) {
            cheat(t);
            continue;
        }
        if (!strncmp(t, "wait:", 5)) {
            waiting = atoi(t + 5);
            return 0;
        }
        if (!strcmp(t, "ENTER")) return 13;
        if (!strcmp(t, "ESC")) return 27;
        if (!strcmp(t, "SPACE")) return 32;
        return t[0];
    }
    static int k;
    return (k++ & 1) ? 'y' : 27;
}

#ifndef SCRIPT
#define SCRIPT "valmora.sc"
#endif
#define main script_main
#define rand(n) host_rand(n)
#define argc argc_
#include SCRIPT
#undef argc
#undef main

static void do_cheat(const char *t)
{
    int a = 0, b = 0, c = 0;
    if (sscanf(t, "go:%d,%d,%d", &a, &b, &c) == 3)
        loadMap(a, b, c);
    else if (sscanf(t, "gold:%d", &a) == 1)
        gold = a;
    else if (sscanf(t, "level:%d", &a) == 1) {
        xp = xpFor(a - 1);
        levelUp();
    } else if (sscanf(t, "shards:%d", &a) == 1)
        shards = a;
    else if (sscanf(t, "time:%d", &a) == 1)
        worldTurn = a;
    else
        printf("unknown %s\n", t);
    panelDirty = 2;
}

int main(int argc, char **argv)
{
    cheat = do_cheat;
    FILE *f = fopen(argv[1], "rb");
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    wad = malloc(n);
    fread(wad, 1, n, f);
    fclose(f);
    wadLoaded = 1;
    f = fopen(argv[2], "r");
    keys = calloc(100000, sizeof(char *));
    char tok[64];
    while (fscanf(f, "%63s", tok) == 1)
        keys[nkeys++] = strdup(tok);
    fclose(f);
    outdir = argv[3];
    if (argc > 4) {
        argc_ = 1;
        arg0 = atoi(argv[4]);
    }
    int r = script_main();
    printf("script returned %d after %ld frames\n", r, shown);
    return 0;
}
