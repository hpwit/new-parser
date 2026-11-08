// MazeChase: a maze game written as an ESPLiveScript script, drawing straight
// into the frame buffer (led16) of the 800x480 RGB panel at 400x240
// (setPixelsize(2)), played with a USB keyboard.
//
// An orb eats the dots of a maze while four drones hunt it, each its own way:
// crimson rushes at the orb, violet aims 3 tiles ahead of it, teal flanks it
// (the side away from crimson), gold chases from afar and drifts when close.
// They take turns of going to their corners and of chasing. A power cell
// makes them run away and lets the orb eat them for 200, 400, 800, 1600; an
// eaten drone goes back to its base as an eye. An apple shows twice per maze
// (100 x the level). Score, high score, lives and level on the sides; a
// cleared maze starts the next level, a bit faster.
//
// Until Space is pressed, the game plays itself (demo); then:
//   Z up, Q left, S down, D right (ZQSD, for an AZERTY keyboard).
// After the game is over, Space starts a new one; Escape gives a game up
// (back to the demo). The arrows work too.
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
// Board: ESP32S3 Dev Module, PSRAM "OPI PSRAM", Flash Size 16MB.
#include "Matouch7.h"
#include "FastLED.h"
#include "keyboard.h"
#include "script_executable.h"
#include "binding.h"

LGFX lcd;
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

char mazeChase[] = R"EOF(
// the frame buffer's width, whatever the pixel size
#define STRIDE 800
// the maze: 27 x 25 tiles of 8 pixels, its top-left corner on the screen
#define W 27
#define H 25
#define MW 216
#define MX 92
#define MY 20
// a step is 1/60 s
#define STEP_MS 16

// tiles: 0 path, 1 wall, 2 dot, 3 power cell, 4 base door, 5 inside the
// base, 6 tunnel, 7 wall outside the maze
uint8_t mazeStart[675] = {
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
   1, 3, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 3, 1,
   1, 2, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 2, 1, 2, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 2, 1,
   1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
   1, 2, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 2, 1,
   1, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 1,
   1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 2, 1, 2, 1, 2, 1, 2, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1,
   7, 7, 7, 7, 1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 7, 7, 7, 7,
   7, 7, 7, 7, 1, 2, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 2, 1, 7, 7, 7, 7,
   1, 1, 1, 1, 1, 2, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 1, 1, 1, 1, 1,
   1, 1, 1, 1, 1, 2, 1, 1, 1, 0, 1, 1, 1, 4, 1, 1, 1, 0, 1, 1, 1, 2, 1, 1, 1, 1, 1,
   6, 6, 6, 6, 6, 2, 0, 0, 0, 0, 1, 5, 5, 5, 5, 5, 1, 0, 0, 0, 0, 2, 6, 6, 6, 6, 6,
   1, 1, 1, 1, 1, 2, 1, 1, 1, 0, 1, 5, 5, 5, 5, 5, 1, 0, 1, 1, 1, 2, 1, 1, 1, 1, 1,
   1, 1, 1, 1, 1, 2, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 2, 1, 1, 1, 1, 1,
   7, 7, 7, 7, 1, 2, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 1, 7, 7, 7, 7,
   7, 7, 7, 7, 1, 2, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 2, 1, 7, 7, 7, 7,
   7, 7, 7, 7, 1, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 1, 7, 7, 7, 7,
   1, 2, 2, 2, 2, 2, 1, 1, 1, 2, 1, 2, 1, 1, 1, 2, 1, 2, 1, 1, 1, 2, 2, 2, 2, 2, 1,
   1, 2, 1, 1, 1, 2, 1, 1, 1, 2, 1, 2, 1, 1, 1, 2, 1, 2, 1, 1, 1, 2, 1, 1, 1, 2, 1,
   1, 3, 2, 2, 1, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 1, 2, 2, 3, 1,
   1, 1, 1, 2, 1, 2, 1, 2, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 2, 1, 2, 1, 2, 1, 1, 1,
   1, 2, 2, 2, 2, 2, 1, 2, 2, 2, 1, 1, 2, 1, 2, 1, 1, 2, 2, 2, 1, 2, 2, 2, 2, 2, 1,
   1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 2, 1, 2, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1,
   1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
};
uint8_t maze[675];
uint8_t edges[675];        // walls: which sides face a path (1 up, 2 left, 4 down, 8 right)

int dxs[4] = {0, -1, 0, 1};  // up, left, down, right
int dys[4] = {-1, 0, 1, 0};

// 3x5 pixel font, 0-9 then A-Z: 5 rows of 3 bits, the top row first
int font[36] = {31599, 11415, 29671, 29647, 23497, 31183, 31215, 29330, 31727, 31695,
                11245, 27566, 14627, 27502, 31143, 31140, 14699, 23533, 29847, 4714,
                23861, 18727, 24557, 27501, 11114, 27556, 11123, 27565, 14478, 29842,
                23407, 23402, 23549, 23213, 23186, 29351};

// the player: pixel position in the maze (the centre of a tile is at 4, 4)
int px;
int py;
int pdir;
int wantDir;
int pmoving;
int pacc;
int ppulse;

// the four chasers: crimson rushes at the player, violet aims ahead of it,
// teal flanks it (the side away from crimson), gold chases from afar and
// drifts at random when close
int gx[4];
int gy[4];
int gdir[4];
int gstate[4];             // 0 waiting in the base, 1 leaving it, 2 out, 3 eyes going home, 4 entering
int gacc[4];
int gfright[4];
int greverse[4];
int gbob[4];
int ghue[4] = {0, 200, 128, 40};
int gcornerX[4] = {25, 1, 26, 0};
int gcornerY[4] = {-3, -3, 27, 27};
int greleaseAt[4] = {0, 60, 240, 420};

// game state
int score;
int high;
int lives;
int level;
int dotsLeft;
int dotsEaten;
int phase;                 // 0 get set, 1 playing, 2 caught, 3 maze cleared, 4 game over, 5 short pause
int phaseTimer;
int playTime;
int modeIndex;
int modeTimer;
int scatter;
int frightTimer;
int eatChain;
int bonusTimer;
int popupTimer;
int popupX;
int popupY;
int popupValue;
int demo;
int frame;
// waves of corner and chase, in steps; the last one never ends
int modeLength[5] = {300, 1500, 300, 1500, 0};

// colours
uint16_t black;
uint16_t white;
uint16_t grey;
uint16_t wallFill;
uint16_t wallEdge;
uint16_t wallGlow;
uint16_t dotColor;
uint16_t doorColor;
uint16_t orbColor;
uint16_t orbDark;
uint16_t orbLight;
uint16_t frightBody;
uint16_t frightDark;
uint16_t red;
uint16_t green;
uint16_t brown;
uint16_t yellow;
uint16_t cyan;
uint16_t gbody[4];
uint16_t glight[4];
uint16_t gdark[4];
uint16_t title[9];
int clipX0;
int clipX1;

// the search the demo player uses
int bfsQueue[675];
int bfsFirst[675];
int bfsMark[675];
int bfsStamp;

int tileAt(int c, int r)
{
   if (r < 0 || r >= H)
      return 1;
   c = (c + W) % W;
   return maze[r * W + c];
}

// a tile the player and the chasers out of the base can walk on
bool walkable(int t)
{
   if (t == 0 || t == 2 || t == 3 || t == 6)
      return true;
   return false;
}

bool centred(int x, int y)
{
   if (x % 8 == 4 && y % 8 == 4)
      return true;
   return false;
}

int speedOf(int base)
{
   int s = base + (level - 1) * 4;
   if (s > base + 16)
      s = base + 16;
   return s;
}

void resetActors()
{
   px = 13 * 8 + 4;
   py = 19 * 8 + 4;
   pdir = 1;
   wantDir = 1;
   pmoving = 0;
   pacc = 0;
   gx[0] = 13 * 8 + 4;
   gy[0] = 9 * 8 + 4;
   gx[1] = 13 * 8 + 4;
   gx[2] = 11 * 8 + 4;
   gx[3] = 15 * 8 + 4;
   for (int i = 0; i < 4; i++)
   {
      if (i > 0)
         gy[i] = 96;
      gstate[i] = 0;
      gdir[i] = 1;
      gacc[i] = 0;
      gfright[i] = 0;
      greverse[i] = 0;
      gbob[i] = 1;
      if (i == 1)
         gbob[i] = -1;
   }
   gstate[0] = 2;
   frightTimer = 0;
   playTime = 0;
   modeIndex = 0;
   modeTimer = 0;
   scatter = 1;
   bonusTimer = 0;
   popupTimer = 0;
   phase = 0;
   phaseTimer = 120;
}

void resetMaze()
{
   dotsLeft = 0;
   for (int i = 0; i < W * H; i++)
   {
      maze[i] = mazeStart[i];
      if (maze[i] == 2 || maze[i] == 3)
         dotsLeft++;
   }
   dotsEaten = 0;
}

void newGame(int asDemo)
{
   demo = asDemo;
   score = 0;
   lives = 3;
   level = 1;
   resetMaze();
   resetActors();
}

void frighten()
{
   int t = 360 - (level - 1) * 60;
   if (t < 120)
      t = 120;
   frightTimer = t;
   eatChain = 0;
   for (int i = 0; i < 4; i++)
      if (gstate[i] == 1 || gstate[i] == 2)
      {
         gfright[i] = 1;
         if (gstate[i] == 2)
            greverse[i] = 1;
      }
}

void addScore(int n)
{
   score = score + n;
   if (score > high)
      high = score;
}

void eat(int c, int r)
{
   int i = r * W + c;
   int t = maze[i];
   if (t == 2 || t == 3)
   {
      maze[i] = 0;
      dotsLeft--;
      dotsEaten++;
      if (t == 2)
         addScore(10);
      else
      {
         addScore(50);
         frighten();
      }
      if (dotsEaten == 70 || dotsEaten == 170)
         bonusTimer = 570;
      if (dotsLeft == 0)
      {
         phase = 3;
         phaseTimer = 150;
      }
   }
   if (bonusTimer > 0 && c == 13 && r == 16)
   {
      bonusTimer = 0;
      addScore(100 * level);
      popupTimer = 90;
      popupX = px;
      popupY = py;
      popupValue = 100 * level;
   }
}

void playerStep()
{
   int c = px / 8;
   int r = py / 8;
   if (centred(px, py))
   {
      if (walkable(tileAt(c + dxs[wantDir], r + dys[wantDir])))
         pdir = wantDir;
      if (!walkable(tileAt(c + dxs[pdir], r + dys[pdir])))
      {
         pmoving = 0;
         return;
      }
      pmoving = 1;
   }
   else if (wantDir == (pdir + 2) % 4)
      pdir = wantDir;
   px = (px + dxs[pdir] + MW) % MW;
   py = py + dys[pdir];
   ppulse++;
   if (centred(px, py))
      eat(px / 8, py / 8);
}

int chooseDir(int i, int c, int r)
{
   int tx = gcornerX[i];
   int ty = gcornerY[i];
   int pc = px / 8;
   int pr = py / 8;
   bool wander = false;
   if (gfright[i])
      wander = true;
   if (gstate[i] == 3)
   {
      tx = 13;
      ty = 9;
      wander = false;
   }
   else if (!wander && !scatter)
   {
      tx = pc;
      ty = pr;
      if (i == 1)
      {
         tx = pc + dxs[pdir] * 3;
         ty = pr + dys[pdir] * 3;
      }
      if (i == 2)
      {
         tx = pc * 2 - gx[0] / 8;
         ty = pr * 2 - gy[0] / 8;
      }
      if (i == 3 && (c - pc) * (c - pc) + (r - pr) * (r - pr) <= 36)
         wander = true;
   }
   int back = (gdir[i] + 2) % 4;
   int best = -1;
   int bestD = 1000000;
   int first = 0;
   if (wander)
      first = rand(4);
   for (int k = 0; k < 4; k++)
   {
      int d = (first + k) % 4;
      if (d != back && walkable(tileAt(c + dxs[d], r + dys[d])))
      {
         int ex = c + dxs[d] - tx;
         int ey = r + dys[d] - ty;
         int dd = ex * ex + ey * ey;
         if (wander)
            dd = k;
         if (dd < bestD)
         {
            bestD = dd;
            best = d;
         }
      }
   }
   if (best < 0)
      best = back;
   return best;
}

void chaserStep(int i)
{
   int s = gstate[i];
   if (s == 0)
   {
      gy[i] = gy[i] + gbob[i];
      if (gy[i] <= 93 || gy[i] >= 99)
         gbob[i] = -gbob[i];
      return;
   }
   if (s == 1)
   {
      if (gx[i] < 108)
         gx[i] = gx[i] + 1;
      else if (gx[i] > 108)
         gx[i] = gx[i] - 1;
      else if (gy[i] > 76)
         gy[i] = gy[i] - 1;
      else
      {
         gstate[i] = 2;
         gdir[i] = 1;
      }
      return;
   }
   if (s == 4)
   {
      if (gy[i] < 96)
         gy[i] = gy[i] + 1;
      else
      {
         gstate[i] = 1;
         gfright[i] = 0;
      }
      return;
   }
   if (centred(gx[i], gy[i]))
   {
      int c = gx[i] / 8;
      int r = gy[i] / 8;
      if (s == 3 && c == 13 && r == 9)
      {
         gstate[i] = 4;
         return;
      }
      if (greverse[i])
      {
         gdir[i] = (gdir[i] + 2) % 4;
         greverse[i] = 0;
      }
      else
         gdir[i] = chooseDir(i, c, r);
   }
   gx[i] = (gx[i] + dxs[gdir[i]] + MW) % MW;
   gy[i] = gy[i] + dys[gdir[i]];
}

int chaserSpeed(int i)
{
   int s = gstate[i];
   if (s == 3 || s == 4)
      return 200;
   if (s == 0 || s == 1)
      return 50;
   if (tileAt(gx[i] / 8, gy[i] / 8) == 6)
      return 40;
   if (gfright[i])
      return 50;
   return speedOf(75);
}

// a tile next to a chaser that can catch the player
bool nearDanger(int c, int r)
{
   for (int i = 0; i < 4; i++)
      if ((gstate[i] == 1 || gstate[i] == 2) && !gfright[i])
      {
         int dc = c - gx[i] / 8;
         if (dc < 0)
            dc = -dc;
         if (dc > W / 2)
            dc = W - dc;
         int dr = r - gy[i] / 8;
         if (dr < 0)
            dr = -dr;
         if (dc + dr <= 3)
            return true;
      }
   return false;
}

bool worthGoing(int c, int r)
{
   int t = maze[r * W + c];
   if (t == 2 || t == 3)
      return true;
   if (bonusTimer > 60 && c == 13 && r == 16)
      return true;
   for (int i = 0; i < 4; i++)
      if (gfright[i] && gstate[i] == 2 && gx[i] / 8 == c && gy[i] / 8 == r && frightTimer > 60)
         return true;
   return false;
}

// the demo player: the first step of the shortest way to something to eat
// that keeps away from the chasers
void autopilot()
{
   int pc = px / 8;
   int pr = py / 8;
   bfsStamp++;
   int head = 0;
   int tail = 0;
   bfsMark[pr * W + pc] = bfsStamp;
   for (int d = 0; d < 4; d++)
   {
      int nc = (pc + dxs[d] + W) % W;
      int nr = pr + dys[d];
      if (walkable(tileAt(nc, nr)) && !nearDanger(nc, nr))
      {
         int n = nr * W + nc;
         bfsMark[n] = bfsStamp;
         bfsFirst[n] = d;
         bfsQueue[tail] = n;
         tail++;
      }
   }
   while (head < tail)
   {
      int n = bfsQueue[head];
      head++;
      int c = n % W;
      int r = n / W;
      if (worthGoing(c, r))
      {
         wantDir = bfsFirst[n];
         return;
      }
      for (int d = 0; d < 4; d++)
      {
         int nc = (c + dxs[d] + W) % W;
         int nr = r + dys[d];
         if (walkable(tileAt(nc, nr)))
         {
            int m = nr * W + nc;
            if (bfsMark[m] != bfsStamp && !nearDanger(nc, nr))
            {
               bfsMark[m] = bfsStamp;
               bfsFirst[m] = bfsFirst[n];
               bfsQueue[tail] = m;
               tail++;
            }
         }
      }
   }
   // cornered: the way that leads farthest from the nearest chaser
   int best = pdir;
   int bestD = -1;
   for (int d = 0; d < 4; d++)
   {
      int nc = (pc + dxs[d] + W) % W;
      int nr = pr + dys[d];
      if (walkable(tileAt(nc, nr)))
      {
         int nearest = 10000;
         for (int i = 0; i < 4; i++)
            if (gstate[i] == 2 && !gfright[i])
            {
               int ex = nc - gx[i] / 8;
               int ey = nr - gy[i] / 8;
               if (ex * ex + ey * ey < nearest)
                  nearest = ex * ex + ey * ey;
            }
         if (nearest > bestD)
         {
            bestD = nearest;
            best = d;
         }
      }
   }
   wantDir = best;
}

void checkCatch()
{
   for (int i = 0; i < 4; i++)
   {
      int dx = gx[i] - px;
      if (dx < 0)
         dx = -dx;
      if (dx > MW / 2)
         dx = MW - dx;
      int dy = gy[i] - py;
      if (dy < 0)
         dy = -dy;
      if (dx < 5 && dy < 5 && gstate[i] == 2)
      {
         if (gfright[i])
         {
            gfright[i] = 0;
            gstate[i] = 3;
            int v = 200;
            for (int k = 0; k < eatChain; k++)
               v = v * 2;
            eatChain++;
            addScore(v);
            popupTimer = 60;
            popupX = gx[i];
            popupY = gy[i];
            popupValue = v;
            phase = 5;
            phaseTimer = 30;
            return;
         }
         phase = 2;
         phaseTimer = 100;
         return;
      }
   }
}

// one step of 1/60 s
void step()
{
   if (popupTimer > 0)
      popupTimer--;
   if (phase != 1)
   {
      phaseTimer--;
      if (phaseTimer > 0)
         return;
      if (phase == 0 || phase == 5)
         phase = 1;
      else if (phase == 2)
      {
         lives--;
         if (lives == 0)
         {
            phase = 4;
            phaseTimer = 300;
            return;
         }
         resetActors();
      }
      else if (phase == 3)
      {
         level++;
         resetMaze();
         resetActors();
      }
      else if (phase == 4)
         newGame(1);
      return;
   }
   playTime++;
   for (int i = 1; i < 4; i++)
      if (gstate[i] == 0 && playTime >= greleaseAt[i])
         gstate[i] = 1;
   if (frightTimer > 0)
   {
      frightTimer--;
      if (frightTimer == 0)
         for (int i = 0; i < 4; i++)
            gfright[i] = 0;
   }
   else if (modeLength[modeIndex] > 0)
   {
      modeTimer++;
      if (modeTimer >= modeLength[modeIndex])
      {
         modeTimer = 0;
         modeIndex++;
         scatter = 1 - scatter;
         for (int i = 0; i < 4; i++)
            if (gstate[i] == 2)
               greverse[i] = 1;
      }
   }
   if (bonusTimer > 0)
      bonusTimer--;
   if (demo && centred(px, py))
      autopilot();
   int sp = speedOf(80);
   if (frightTimer > 0)
      sp = speedOf(90);
   pacc = pacc + sp;
   while (pacc >= 100 && phase == 1)
   {
      pacc = pacc - 100;
      playerStep();
   }
   for (int i = 0; i < 4; i++)
   {
      gacc[i] = gacc[i] + chaserSpeed(i);
      while (gacc[i] >= 100)
      {
         gacc[i] = gacc[i] - 100;
         chaserStep(i);
      }
   }
   if (phase == 1)
      checkCatch();
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
   if (k == 122 || k == 90)
      wantDir = 0;
   if (k == 113 || k == 81)
      wantDir = 1;
   if (k == 115 || k == 83)
      wantDir = 2;
   if (k == 100 || k == 68)
      wantDir = 3;
}

void computeEdges()
{
   for (int r = 0; r < H; r++)
      for (int c = 0; c < W; c++)
      {
         int e = 0;
         int t = mazeStart[r * W + c];
         if (t == 1 || t == 7)
            for (int d = 0; d < 4; d++)
            {
               int n = tileAt(c + dxs[d], r + dys[d]);
               if (r + dys[d] >= 0 && r + dys[d] < H && (walkable(n) || n == 5))
                  e = e | (1 << d);
            }
         edges[r * W + c] = e;
      }
}

void makeColors()
{
   black = hsv2(0, 0, 0);
   white = hsv2(0, 0, 255);
   grey = hsv2(160, 40, 150);
   wallFill = hsv2(195, 230, 170);
   wallEdge = hsv2(220, 110, 255);
   wallGlow = hsv2(210, 220, 190);
   dotColor = hsv2(30, 90, 255);
   doorColor = hsv2(224, 160, 255);
   orbColor = hsv2(80, 230, 255);
   orbDark = hsv2(90, 255, 130);
   orbLight = hsv2(70, 80, 255);
   frightBody = hsv2(165, 255, 200);
   frightDark = hsv2(165, 255, 90);
   red = hsv2(0, 255, 230);
   green = hsv2(96, 255, 200);
   brown = hsv2(20, 200, 110);
   yellow = hsv2(50, 200, 255);
   cyan = hsv2(128, 200, 255);
   for (int i = 0; i < 4; i++)
   {
      gbody[i] = hsv2(ghue[i], 230, 240);
      glight[i] = hsv2(ghue[i], 110, 255);
      gdark[i] = hsv2(ghue[i], 255, 120);
   }
   for (int i = 0; i < 9; i++)
      title[i] = hsv2(i * 28, 200, 255);
}

void fillRect(int x, int y, int w, int h, uint16_t c)
{
   for (int j = 0; j < h; j++)
   {
      int row = (y + j) * STRIDE + x;
      for (int i = 0; i < w; i++)
         led16[row + i] = c;
   }
}

// a pixel of a sprite: only inside the clip columns
void plot(int x, int y, uint16_t c)
{
   if (x >= clipX0 && x < clipX1 && y >= 0 && y < 240)
      led16[y * STRIDE + x] = c;
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

// a wall tile: filled (inside the maze), a neon line on each side facing a path
void drawWall(int x, int y, int e, int t, uint16_t edge)
{
   if (t == 1)
      fillRect(x, y, 8, 8, wallFill);
   if (e & 1)
   {
      fillRect(x + 1, y, 6, 1, edge);
      fillRect(x + 1, y + 1, 6, 1, wallGlow);
   }
   if (e & 4)
   {
      fillRect(x + 1, y + 7, 6, 1, edge);
      fillRect(x + 1, y + 6, 6, 1, wallGlow);
   }
   if (e & 2)
   {
      fillRect(x, y + 1, 1, 6, edge);
      fillRect(x + 1, y + 1, 1, 6, wallGlow);
   }
   if (e & 8)
   {
      fillRect(x + 7, y + 1, 1, 6, edge);
      fillRect(x + 6, y + 1, 1, 6, wallGlow);
   }
   // the corners: rounded where two sides meet, joined where a side goes on
   if ((e & 3) == 3)
      led16[(y + 1) * STRIDE + x + 1] = edge;
   if ((e & 9) == 9)
      led16[(y + 1) * STRIDE + x + 6] = edge;
   if ((e & 6) == 6)
      led16[(y + 6) * STRIDE + x + 1] = edge;
   if ((e & 12) == 12)
      led16[(y + 6) * STRIDE + x + 6] = edge;
   if ((e & 3) == 1 || (e & 3) == 2)
      led16[y * STRIDE + x] = edge;
   if ((e & 9) == 1 || (e & 9) == 8)
      led16[y * STRIDE + x + 7] = edge;
   if ((e & 6) == 4 || (e & 6) == 2)
      led16[(y + 7) * STRIDE + x] = edge;
   if ((e & 12) == 4 || (e & 12) == 8)
      led16[(y + 7) * STRIDE + x + 7] = edge;
}

void drawMaze()
{
   uint16_t edge = wallEdge;
   if (phase == 3 && (phaseTimer / 15) % 2 == 0)
      edge = white;
   bool cellOn = false;
   if ((frame / 8) % 2 == 0)
      cellOn = true;
   for (int r = 0; r < H; r++)
      for (int c = 0; c < W; c++)
      {
         int i = r * W + c;
         int t = maze[i];
         int x = MX + c * 8;
         int y = MY + r * 8;
         if (t == 1 || t == 7)
            drawWall(x, y, edges[i], t, edge);
         else if (t == 2)
            fillRect(x + 3, y + 3, 2, 2, dotColor);
         else if (t == 3)
         {
            if (cellOn || phase != 1)
            {
               fillRect(x + 2, y + 1, 4, 6, dotColor);
               fillRect(x + 1, y + 2, 6, 4, dotColor);
               fillRect(x + 3, y + 2, 2, 2, white);
            }
         }
         else if (t == 4)
            fillRect(x, y + 3, 8, 2, doorColor);
      }
}

// the player: a round orb with an eye looking where it goes
void drawOrb(int x, int y, int dir, int rad)
{
   int r2 = rad * rad + rad;
   int in2 = (rad - 1) * (rad - 1);
   for (int j = -rad; j <= rad; j++)
      for (int i = -rad; i <= rad; i++)
      {
         int d = i * i + j * j;
         if (d <= r2)
         {
            uint16_t c = orbColor;
            if (d > in2 && i + j > 0)
               c = orbDark;
            if (d > in2 && i + j < -1)
               c = orbLight;
            plot(x + i, y + j, c);
         }
      }
   if (rad < 3)
      return;
   int ex = x + dxs[dir] * 2;
   int ey = y + dys[dir] * 2 - 1;
   plot(ex, ey, white);
   plot(ex - 1, ey, white);
   plot(ex, ey + 1, white);
   plot(ex - 1, ey + 1, white);
   int qx = ex;
   if (dxs[dir] < 0)
      qx = ex - 1;
   int qy = ey;
   if (dys[dir] > 0)
      qy = ey + 1;
   plot(qx, qy, black);
}

// an eye: white, the pupil on the side it looks
void drawEye(int x, int y, int dir)
{
   for (int j = -2; j <= 2; j++)
      for (int i = -2; i <= 2; i++)
         if (i * i + j * j <= 5)
            plot(x + i, y + j, white);
   int ex = x + dxs[dir];
   int ey = y + dys[dir];
   plot(ex, ey, black);
   plot(ex - 1, ey, black);
   plot(ex, ey - 1, black);
   plot(ex - 1, ey - 1, black);
}

// a chaser: a drone, a diamond with two antennas and one eye
void drawDrone(int x, int y, int i, int scared)
{
   uint16_t body = gbody[i];
   uint16_t light = glight[i];
   uint16_t dark = gdark[i];
   if (scared)
   {
      body = frightBody;
      light = frightBody;
      dark = frightDark;
      if (frightTimer < 120 && (frightTimer / 10) % 2 == 0)
      {
         body = white;
         light = white;
         dark = grey;
      }
   }
   for (int j = -6; j <= 6; j++)
      for (int k = -6; k <= 6; k++)
      {
         int a = k;
         if (a < 0)
            a = -a;
         int b = j;
         if (b < 0)
            b = -b;
         if (a + b <= 6)
         {
            uint16_t c = body;
            if (a + b >= 5 && k + j < 0)
               c = light;
            if (a + b >= 5 && k + j >= 0)
               c = dark;
            plot(x + k, y + j, c);
         }
      }
   int wave = (frame / 6 + i) % 2;
   plot(x - 3, y - 5 - wave, light);
   plot(x - 4, y - 6 - wave, light);
   plot(x + 3, y - 6 + wave, light);
   plot(x + 4, y - 7 + wave, light);
   if (scared)
   {
      plot(x - 2, y - 1, white);
      plot(x + 2, y - 1, white);
      for (int k = -3; k <= 3; k++)
         plot(x + k, y + 2 + (k + 3) % 2, white);
   }
   else
      drawEye(x, y, gdir[i]);
}

void drawApple(int x, int y)
{
   for (int j = -3; j <= 3; j++)
      for (int i = -3; i <= 3; i++)
         if (i * i + j * j <= 10)
            plot(x + i, y + j + 1, red);
   plot(x - 1, y - 1, white);
   plot(x, y - 3, brown);
   plot(x, y - 4, brown);
   plot(x + 1, y - 4, green);
   plot(x + 2, y - 5, green);
   plot(x + 2, y - 4, green);
}

void drawActors()
{
   clipX0 = MX;
   clipX1 = MX + MW;
   if (bonusTimer > 0)
      drawApple(MX + 13 * 8 + 4, MY + 16 * 8 + 4);
   if (phase != 4)
   {
      for (int i = 0; i < 4; i++)
      {
         if (phase == 2 && phaseTimer < 70)
            continue;
         int x = MX + gx[i];
         int y = MY + gy[i];
         if (gstate[i] == 3 || gstate[i] == 4)
         {
            if (phase == 5 && popupTimer > 0 && popupX == gx[i] && popupY == gy[i])
               drawNumber(x - 6, y - 2, popupValue, 1, cyan);
            else
               drawEye(x, y, gdir[i]);
         }
         else
            drawDrone(x, y, i, gfright[i]);
      }
      int rad = 6 - (ppulse / 4) % 2;
      if (phase == 2 && phaseTimer < 70)
      {
         // caught: the orb shrinks and throws sparks
         rad = phaseTimer / 10;
         int s = 70 - phaseTimer;
         for (int k = 0; k < 8; k++)
         {
            int ax = dxs[k % 4] + dxs[(k + 1) % 4] * (k / 4);
            int ay = dys[k % 4] + dys[(k + 1) % 4] * (k / 4);
            plot(MX + px + ax * s / 4, MY + py + ay * s / 4, orbLight);
         }
      }
      if (rad > 0)
         drawOrb(MX + px, MY + py, pdir, rad);
   }
   if (popupTimer > 0 && phase != 5)
      drawNumber(MX + popupX - 6, MY + popupY - 2, popupValue, 1, cyan);
   clipX0 = 0;
   clipX1 = 400;
}

void drawPanels()
{
   // the title, a colour per letter: M A Z E  C H A S E
   for (int i = 0; i < 9; i++)
   {
      int g = 22;
      if (i == 1 || i == 6)
         g = 10;
      if (i == 2)
         g = 35;
      if (i == 3 || i == 8)
         g = 14;
      if (i == 4)
         g = 12;
      if (i == 5)
         g = 17;
      if (i == 7)
         g = 28;
      int x = MX + 30 + i * 16;
      if (i > 3)
         x = x + 8;
      drawGlyph(x, 2, g, 3, title[i]);
   }
   drawText(10, 28, "SCORE", 2, grey);
   drawNumber(10, 42, score, 2, white);
   drawText(10, 70, "HIGH", 2, grey);
   drawNumber(10, 84, high, 2, white);
   drawText(318, 28, "LIVES", 2, grey);
   for (int i = 0; i < lives - 1; i++)
      drawOrb(324 + i * 15, 48, 3, 5);
   drawText(318, 70, "LEVEL", 2, grey);
   drawNumber(318, 84, level, 2, white);
   for (int i = 0; i < level && i < 5; i++)
      drawApple(323 + i * 12, 110);
   // who chases how
   for (int i = 0; i < 4; i++)
   {
      int dir = gdir[i];
      gdir[i] = 3;
      drawDrone(326, 142 + i * 20, i, 0);
      gdir[i] = dir;
   }
   drawText(338, 140, "RUSH", 1, gbody[0]);
   drawText(338, 160, "AHEAD", 1, gbody[1]);
   drawText(338, 180, "FLANK", 1, gbody[2]);
   drawText(338, 200, "DRIFT", 1, gbody[3]);
   if (demo)
   {
      if ((frame / 20) % 2 == 0)
      {
         drawText(10, 190, "PRESS", 2, yellow);
         drawText(10, 206, "SPACE", 2, yellow);
      }
   }
   else
   {
      drawText(10, 190, "Z Q S D", 1, grey);
      drawText(10, 200, "TO MOVE", 1, grey);
   }
   if (phase == 0)
   {
      fillRect(MX + 92, MY + 16 * 8, 32, 8, black);
      drawText(MX + 94, MY + 16 * 8 + 2, "GET SET", 1, yellow);
   }
   if (phase == 4)
   {
      fillRect(MX + 66, MY + 15 * 8 - 1, 84, 18, black);
      drawText(MX + 72, MY + 15 * 8 + 3, "GAME OVER", 2, red);
   }
}

void drawScene()
{
   for (int y = 0; y < 240; y++)
   {
      int row = y * STRIDE;
      for (int x = 0; x < 400; x++)
         led16[row + x] = black;
   }
   drawMaze();
   drawActors();
   drawPanels();
}

int main()
{
   setPixelsize(2);           // 400x240, each 2x2 pixels
   makeColors();
   high = 0;
   clipX0 = 0;
   clipX1 = 400;
   newGame(1);
   computeEdges();
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
      drawScene();
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

  static ScriptExecutable exec = parseScript(mazeChase);
  if (!exec.isExeExists()) {
    showCompileError();
    return;
  }
  printf("maze chase: %u bytes of code\n", (unsigned)exec.exe.binary_size);

  // the USB keyboard: keys go to print_char()
  hid_keyboard_print_char = print_char;
  app_main_keyboard();

  exec.execute("main");
}

void loop() {
  delay(1000);
}
