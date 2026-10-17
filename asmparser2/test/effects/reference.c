/* C versions of the effect scripts, written to compute exactly what the
   scripts do (same integer arithmetic, same tables, same host functions
   from common.h), so their frame dumps must match the QEMU run of the
   compiled script byte for byte. See run.py. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "common.h"
#define width 128
#define height 96
#define rows 98
static CRGB buf[height * width];
static uint8_t wave[256]; static CRGB palette[256]; static uint8_t heat[rows][width];
static void plasma(void)
{
    for (int i = 0; i < 256; i++) { float a = i * 0.02454369f; wave[i] = (int)(sinf(a) * 127.0f + 128.0f); palette[i] = target_hsv(i, 255, 255); }
    uint32_t start = target_millis(); int t = 0;
    while (target_millis() - start < 20000)
    {
        int cx = wave[t % 256] / 2; int cy = wave[(t * 2 + 64) % 256] * 3 / 8;
        for (int y = 0; y < height; y++)
        {
            int wy = wave[(y * 3 + t * 2) % 256]; int dy = (y - cy) * (y - cy);
            for (int x = 0; x < width; x++)
            {
                int d = ((x - cx) * (x - cx) + dy) / 32;
                int v = wave[(x * 2 + t) % 256] + wy;
                v = v + wave[(x + y + t * 3) % 256];
                v = v + wave[(d + t * 4) % 256];
                leds[y * width + x] = palette[(v / 2 + t) % 256];
            }
        }
        target_show(); t++;
    }
}
static void fire(void)
{
    for (int i = 0; i < 256; i++)
    {
        CRGB c;
        if (i < 64) { c.r = i * 4; c.g = 0; c.b = 0; }
        else if (i < 160) { c.r = 255; c.g = (i - 64) * 8 / 3; c.b = 0; }
        else { c.r = 255; c.g = 255; c.b = (i - 160) * 8 / 3; }
        palette[i] = c;
    }
    memset(heat, 0, sizeof(heat));
    uint32_t start = target_millis();
    while (target_millis() - start < 20000)
    {
        for (int x = 0; x < width; x++) { int ember = heat[height + 1][x]; heat[height + 1][x] = ember * 13 / 16; }
        for (int b = 0; b < 4; b++)
        {
            int x0 = target_rand(127); int w = target_rand(8); w = w + 2;
            for (int k = 0; k < w; k++) heat[height + 1][(x0 + k) % width] = 255;
        }
        for (int x = 0; x < width; x++) heat[height][x] = heat[height + 1][x];
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++)
            {
                int xl = (x + width - 1) % width, xr = (x + 1) % width;
                int s = heat[y + 1][xl] + heat[y + 1][x] + heat[y + 1][xr] + heat[y + 2][x];
                int v = s * 32 / 130;
                heat[y][x] = v; leds[y * width + x] = palette[v];
            }
        target_show();
    }
}
static void fixes(void)
{
    static uint8_t h2[height][width];
    for (int i = 0; i < 256; i++)
    {
        CRGB c;
        if (i < 64) { c.r = i * 4; c.g = 0; c.b = 0; }
        else if (i < 160) { c.r = 255; c.g = (i - 64) * 8 / 3; c.b = 0; }
        else if (i < 200) { c.r = 255; c.g = 255; c.b = (i - 160) * 8 / 3; }
        else { c.r = i; c.g = i; c.b = 255; }
        palette[i] = c;
    }
    for (int y = 0; y < 32; y++) for (int x = 0; x < width; x++) { h2[y][x] = target_rand(200) + 20; leds[y * width + x] = palette[h2[y][x]]; }
    for (int y = 32; y < 64; y++) for (int x = 0; x < width; x++) leds[y * width + x] = target_hsv(x + y, 255, 255);
    for (int y = 64; y < 80; y++) for (int x = 0; x < width; x++) { h2[y][x] = target_rand(255); leds[y * width + x] = palette[h2[y][x]]; }
    for (int y = 80; y < height; y++) for (int x = 0; x < width; x++) { h2[y][x] = 20 + target_rand(200); leds[y * width + x] = palette[h2[y][x]]; }
    target_show();
}
#define NSEEDS 9
static int sx[NSEEDS], sy[NSEEDS], ringIn[NSEEDS], ringOut[NSEEDS], gap[NSEEDS][NSEEDS];
static int isqrt_(int v) { int r = 0; while ((r + 1) * (r + 1) <= v) r++; return r; }
static void voronoi(void)
{
    for (int i = 0; i < 256; i++) { float a = i * 0.02454369f; wave[i] = (int)(sinf(a) * 127.0f + 128.0f); }
    uint32_t start = target_millis(); int t = 0;
    while (target_millis() - start < 20000)
    {
        for (int i = 0; i < NSEEDS; i++)
        {
            sx[i] = wave[(t * (i % 3 + 1) / 2 + i * 29) % 256] / 2;
            sy[i] = wave[(t * (i % 2 + 1) / 2 + i * 53 + 64) % 256] * 3 / 8;
            int r = 6 + wave[(t * 3 + i * 40) % 256] / 20;
            ringIn[i] = (r - 1) * (r - 1); ringOut[i] = (r + 1) * (r + 1);
        }
        for (int i = 0; i < NSEEDS; i++) for (int j = 0; j < NSEEDS; j++)
        { int dx = sx[i] - sx[j], dy = sy[i] - sy[j]; gap[i][j] = 2 * isqrt_(dx * dx + dy * dy) + 1; }
        for (int y = 0; y < height; y++) for (int x = 0; x < width; x++)
        {
            int best = 1000000, second = 1000000, bi = 0, si = 0;
            for (int i = 0; i < NSEEDS; i++)
            {
                int dx = x - sx[i], dy = y - sy[i], d = dx * dx + dy * dy;
                if (d < best) { second = best; si = bi; best = d; bi = i; }
                else if (d < second) { second = d; si = i; }
            }
            int edge = (second - best) / gap[bi][si];
            int v = 0, s = 255;
            if (edge < 5) { v = 255 - edge * 40; s = 110 + edge * 29; }
            else if (best < 6) v = 255;
            else if (best >= ringIn[bi] && best <= ringOut[bi]) v = 170;
            CRGB c = {0, 0, 0};
            if (v > 0) c = target_hsv((bi * 28 + t) % 256, s, v);
            leds[y * width + x] = c;
        }
        target_show(); t++;
    }
}
static uint8_t ang[48][width], rad[48][width];
static void mandala(void)
{
    for (int i = 0; i < 256; i++) { float a = i * 0.02454369f; wave[i] = (int)(sinf(a) * 127.0f + 128.0f); }
    for (int y = 0; y < 48; y++) for (int x = 0; x < width; x++)
    {
        float dx = x - 63.5f; float dy = y - 47.5f;
        int turn = (int)(atan2f(dy, dx) * 244.46199f + 768.0f);
        ang[y][x] = turn % 256;
        rad[y][x] = (int)(hypotf(dx, dy));
    }
    uint32_t start = target_millis(); int t = 0;
    while (target_millis() - start < 20000)
    {
        int breath = wave[(t / 2) % 256];
        int swell = 40 + breath / 3;
        int drift = 256 - t % 256;
        for (int y = 0; y < height; y++) for (int x = 0; x < width; x++)
        {
            int ty = y, tx = x;
            if (y >= 48) { ty = height - 1 - y; tx = width - 1 - x; }
            int r = rad[ty][tx];
            int p = (ang[ty][tx] + t / 2) % 256;
            if (p > 127) p = 255 - p;
            int petal = wave[(p * 2 + 192) % 256];
            int q = (r * 5 + petal * swell / 256 + drift) % 256;
            int soft = wave[q];
            int b = soft * soft / 255; b = b * b / 255;
            int q2 = (p * 2 + r * 2 + t) % 256;
            int b2 = wave[q2]; b2 = b2 * b2 / 255; b2 = b2 * b2 / 400;
            int hue = r * 2 + petal / 4 + t;
            int v = soft / 6 + 16;
            if (b > v) v = b;
            if (b2 > v) { v = b2; hue = hue + 128; }
            if (r < 3) v = 255;
            leds[y * width + x] = target_hsv(hue % 256, 255 - v / 3, v);
        }
        target_show(); t++;
    }
}
#define NSQ 6
static int qx[NSQ], qy[NSQ], qc[NSQ], qs[NSQ], qsize[NSQ];
static void squares(void)
{
    for (int i = 0; i < 256; i++) { float a = i * 0.02454369f; wave[i] = (int)(sinf(a) * 127.0f + 128.0f); }
    uint32_t start = target_millis(); int t = 0;
    while (target_millis() - start < 20000)
    {
        for (int i = 0; i < NSQ; i++)
        {
            int orbit = (t + i * 43) % 256;
            int reach = 24 + (i % 2) * 16;
            int oc = wave[(orbit + 64) % 256] - 128;
            int os = wave[orbit] - 128;
            qx[i] = 64 + reach * oc / 127;
            qy[i] = 48 + reach * os / 160;
            int spin = (t * 3 + i * 40) % 256;
            if (i % 2 == 1) spin = 255 - spin;
            qc[i] = wave[(spin + 64) % 256] - 128;
            qs[i] = wave[spin] - 128;
            qsize[i] = (9 + wave[(t * 2 + i * 60) % 256] / 40) * 127;
        }
        int flow = 256 - (t * 2) % 256;
        for (int y = 0; y < height; y++) for (int x = 0; x < width; x++)
        {
            int dx = x - 64; int dy = y - 48;
            int twist = (t + (dx * dx + dy * dy) / 48) % 256;
            int c = wave[(twist + 64) % 256] - 128;
            int s = wave[twist] - 128;
            int u = dx * c + dy * s;
            int w = dy * c - dx * s;
            if (u < 0) u = 0 - u;
            if (w < 0) w = 0 - w;
            int m = u; if (w > m) m = w;
            m = m / 127;
            int band = wave[(m * 10 + flow) % 256];
            int v = band * band / 520;
            int hue = m * 3 + t;
            int sat = 255;
            for (int i = 0; i < NSQ; i++)
            {
                dx = x - qx[i]; dy = y - qy[i];
                u = dx * qc[i] + dy * qs[i];
                w = dy * qc[i] - dx * qs[i];
                int across = u;
                if (u < 0) u = 0 - u;
                if (w < 0) w = 0 - w;
                m = u; if (w > m) m = w;
                if (m < qsize[i])
                {
                    int g = m * 255 / qsize[i];
                    v = 110 + g * 145 / 255;
                    hue = i * 43 + 64 + across * 56 / qsize[i] + t * 2;
                    sat = 255 - g * g / 600;
                }
            }
            leds[y * width + x] = target_hsv((hue + 512) % 256, sat, v);
        }
        target_show(); t++;
    }
}
static int mandel_iter_fixed(int x1, int y1)
{
    int x0 = x1 * 11059 / width - 8602, y0 = y1 * 9011 / height - 4506;
    int x = 0, y = 0, x2 = 0, y2 = 0, iter = 0;
    while (iter < 50 && x2 + y2 <= 4 * 4096)
    { int fX = x2 - y2 + x0; y = 2 * x * y / 4096 + y0; x = fX; x2 = x * x / 4096; y2 = y * y / 4096; iter++; }
    return iter;
}
static void mandel(void)
{
    for (int i = 0; i < width; i++) for (int j = 0; j < height; j++)
    {
        int it = mandel_iter_fixed(i, j);
        CRGB c = {0, 0, 0};
        if (it < 50) c = target_hsv(it * 255 / 50, 255, 255);
        leds[j * width + i] = c;
    }
    target_show();
}
int main(int argc, char **argv)
{
    leds = buf;
    const char *w = argc > 1 ? argv[1] : "";
    if (!strcmp(w, "mandelbrot")) mandel();
    else if (!strcmp(w, "squares")) squares();
    else if (!strcmp(w, "mandala")) mandala();
    else if (!strcmp(w, "voronoi")) voronoi();
    else if (!strcmp(w, "plasma")) plasma();
    else if (!strcmp(w, "fixes")) fixes();
    else if (!strcmp(w, "fire")) fire();
    else
    {
        printf("unknown effect %s\n", w);
        return 1;
    }
    printf("FRAMES %d\n", g_frame);
    return 0;
}
