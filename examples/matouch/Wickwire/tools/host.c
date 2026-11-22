/* Wickwire on a Mac: the script compiled as C (ffloat as float), the
 * SolarOS bindings stubbed, keys from a file, pictures saved as PPM.
 *
 *   cc -O2 -o host host.c -lm && ./host ../data/wickwire.wad keys.txt outdir
 *
 * keys.txt as Lumenfall's: ENTER ESC SPACE y n ...; hold:KEYS:MS with
 * U D L R (the arrows), J jump, F spin; wait:MS; shot:name; level:n; god;
 * end. Time passes 33 ms a picture shown and as the script waits. */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef float ffloat;

static uint16_t frames[2][800 * 480];
static int back;
uint16_t *display = frames[0];
int stride = 800;
uint8_t *wad;
int wadLoaded;
static uint32_t now;
static long shown;
static char **toks;
static int ntoks, ti;
static const char *outdir;
static uint32_t seed = 12345;
static uint32_t holdUntil, waitUntil;
static int heldU, heldD, heldL, heldR, heldJ, heldF;
static int queue[32], qh, qt;
extern int startLevel;
extern int godMode;

static void save(const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", outdir, name);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n400 240\n255\n");
    const uint16_t *b = frames[back ^ 1];
    for (int i = 0; i < 400 * 240; i++) {
        uint16_t s = b[(i / 400) * 800 + (i % 400)];
        uint16_t v = (uint16_t)((s << 8) | (s >> 8));
        unsigned char px[3] = {(unsigned char)(((v >> 11) & 31) * 255 / 31),
                               (unsigned char)(((v >> 5) & 63) * 255 / 63),
                               (unsigned char)((v & 31) * 255 / 31)};
        fwrite(px, 1, 3, f);
    }
    fclose(f);
    printf("shot %s (picture %ld, %u ms)\n", name, shown, now);
}

static void push(int c)
{
    queue[qh] = c;
    qh = (qh + 1) & 31;
}

static void advance(void)
{
    while (now >= holdUntil && now >= waitUntil && ti < ntoks) {
        heldU = heldD = heldL = heldR = heldJ = heldF = 0;
        char *t = toks[ti++];
        if (!strncmp(t, "shot:", 5)) {
            save(t + 5);
        } else if (!strncmp(t, "hold:", 5)) {
            char keys[16];
            int ms = 0;
            sscanf(t + 5, "%15[^:]:%d", keys, &ms);
            for (char *k = keys; *k; k++) {
                if (*k == 'U') { heldU = 1; push('z'); }
                if (*k == 'D') { heldD = 1; push('s'); }
                if (*k == 'L') { heldL = 1; push('q'); }
                if (*k == 'R') { heldR = 1; push('d'); }
                if (*k == 'J') { heldJ = 1; push(' '); }
                if (*k == 'F') { heldF = 1; push('f'); }
            }
            holdUntil = now + ms;
        } else if (!strncmp(t, "wait:", 5)) {
            waitUntil = now + atoi(t + 5);
        } else if (!strncmp(t, "level:", 6)) {
            startLevel = atoi(t + 6) - 1;
        } else if (!strcmp(t, "god")) {
            godMode = 1;
        } else if (!strcmp(t, "end")) {
            printf("end at picture %ld\n", shown);
            exit(0);
        } else if (!strcmp(t, "ENTER")) {
            push(13);
        } else if (!strcmp(t, "ESC")) {
            push(27);
        } else if (!strcmp(t, "SPACE")) {
            push(32);
        } else {
            push(t[0]);
        }
    }
    if (ti >= ntoks && now >= holdUntil && now >= waitUntil) {
        printf("keys done at picture %ld\n", shown);
        exit(0);
    }
}

void updateDisplay(void)
{
    back ^= 1;
    display = frames[back];
    now += 33;
    shown++;
    if (shown > 400000) {
        printf("too many pictures\n");
        exit(2);
    }
    advance();
}
uint32_t millis(void) { return now; }
void delay(uint32_t ms)
{
    now += ms ? ms : 1;
    advance();
}
uint32_t host_rand(uint32_t n)
{
    seed = seed * 1103515245u + 12345u;
    return n ? (seed >> 8) % n : 0;
}
int keyHeld(int c)
{
    if (c == 'z') return heldU;
    if (c == 's') return heldD;
    if (c == 'q') return heldL;
    if (c == 'd') return heldR;
    if (c == ' ') return heldJ;
    if (c == 'f') return heldF;
    return 0;
}
int readKey(void)
{
    if (qt == qh)
        return 0;
    int c = queue[qt];
    qt = (qt + 1) & 31;
    return c;
}
void enableGraphicMode(void) {}
void setPixelsize(int n) { (void)n; }
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

#define main script_main
#define rand(n) host_rand(n)
#include "wickwire.sc"
#undef main

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    wad = malloc(n);
    fread(wad, 1, n, f);
    fclose(f);
    wadLoaded = 1;
    f = fopen(argv[2], "r");
    toks = calloc(100000, sizeof(char *));
    char tok[64];
    while (fscanf(f, "%63s", tok) == 1)
        toks[ntoks++] = strdup(tok);
    fclose(f);
    outdir = argv[3];
    int r = script_main();
    printf("script returned %d after %ld pictures\n", r, shown);
    return 0;
}
