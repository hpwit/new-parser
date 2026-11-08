// XenoWing: a horizontal shoot'em up written as an ESPLiveScript script, drawing
// straight into the frame buffer (led16) of the 800x480 RGB panel at 400x240
// (setPixelsize(2)), played with a USB keyboard.
//
// A living manta ship flies over an alien world that scrolls in five layers:
// a sky with a ringed planet, far mountains, a forest of strange trees,
// a crystal ridge, and dark plants in front. Bugs, jellies and horned
// crawlers come in waves and shoot; after a minute the hive mother comes,
// sways and fires spreads and spores. Shots get wider with P orbs (four
// levels), B orbs give spore bombs that clear the screen. Score, high score,
// lives, bombs, power; each stage faster.
//
// The art is in xenowing_assets.h (in flash, RGB565 as the frame buffer wants
// it, magenta transparent), made by tools/assets.py (python3 tools/assets.py
// xenowing_assets.h defines.txt .); the script gets it through bound pointers
// (gfx, sky, farRidge, forest, nearRidge, frontPlants) and knows where each
// sprite is from the #defines assets.py writes, pasted at its top.
//
// Until Space is pressed, the game plays itself (demo); then:
//   Z up, Q left, S down, D right (or the arrows): the ship keeps going that
//   way (the keyboard sends a key when it goes down, not while it is held);
//   the same direction again stops it. The ship fires by itself. Space: a
//   spore bomb. Escape gives the game up (back to the demo).
// After the game is over, Space starts a new one.
//
// The keys: the keyboard library calls print_char() on its USB task for
// every key; it only stores the character, and the script takes them with
// readKey() once per frame, so all the script's code runs on one task.
//
// Needs the modified LovyanGFX Bus_RGB driver (double frame buffer with show(),
// _frame_buffer, pixel_size, offset), FastLED (CHSV) and the ESP32_USB_Host_HID
// library with the key hook (see the README's "A USB keyboard driving a
// script"). Matouch7.h is the panel configuration (Waveshare ESP32-S3-Touch-LCD
// 800x480 pinout).
//
// For the ESP32-S3-Touch-LCD-7: a 7-inch 800x480 RGB touch screen with an
// ESP32-S3 (16MB flash, 8MB OPI PSRAM). The keyboard goes on its USB host port.
// Board: ESP32S3 Dev Module, PSRAM "OPI PSRAM", Flash Size 16MB,
// Partition Scheme "16M Flash (3MB APP/9.9MB FATFS)" (the art takes 650 KB).
#include "Matouch7.h"
#include "FastLED.h"
#include "keyboard.h"
#include "script_executable.h"
#include "binding.h"
#include "xenowing_assets.h"

LGFX lcd;
// the art in flash, read by the script through these pointers
static uint16_t *gfxP = (uint16_t *)gfx, *skyP = (uint16_t *)sky, *farP = (uint16_t *)farRidge;
static uint16_t *forestP = (uint16_t *)forest, *nearP = (uint16_t *)nearRidge, *frontP = (uint16_t *)frontPlants;
uint16_t *frame_buffer;
static uint32_t _sizex = 800;
static uint32_t _sizey = 480;

// HSV -> RGB565, byte swapped like LovyanGFX's frame buffer
static uint16_t hsv16(uint8_t h, uint8_t s, uint8_t v) {
  CRGB d = CHSV(h, s, v);
  uint16_t j = lcd.color565(d.red, d.green, d.blue);
  return ((j & 0xff) << 8) | ((j & 0xFF00) >> 8);
}

void show16() {
  lcd._bus_instance.show();
  frame_buffer = (uint16_t *)lcd._bus_instance._frame_buffer;
  lcd._panel_instance.resetWriteBuffer();
}

void clearscreen() {
  memset(frame_buffer, 0, 800 * 480 * 2);
}

// pixel_size h: each cell is shown as h x h pixels, width x height cells
void setPixelsize(int h) {
  clearscreen();
  show16();
  clearscreen();
  lcd._bus_instance.offset = 0;
  lcd._bus_instance.pixel_size = h;
  _sizex = 800 / h;
  _sizey = 480 / h;
}

uint32_t scriptMillis() {
  return millis();
}

uint32_t scriptRand(uint32_t n) {
  return esp_random() % n;
}

// the keys, from the keyboard's task to the script's
#define KEY_QUEUE 16
static volatile uint8_t keyQueue[KEY_QUEUE];
static volatile uint8_t keyHead = 0, keyTail = 0;

void print_char(key_return k) {
  // Escape (no character of its own) quits the game; the arrows are Z Q S D
  uint8_t c = k.key_char;
  if (k.key_code == HID_KEY_ESC)
    c = 27;
  else if (k.key_code == HID_KEY_UP)
    c = 'z';
  else if (k.key_code == HID_KEY_LEFT)
    c = 'q';
  else if (k.key_code == HID_KEY_DOWN)
    c = 's';
  else if (k.key_code == HID_KEY_RIGHT)
    c = 'd';
  if (c == 0)
    return;
  uint8_t next = (keyHead + 1) % KEY_QUEUE;
  if (next != keyTail) {  // full: the key is dropped
    keyQueue[keyHead] = c;
    keyHead = next;
  }
}

int readKey() {
  if (keyTail == keyHead)
    return 0;
  int c = keyQueue[keyTail];
  keyTail = (keyTail + 1) % KEY_QUEUE;
  return c;
}

char xenoWing[] = R"EOF(
// the frame buffer's width, whatever the pixel size
#define STRIDE 800
#define SW 400
#define SH 240
// the transparent colour of the art (magenta, byte swapped)
#define KEY 8184
// positions are in sixteenths of a pixel
#define FP 16
// where the layers are drawn
#define FAR_Y 90
#define FOREST_Y 96
#define NEAR_Y 184
#define FRONT_Y 180
// pools
#define SHOTS 24
#define ENEMIES 16
#define BULLETS 40
#define BOOMS 12
#define PICKS 4
#define MOTES 24
// a step is 1/60 s
#define STEP_MS 16
// the stage: waves for this long, then the boss
#define STAGE_STEPS 3600

// where each sprite is in gfx[]: offset, width, height (assets.py)
#define PLAYER0 0
#define PLAYER0_W 36
#define PLAYER0_H 22
#define PLAYER1 792
#define PLAYER1_W 36
#define PLAYER1_H 22
#define BUG0 1584
#define BUG0_W 22
#define BUG0_H 18
#define BUG1 1980
#define BUG1_W 22
#define BUG1_H 18
#define JELLY0 2376
#define JELLY0_W 20
#define JELLY0_H 26
#define JELLY1 2896
#define JELLY1_W 20
#define JELLY1_H 26
#define CRAWL0 3416
#define CRAWL0_W 28
#define CRAWL0_H 20
#define CRAWL1 3976
#define CRAWL1_W 28
#define CRAWL1_H 20
#define BOSS0 4536
#define BOSS0_W 84
#define BOSS0_H 66
#define BOSS1 10080
#define BOSS1_W 84
#define BOSS1_H 66
#define SHOT 15624
#define SHOT_W 14
#define SHOT_H 5
#define SHOT2 15694
#define SHOT2_W 8
#define SHOT2_H 4
#define EBUL 15726
#define EBUL_W 8
#define EBUL_H 8
#define SPORE 15790
#define SPORE_W 9
#define SPORE_H 9
#define POWER 15871
#define POWER_W 15
#define POWER_H 15
#define BOMB 16096
#define BOMB_W 15
#define BOMB_H 15
#define ICON 16321
#define ICON_W 18
#define ICON_H 11
#define EXPL0 16519
#define EXPL0_W 32
#define EXPL0_H 32
#define EXPL1 17543
#define EXPL1_W 32
#define EXPL1_H 32
#define EXPL2 18567
#define EXPL2_W 32
#define EXPL2_H 32
#define EXPL3 19591
#define EXPL3_W 32
#define EXPL3_H 32
#define EXPL4 20615
#define EXPL4_W 32
#define EXPL4_H 32
#define EXPL5 21639
#define EXPL5_W 32
#define EXPL5_H 32
#define FAR_W 512
#define FAR_H 150
#define FAR_SOLID 59
#define FOREST_W 512
#define FOREST_H 150
#define FOREST_SOLID 150
#define NEAR_W 512
#define NEAR_H 80
#define NEAR_SOLID 61
#define FRONT_W 640
#define FRONT_H 60
#define FRONT_SOLID 60


int sinT[64] = {0, 6, 12, 19, 24, 30, 36, 41, 45, 49, 53, 56, 59, 61, 63, 64, 64, 64, 63, 61, 59, 56, 53, 49, 45, 41, 36, 30, 24, 19, 12, 6, 0, -6, -12, -19, -24, -30, -36, -41, -45, -49, -53, -56, -59, -61, -63, -64, -64, -64, -63, -61, -59, -56, -53, -49, -45, -41, -36, -30, -24, -19, -12, -6};

// 3x5 pixel font, 0-9 then A-Z
int font[36] = {31599, 11415, 29671, 29647, 23497, 31183, 31215, 29330, 31727, 31695,
                11245, 27566, 14627, 27502, 31143, 31140, 14699, 23533, 29847, 4714,
                23861, 18727, 24557, 27501, 11114, 27556, 11123, 27565, 14478, 29842,
                23407, 23402, 23549, 23213, 23186, 29351};

// the player
int px;
int py;
int pvx;
int pvy;
int power;
int bombs;
int lives;
int invincible;
int fireTimer;
int dead;

// shots: x, y, speed, kind (0 main, 1 small)
int sx[24];
int sy[24];
int svx[24];
int svy[24];
int son[24];
int skind[24];

// enemies: kind 0 bug, 1 jelly, 2 crawler
int ex[16];
int ey[16];
int eon[16];
int ekind[16];
int ehp[16];
int eage[16];
int ebase[16];
int espeed[16];
int efire[16];
int eflash[16];

// enemy bullets: kind 0 pink, 1 spore
int bx[40];
int by[40];
int bvx[40];
int bvy[40];
int bon[40];
int bkind[40];

// explosions
int ox[12];
int oy[12];
int oage[12];

// pickups: kind 0 power, 1 bomb
int kx[4];
int ky[4];
int kon[4];
int kkind[4];

// motes drifting up in the air
int mx[24];
int my[24];
int mspeed[24];

// the boss
int bossOn;
int bossX;
int bossY;
int bossHp;
int bossMax;
int bossAge;
int bossFlash;

int score;
int high;
int stage;
int stageTime;
int phase;                   // 0 stage title, 1 playing, 2 boss warning, 3 cleared, 4 game over
int phaseTimer;
int demo;
int frame;
int bombFlash;
int farScroll;
int forestScroll;
int nearScroll;
int frontScroll;
int waveTimer;
int waveCount;

uint16_t white;
uint16_t grey;
uint16_t yellow;
uint16_t red;
uint16_t cyan;
uint16_t pink;
uint16_t green;
uint16_t black;
uint16_t moteColor;
uint16_t barBack;

int sine(int a)
{
   return sinT[a & 63];
}

void makeColors()
{
   white = hsv2(0, 0, 255);
   grey = hsv2(200, 40, 190);
   yellow = hsv2(50, 220, 255);
   red = hsv2(0, 240, 255);
   cyan = hsv2(128, 200, 255);
   pink = hsv2(224, 180, 255);
   green = hsv2(96, 220, 230);
   black = hsv2(0, 0, 0);
   moteColor = hsv2(210, 90, 255);
   barBack = hsv2(200, 200, 60);
}

void fillRect(int x, int y, int w, int h, uint16_t c)
{
   if (x < 0)
   {
      w = w + x;
      x = 0;
   }
   if (x + w > SW)
      w = SW - x;
   if (y < 0)
   {
      h = h + y;
      y = 0;
   }
   if (y + h > SH)
      h = SH - y;
   for (int j = 0; j < h; j++)
   {
      int row = (y + j) * STRIDE + x;
      for (int i = 0; i < w; i++)
         led16[row + i] = c;
   }
}

// a sprite of gfx[] at (x, y), its transparent pixels left out
void drawSprite(int base, int w, int h, int x, int y)
{
   int i0 = 0;
   int i1 = w;
   if (x < 0)
      i0 = -x;
   if (x + w > SW)
      i1 = SW - x;
   if (i0 >= i1)
      return;
   for (int j = 0; j < h; j++)
   {
      int yy = y + j;
      if (yy >= 0 && yy < SH)
      {
         int row = yy * STRIDE + x;
         int src = base + j * w;
         for (int i = i0; i < i1; i++)
         {
            uint16_t c = gfx[src + i];
            if (c != KEY)
               led16[row + i] = c;
         }
      }
   }
}

// the same, all white: a hit
void drawFlash(int base, int w, int h, int x, int y)
{
   for (int j = 0; j < h; j++)
   {
      int yy = y + j;
      if (yy >= 0 && yy < SH)
         for (int i = 0; i < w; i++)
         {
            int xx = x + i;
            if (xx >= 0 && xx < SW && gfx[base + j * w + i] != KEY)
               led16[yy * STRIDE + xx] = white;
         }
   }
}

void drawGlyph(int x, int y, int g, int sc, uint16_t c)
{
   int bits = font[g];
   for (int r = 0; r < 5; r++)
      for (int b = 0; b < 3; b++)
         if ((bits >> ((4 - r) * 3 + 2 - b)) & 1)
            fillRect(x + b * sc, y + r * sc, sc, sc, c);
}

// capitals, digits and spaces
void drawText(int x, int y, char *s, int sc, uint16_t c)
{
   int i = 0;
   while (s[i] != 0)
   {
      int ch = s[i];
      if (ch >= 48 && ch <= 57)
         drawGlyph(x, y, ch - 48, sc, c);
      if (ch >= 65 && ch <= 90)
         drawGlyph(x, y, ch - 55, sc, c);
      x = x + 4 * sc;
      i++;
   }
}

int digits[8];
void drawNumber(int x, int y, int v, int sc, uint16_t c)
{
   int n = 0;
   while (v > 0 || n == 0)
   {
      digits[n] = v % 10;
      v = v / 10;
      n++;
   }
   for (int k = n - 1; k >= 0; k--)
   {
      drawGlyph(x, y, digits[k], sc, c);
      x = x + 4 * sc;
   }
}

// --- the world in layers, back to front ---

void drawSky(int rows)
{
   for (int y = 0; y < rows; y++)
   {
      int row = y * STRIDE;
      int src = y * SW;
      for (int x = 0; x < SW; x++)
         led16[row + x] = sky[src + x];
   }
}

void drawFar(int scroll)
{
   for (int j = 0; j < FAR_H; j++)
   {
      int row = (FAR_Y + j) * STRIDE;
      int src = j * FAR_W;
      int s = scroll;
      for (int x = 0; x < SW; x++)
      {
         uint16_t c = farRidge[src + s];
         if (c != KEY)
            led16[row + x] = c;
         s++;
         if (s == FAR_W)
            s = 0;
      }
   }
}

void drawForest(int scroll)
{
   for (int j = 0; j < SH - FOREST_Y; j++)
   {
      int row = (FOREST_Y + j) * STRIDE;
      int src = j * FOREST_W;
      int s = scroll;
      for (int x = 0; x < SW; x++)
      {
         uint16_t c = forest[src + s];
         if (c != KEY)
            led16[row + x] = c;
         s++;
         if (s == FOREST_W)
            s = 0;
      }
   }
}

void drawNear(int scroll)
{
   for (int j = 0; j < SH - NEAR_Y; j++)
   {
      int row = (NEAR_Y + j) * STRIDE;
      int src = j * NEAR_W;
      int s = scroll;
      for (int x = 0; x < SW; x++)
      {
         uint16_t c = nearRidge[src + s];
         if (c != KEY)
            led16[row + x] = c;
         s++;
         if (s == NEAR_W)
            s = 0;
      }
   }
}

void drawFront(int scroll)
{
   for (int j = 0; j < FRONT_H; j++)
   {
      int row = (FRONT_Y + j) * STRIDE;
      int src = j * FRONT_W;
      int s = scroll;
      for (int x = 0; x < SW; x++)
      {
         uint16_t c = frontPlants[src + s];
         if (c != KEY)
            led16[row + x] = c;
         s++;
         if (s == FRONT_W)
            s = 0;
      }
   }
}

// --- the game ---

void boom(int x, int y)
{
   for (int k = 0; k < BOOMS; k++)
      if (oage[k] < 0)
      {
         ox[k] = x;
         oy[k] = y;
         oage[k] = 0;
         return;
      }
}

void dropPickup(int x, int y)
{
   int r = rand(100);
   if (r >= 14)
      return;
   for (int k = 0; k < PICKS; k++)
      if (!kon[k])
      {
         kon[k] = 1;
         kx[k] = x;
         ky[k] = y;
         kkind[k] = 0;
         if (r < 4)
            kkind[k] = 1;
         return;
      }
}

void addScore(int n)
{
   score = score + n;
   if (score > high)
      high = score;
}

void fireBullet(int x, int y, int vx, int vy, int kind)
{
   for (int b = 0; b < BULLETS; b++)
      if (!bon[b])
      {
         bon[b] = 1;
         bx[b] = x;
         by[b] = y;
         bvx[b] = vx;
         bvy[b] = vy;
         bkind[b] = kind;
         return;
      }
}

// a bullet towards the player at `speed` sixteenths per step
void aimAt(int x, int y, int speed, int kind)
{
   int dx = px + 18 * FP - x;
   int dy = py + 11 * FP - y;
   int ax = dx;
   if (ax < 0)
      ax = -ax;
   int ay = dy;
   if (ay < 0)
      ay = -ay;
   // the length, near enough: the larger side plus half the smaller
   int len = ax + ay / 2;
   if (ay > ax)
      len = ay + ax / 2;
   if (len < 1)
      len = 1;
   fireBullet(x, y, dx * speed / len, dy * speed / len, kind);
}

void spawn(int kind, int x, int y, int speed)
{
   for (int e = 0; e < ENEMIES; e++)
      if (!eon[e])
      {
         eon[e] = 1;
         ekind[e] = kind;
         ex[e] = x * FP;
         ey[e] = y * FP;
         ebase[e] = y * FP;
         espeed[e] = speed;
         eage[e] = 0;
         eflash[e] = 0;
         efire[e] = 40 + rand(80);
         ehp[e] = 2 + kind * 2 + stage;
         return;
      }
}

// the waves, one after the other
void nextWave()
{
   int faster = (stage - 1) * 4;
   int w = waveCount % 5;
   if (w == 0)
   {
      // a line of bugs on a wave
      int y = 40 + rand(110);
      for (int k = 0; k < 5; k++)
         spawn(0, 410 + k * 26, y, 22 + faster);
   }
   if (w == 1)
   {
      for (int k = 0; k < 3; k++)
         spawn(1, 410 + k * 50, 40 + rand(120), 10 + faster);
   }
   if (w == 2)
   {
      for (int k = 0; k < 4; k++)
         spawn(2, 410 + k * 40, 150 + rand(30), 34 + faster);
   }
   if (w == 3)
   {
      // a V of bugs
      for (int k = 0; k < 5; k++)
      {
         int d = k - 2;
         if (d < 0)
            d = -d;
         spawn(0, 410 + d * 22, 70 + k * 18, 24 + faster);
      }
   }
   if (w == 4)
   {
      spawn(1, 420, 60, 12 + faster);
      spawn(1, 420, 150, 12 + faster);
      for (int k = 0; k < 3; k++)
         spawn(0, 440 + k * 24, 105, 26 + faster);
   }
   waveCount++;
}

void resetPlayer()
{
   px = 50 * FP;
   py = 100 * FP;
   pvx = 0;
   pvy = 0;
   invincible = 150;
   dead = 0;
}

void newGame(int asDemo)
{
   demo = asDemo;
   score = 0;
   lives = 3;
   bombs = 3;
   power = 1;
   stage = 1;
   stageTime = 0;
   waveTimer = 60;
   waveCount = 0;
   bossOn = 0;
   for (int k = 0; k < SHOTS; k++)
      son[k] = 0;
   for (int k = 0; k < ENEMIES; k++)
      eon[k] = 0;
   for (int k = 0; k < BULLETS; k++)
      bon[k] = 0;
   for (int k = 0; k < BOOMS; k++)
      oage[k] = -1;
   for (int k = 0; k < PICKS; k++)
      kon[k] = 0;
   resetPlayer();
   phase = 0;
   phaseTimer = 120;
}

void bomb()
{
   if (bombs <= 0 || dead)
      return;
   bombs--;
   bombFlash = 30;
   for (int b = 0; b < BULLETS; b++)
      bon[b] = 0;
   for (int e = 0; e < ENEMIES; e++)
      if (eon[e])
      {
         eon[e] = 0;
         boom(ex[e] / FP, ey[e] / FP);
         addScore(100);
      }
   if (bossOn)
   {
      bossHp = bossHp - 20;
      bossFlash = 10;
   }
}

void shoot()
{
   int x = px + 30 * FP;
   int y = py + 9 * FP;
   for (int k = 0; k < SHOTS; k++)
      if (!son[k])
      {
         son[k] = 1;
         sx[k] = x;
         sy[k] = y;
         svx[k] = 96;
         svy[k] = 0;
         skind[k] = 0;
         if (power >= 2)
            sy[k] = y - 4 * FP;
         break;
      }
   if (power >= 2)
      for (int k = 0; k < SHOTS; k++)
         if (!son[k])
         {
            son[k] = 1;
            sx[k] = x;
            sy[k] = y + 4 * FP;
            svx[k] = 96;
            svy[k] = 0;
            skind[k] = 0;
            break;
         }
   if (power >= 3)
      for (int d = -1; d <= 1; d = d + 2)
         for (int k = 0; k < SHOTS; k++)
            if (!son[k])
            {
               son[k] = 1;
               sx[k] = x - 6 * FP;
               sy[k] = y + 1 * FP;
               svx[k] = 84;
               svy[k] = d * 22;
               skind[k] = 1;
               break;
            }
}

void hitPlayer()
{
   if (invincible > 0 || dead)
      return;
   boom(px / FP + 6, py / FP - 4);
   boom(px / FP + 14, py / FP);
   dead = 90;
   // the demo shows the game: it goes on to the boss whatever happens
   if (!demo)
      lives--;
   if (power > 1)
      power--;
}

// the demo pilot: in line with the nearest enemy, away from the bullets
void autopilot()
{
   int targetY = 100 * FP;
   int best = 100000;
   for (int e = 0; e < ENEMIES; e++)
      if (eon[e] && ex[e] > px && ex[e] - px < best)
      {
         best = ex[e] - px;
         targetY = ey[e] - 2 * FP;
      }
   if (bossOn)
      targetY = bossY + 26 * FP;
   for (int k = 0; k < PICKS; k++)
      if (kon[k] && kx[k] < px + 160 * FP)
         targetY = ky[k];
   int targetX = 60 * FP;
   for (int b = 0; b < BULLETS; b++)
      if (bon[b])
      {
         int dx = bx[b] - px - 18 * FP;
         int dy = by[b] - py - 11 * FP;
         if (dx > -20 * FP && dx < 60 * FP && dy > -18 * FP && dy < 18 * FP)
         {
            if (dy > 0)
               targetY = py - 30 * FP;
            else
               targetY = py + 30 * FP;
            targetX = px - 10 * FP;
         }
      }
   pvy = 0;
   if (targetY < py - 3 * FP)
      pvy = -1;
   if (targetY > py + 3 * FP)
      pvy = 1;
   pvx = 0;
   if (targetX < px - 4 * FP)
      pvx = -1;
   if (targetX > px + 4 * FP)
      pvx = 1;
   if (eon[0] && rand(400) == 0)
      bomb();
}

// one step of 1/60 s
void step()
{
   farScroll = farScroll + 4;
   forestScroll = forestScroll + 10;
   nearScroll = nearScroll + 16;
   frontScroll = frontScroll + 26;
   for (int m = 0; m < MOTES; m++)
   {
      my[m] = my[m] - mspeed[m];
      mx[m] = mx[m] - 6;
      if (my[m] < 0 || mx[m] < 0)
      {
         mx[m] = (rand(SW) + 40) * FP;
         my[m] = (SH + rand(40)) * FP;
         mspeed[m] = 3 + rand(8);
      }
   }
   if (bombFlash > 0)
      bombFlash--;
   for (int k = 0; k < BOOMS; k++)
      if (oage[k] >= 0)
      {
         oage[k] = oage[k] + 1;
         ox[k] = ox[k] - 1;
         if (oage[k] >= 24)
            oage[k] = -1;
      }
   if (phase == 0 || phase == 3)
   {
      phaseTimer--;
      if (phaseTimer <= 0)
         phase = 1;
   }
   if (phase == 4)
   {
      phaseTimer--;
      if (phaseTimer <= 0)
         newGame(1);
      return;
   }
   // the player
   if (demo)
      autopilot();
   if (dead > 0)
   {
      dead--;
      if (dead == 0)
      {
         if (lives <= 0)
         {
            phase = 4;
            phaseTimer = 300;
            return;
         }
         resetPlayer();
      }
   }
   else
   {
      px = px + pvx * 34;
      py = py + pvy * 34;
      if (px < 0)
         px = 0;
      if (px > (SW - 40) * FP)
         px = (SW - 40) * FP;
      if (py < 14 * FP)
         py = 14 * FP;
      if (py > (SH - 26) * FP)
         py = (SH - 26) * FP;
      if (invincible > 0)
         invincible--;
      fireTimer--;
      if (fireTimer <= 0)
      {
         shoot();
         fireTimer = 9;
         if (power >= 4)
            fireTimer = 6;
      }
   }
   // shots
   for (int k = 0; k < SHOTS; k++)
      if (son[k])
      {
         sx[k] = sx[k] + svx[k];
         sy[k] = sy[k] + svy[k];
         if (sx[k] > SW * FP || sy[k] < 0 || sy[k] > SH * FP)
            son[k] = 0;
      }
   // waves, then the boss
   if (phase == 1 && !bossOn)
   {
      stageTime++;
      waveTimer--;
      if (waveTimer <= 0 && stageTime < STAGE_STEPS)
      {
         nextWave();
         waveTimer = 150 - stage * 10;
      }
      if (stageTime == STAGE_STEPS + 240)
      {
         phase = 2;
         phaseTimer = 150;
      }
   }
   if (phase == 2)
   {
      phaseTimer--;
      if (phaseTimer <= 0)
      {
         phase = 1;
         bossOn = 1;
         bossX = 420 * FP;
         bossY = 80 * FP;
         bossMax = 120 + stage * 60;
         bossHp = bossMax;
         bossAge = 0;
      }
   }
   // enemies
   for (int e = 0; e < ENEMIES; e++)
      if (eon[e])
      {
         eage[e] = eage[e] + 1;
         if (eflash[e] > 0)
            eflash[e] = eflash[e] - 1;
         ex[e] = ex[e] - espeed[e];
         if (ekind[e] == 0)
            ey[e] = ebase[e] + sine(eage[e] / 2) * 6 * FP / 16;
         if (ekind[e] == 1)
            ey[e] = ebase[e] + sine(eage[e] / 4) * 2 * FP / 16;
         efire[e] = efire[e] - 1;
         if (efire[e] <= 0 && ex[e] < (SW - 20) * FP && ex[e] > px + 40 * FP)
         {
            if (ekind[e] == 1)
               aimAt(ex[e] + 4 * FP, ey[e] + 9 * FP, 26 + stage * 3, 1);
            if (ekind[e] == 2)
               fireBullet(ex[e], ey[e] + 9 * FP, -48 - stage * 4, 0, 0);
            if (ekind[e] == 0 && rand(3) == 0)
               aimAt(ex[e], ey[e] + 8 * FP, 22 + stage * 3, 0);
            efire[e] = 90 + rand(60) - stage * 8;
         }
         if (ex[e] < -40 * FP)
            eon[e] = 0;
      }
   // the boss: comes in, then sways and fires spreads and spores
   if (bossOn)
   {
      bossAge++;
      if (bossFlash > 0)
         bossFlash--;
      if (bossX > 290 * FP)
         bossX = bossX - 12;
      else
      {
         bossY = 80 * FP + sine(bossAge / 3) * 50 * FP / 64;
         int t = bossAge % 120;
         if (t == 30 || t == 50)
            for (int k = -2; k <= 2; k++)
               fireBullet(bossX + 10 * FP, bossY + 33 * FP, -36, k * 9, 0);
         if (t == 90 || t == 100 || t == 110)
            aimAt(bossX + 10 * FP, bossY + 33 * FP, 30 + stage * 3, 1);
      }
      if (bossHp <= 0)
      {
         bossOn = 0;
         for (int k = 0; k < 8; k++)
            boom(bossX / FP + rand(70), bossY / FP + rand(60));
         addScore(5000 * stage);
         stage++;
         stageTime = 0;
         waveCount = 0;
         waveTimer = 200;
         phase = 3;
         phaseTimer = 180;
      }
   }
   // enemy bullets
   for (int b = 0; b < BULLETS; b++)
      if (bon[b])
      {
         bx[b] = bx[b] + bvx[b];
         by[b] = by[b] + bvy[b];
         if (bx[b] < -8 * FP || bx[b] > SW * FP || by[b] < -8 * FP || by[b] > SH * FP)
            bon[b] = 0;
         // the player's heart is small: its core
         int dx = bx[b] + 3 * FP - px - 19 * FP;
         int dy = by[b] + 3 * FP - py - 11 * FP;
         if (bon[b] && dx > -5 * FP && dx < 5 * FP && dy > -4 * FP && dy < 4 * FP)
         {
            bon[b] = 0;
            hitPlayer();
         }
      }
   // shots against enemies and the boss
   for (int k = 0; k < SHOTS; k++)
      if (son[k])
      {
         for (int e = 0; e < ENEMIES; e++)
            if (eon[e] && son[k])
            {
               int w = 20;
               int h = 18;
               if (ekind[e] == 1)
                  h = 24;
               if (ekind[e] == 2)
                  w = 26;
               if (sx[k] + 12 * FP > ex[e] && sx[k] < ex[e] + w * FP && sy[k] + 3 * FP > ey[e] && sy[k] < ey[e] + h * FP)
               {
                  son[k] = 0;
                  ehp[e] = ehp[e] - 1;
                  eflash[e] = 3;
                  if (ehp[e] <= 0)
                  {
                     eon[e] = 0;
                     boom(ex[e] / FP - 4, ey[e] / FP - 6);
                     addScore(100 + ekind[e] * 100);
                     dropPickup(ex[e], ey[e]);
                  }
               }
            }
         if (bossOn && son[k] && sx[k] + 12 * FP > bossX + 8 * FP && sx[k] < bossX + 66 * FP && sy[k] > bossY + 6 * FP && sy[k] < bossY + 60 * FP)
         {
            son[k] = 0;
            bossHp--;
            bossFlash = 3;
            addScore(10);
         }
      }
   // touching an enemy or the boss
   if (!dead && invincible == 0)
   {
      for (int e = 0; e < ENEMIES; e++)
         if (eon[e])
         {
            int dx = ex[e] + 10 * FP - px - 18 * FP;
            int dy = ey[e] + 9 * FP - py - 11 * FP;
            if (dx > -16 * FP && dx < 16 * FP && dy > -10 * FP && dy < 10 * FP)
            {
               eon[e] = 0;
               boom(ex[e] / FP, ey[e] / FP);
               hitPlayer();
            }
         }
      if (bossOn && px + 30 * FP > bossX + 10 * FP && py + 11 * FP > bossY + 6 * FP && py + 11 * FP < bossY + 60 * FP)
         hitPlayer();
   }
   // pickups
   for (int k = 0; k < PICKS; k++)
      if (kon[k])
      {
         kx[k] = kx[k] - 12;
         ky[k] = ky[k] + sine(frame / 2) / 6;
         if (kx[k] < -16 * FP)
            kon[k] = 0;
         int dx = kx[k] - px - 12 * FP;
         int dy = ky[k] - py - 4 * FP;
         if (!dead && dx > -14 * FP && dx < 20 * FP && dy > -14 * FP && dy < 16 * FP)
         {
            kon[k] = 0;
            addScore(200);
            if (kkind[k] == 0 && power < 4)
               power++;
            if (kkind[k] == 1 && bombs < 5)
               bombs++;
         }
      }
}

void handleKey(int k)
{
   if (k == 27)
   {
      // Escape: the game is given up, back to the demo
      if (!demo)
         newGame(1);
      return;
   }
   if (k == 32 && (demo || phase == 4))
   {
      newGame(0);
      return;
   }
   if (demo)
      return;
   // a direction keeps the ship going; the same one again stops it
   if (k == 122 || k == 90)
   {
      if (pvy == -1)
         pvy = 0;
      else
         pvy = -1;
   }
   if (k == 115 || k == 83)
   {
      if (pvy == 1)
         pvy = 0;
      else
         pvy = 1;
   }
   if (k == 113 || k == 81)
   {
      if (pvx == -1)
         pvx = 0;
      else
         pvx = -1;
   }
   if (k == 100 || k == 68)
   {
      if (pvx == 1)
         pvx = 0;
      else
         pvx = 1;
   }
   if (k == 32)
      bomb();
}

void drawWorld()
{
   drawSky(FAR_Y + FAR_SOLID);
   drawFar((farScroll / FP) % FAR_W);
   drawForest((forestScroll / FP) % FOREST_W);
   drawNear((nearScroll / FP) % NEAR_W);
   // motes of light rising
   for (int m = 0; m < MOTES; m++)
   {
      int x = mx[m] / FP;
      int y = my[m] / FP;
      if (x >= 0 && x < SW - 1 && y >= 0 && y < SH - 1)
      {
         led16[y * STRIDE + x] = moteColor;
         if (m % 3 == 0)
            led16[y * STRIDE + x + 1] = moteColor;
      }
   }
}

void drawThings()
{
   for (int k = 0; k < PICKS; k++)
      if (kon[k])
      {
         if (kkind[k] == 0)
            drawSprite(POWER, POWER_W, POWER_H, kx[k] / FP, ky[k] / FP);
         else
            drawSprite(BOMB, BOMB_W, BOMB_H, kx[k] / FP, ky[k] / FP);
      }
   int f = (frame / 8) % 2;
   for (int e = 0; e < ENEMIES; e++)
      if (eon[e])
      {
         int x = ex[e] / FP;
         int y = ey[e] / FP;
         if (ekind[e] == 0)
         {
            if (eflash[e] > 0)
               drawFlash(BUG0, BUG0_W, BUG0_H, x, y);
            else if (f == 0)
               drawSprite(BUG0, BUG0_W, BUG0_H, x, y);
            else
               drawSprite(BUG1, BUG1_W, BUG1_H, x, y);
         }
         if (ekind[e] == 1)
         {
            if (eflash[e] > 0)
               drawFlash(JELLY0, JELLY0_W, JELLY0_H, x, y);
            else if (f == 0)
               drawSprite(JELLY0, JELLY0_W, JELLY0_H, x, y);
            else
               drawSprite(JELLY1, JELLY1_W, JELLY1_H, x, y);
         }
         if (ekind[e] == 2)
         {
            if (eflash[e] > 0)
               drawFlash(CRAWL0, CRAWL0_W, CRAWL0_H, x, y);
            else if (f == 0)
               drawSprite(CRAWL0, CRAWL0_W, CRAWL0_H, x, y);
            else
               drawSprite(CRAWL1, CRAWL1_W, CRAWL1_H, x, y);
         }
      }
   if (bossOn)
   {
      int t = bossAge % 120;
      // hit: a white frame now and then (it is hit all the time)
      if (bossFlash > 0 && (frame % 8) == 0)
         drawFlash(BOSS0, BOSS0_W, BOSS0_H, bossX / FP, bossY / FP);
      else if (t >= 20 && t < 115)
         drawSprite(BOSS1, BOSS1_W, BOSS1_H, bossX / FP, bossY / FP);
      else
         drawSprite(BOSS0, BOSS0_W, BOSS0_H, bossX / FP, bossY / FP);
   }
   for (int b = 0; b < BULLETS; b++)
      if (bon[b])
      {
         if (bkind[b] == 0)
            drawSprite(EBUL, EBUL_W, EBUL_H, bx[b] / FP, by[b] / FP);
         else
            drawSprite(SPORE, SPORE_W, SPORE_H, bx[b] / FP, by[b] / FP);
      }
   for (int k = 0; k < SHOTS; k++)
      if (son[k])
      {
         if (skind[k] == 0)
            drawSprite(SHOT, SHOT_W, SHOT_H, sx[k] / FP, sy[k] / FP);
         else
            drawSprite(SHOT2, SHOT2_W, SHOT2_H, sx[k] / FP, sy[k] / FP);
      }
   if (!dead && phase != 4)
   {
      // blinking while invincible
      if (invincible == 0 || (frame / 3) % 2 == 0)
      {
         int x = px / FP;
         int y = py / FP;
         // the engine's flame
         int fl = 6 + (frame % 4);
         for (int i = 0; i < fl; i++)
         {
            uint16_t c = yellow;
            if (i > 3)
               c = pink;
            fillRect(x - i, y + 10, 1, 2, c);
         }
         if ((frame / 6) % 2 == 0)
            drawSprite(PLAYER0, PLAYER0_W, PLAYER0_H, x, y);
         else
            drawSprite(PLAYER1, PLAYER1_W, PLAYER1_H, x, y);
      }
   }
   for (int k = 0; k < BOOMS; k++)
      if (oage[k] >= 0)
      {
         int fr = oage[k] / 4;
         drawSprite(EXPL0 + fr * EXPL0_W * EXPL0_H, EXPL0_W, EXPL0_H, ox[k] - 8, oy[k] - 8);
      }
   if (bombFlash > 0)
   {
      // rings of the spore bomb
      int r = (30 - bombFlash) * 12;
      for (int a = 0; a < 64; a++)
      {
         int x = px / FP + 18 + sine(a + 16) * r / 64;
         int y = py / FP + 11 + sine(a) * r / 64;
         fillRect(x, y, 3, 3, green);
      }
   }
}

void drawHud()
{
   drawFront((frontScroll / FP) % FRONT_W);
   drawNumber(6, 4, score, 2, white);
   drawText(150, 4, "HI", 2, grey);
   drawNumber(170, 4, high, 2, yellow);
   for (int i = 0; i < lives - 1 && i < 5; i++)
      drawSprite(ICON, ICON_W, ICON_H, 300 + i * 20, 3);
   for (int i = 0; i < bombs && i < 5; i++)
      fillRect(6 + i * 8, 18, 5, 5, green);
   for (int i = 0; i < 4; i++)
   {
      uint16_t c = barBack;
      if (i < power)
         c = cyan;
      fillRect(56 + i * 7, 19, 5, 3, c);
   }
   if (bossOn)
   {
      fillRect(100, 230, 200, 4, barBack);
      fillRect(100, 230, bossHp * 200 / bossMax, 4, red);
   }
   if (phase == 0 || phase == 3)
   {
      if (phase == 3)
         drawText(150, 90, "CLEARED", 3, cyan);
      drawText(150, 110, "STAGE", 3, white);
      drawNumber(214, 110, stage, 3, white);
   }
   if (phase == 2 && (frame / 8) % 2 == 0)
      drawText(140, 100, "WARNING", 4, red);
   if (phase == 4)
      drawText(130, 100, "GAME OVER", 4, red);
   if (demo && (frame / 20) % 2 == 0)
      drawText(158, 214, "PRESS SPACE", 2, white);
}

int main()
{
   setPixelsize(2);           // 400x240, each 2x2 pixels
   makeColors();
   high = 0;
   for (int m = 0; m < MOTES; m++)
   {
      mx[m] = rand(SW) * FP;
      my[m] = rand(SH) * FP;
      mspeed[m] = 3 + rand(8);
   }
   newGame(1);
   uint32_t last = millis();
   while (true)
   {
      int k = readKey();
      while (k != 0)
      {
         handleKey(k);
         k = readKey();
      }
      uint32_t now = millis();
      int n = 0;
      while (now - last >= STEP_MS && n < 4)
      {
         step();
         last = last + STEP_MS;
         n++;
      }
      if (now - last > 100)
         last = now;
      drawWorld();
      drawThings();
      drawHud();
      show16();
      frame++;
   }
}
)EOF";

// lcd.init() on core 0 so Bus_RGB's line copy interrupts stay off core 1, where the script draws
static void lcdInitTask(void *done) {
  lcd.init();
  xTaskNotifyGive((TaskHandle_t)done);
  vTaskDelete(NULL);
}

void setup() {
  Serial.begin(115200);
  xTaskCreatePinnedToCore(lcdInitTask, "lcdInit", 4096, xTaskGetCurrentTaskHandle(), 5, NULL, 0);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  frame_buffer = (uint16_t *)lcd._bus_instance._frame_buffer;

  bindVariable((char *)"uint32_t", (char *)"width", NULL, (void *)&_sizex);
  bindVariable((char *)"uint32_t", (char *)"height", NULL, (void *)&_sizey);
  // a pointer, not an array: show16() swaps the frame buffers
  bindVariable((char *)"uint16_t *", (char *)"led16", NULL, (void *)&frame_buffer);
  bindFunction((char *)"uint16_t", (char *)"hsv2", (char *)"int,int,int", (void *)hsv16);
  bindFunction((char *)"void", (char *)"show16", NULL, (void *)show16);
  bindFunction((char *)"void", (char *)"setPixelsize", (char *)"int", (void *)setPixelsize);
  bindFunction((char *)"uint32_t", (char *)"millis", NULL, (void *)scriptMillis);
  bindFunction((char *)"uint32_t", (char *)"rand", (char *)"uint32_t", (void *)scriptRand);
  bindFunction((char *)"int", (char *)"readKey", NULL, (void *)readKey);
  // the sprites and the five layers of the alien world
  bindVariable((char *)"uint16_t *", (char *)"gfx", NULL, (void *)&gfxP);
  bindVariable((char *)"uint16_t *", (char *)"sky", NULL, (void *)&skyP);
  bindVariable((char *)"uint16_t *", (char *)"farRidge", NULL, (void *)&farP);
  bindVariable((char *)"uint16_t *", (char *)"forest", NULL, (void *)&forestP);
  bindVariable((char *)"uint16_t *", (char *)"nearRidge", NULL, (void *)&nearP);
  bindVariable((char *)"uint16_t *", (char *)"frontPlants", NULL, (void *)&frontP);

  static ScriptExecutable exec = parseScript(xenoWing);
  if (!exec.isExeExists()) {
    showCompileError();
    return;
  }
  printf("xenowing: %u bytes of code\n", (unsigned)exec.exe.binary_size);

  // the USB keyboard: keys go to print_char()
  hid_keyboard_print_char = print_char;
  app_main_keyboard();

  exec.execute("main");
}

void loop() {
  delay(1000);
}
