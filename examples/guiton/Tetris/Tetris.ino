// Tetris on the Guition ESP32-4848S040 (a 4-inch 480x480 touch screen) at
// 240x240 (setPixelsize(2)), the script of examples/matouch/Tetris laid out
// for a square screen and played on the touch screen: the buttons right of
// the well turn, drop, and move the piece left, down and right; left, right
// and down repeat while held. After a game over, TURN starts a new game.
//
// The script draws everything, the buttons too (bright while held: the
// sketch tells it which are held through touchHeld). The sketch reads the
// GT911 on a task of its own, 60 times a second, and turns a touch into the
// keys the script already knows (q d z s and space), queued for readKey()
// as the USB keyboard does on the matouch boards.
//
// Needs the modified LovyanGFX Bus_RGB driver (double frame buffer with
// show(), _frame_buffer, pixel_size, offset) and FastLED (CHSV).
// Guition4848S040.h is the panel configuration.
//
// Board: ESP32S3 Dev Module, PSRAM "OPI PSRAM", Flash Size 16MB.
#include "Guition4848S040.h"
#include "FastLED.h"
#include "script_executable.h"
#include "binding.h"

LGFX lcd;
uint16_t *frame_buffer;
static uint32_t _sizex = 480;
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
  memset(frame_buffer, 0, 480 * 480 * 2);
}

// pixel_size h: each cell is shown as h x h pixels, width x height cells
void setPixelsize(int h) {
  clearscreen();
  show16();
  clearscreen();
  lcd._bus_instance.offset = 0;
  lcd._bus_instance.pixel_size = h;
  _sizex = 480 / h;
  _sizey = 480 / h;
}

uint32_t scriptMillis() {
  return millis();
}

uint32_t scriptRand(uint32_t n) {
  return esp_random() % n;
}

// the backlight, 0 (off) to 255: a script can dim the screen
void scriptSetBrightness(int level) {
  lcd.setBrightness(level < 0 ? 0 : level > 255 ? 255 : level);
}

// the keys, from the touch task to the script's
#define KEY_QUEUE 16
static volatile uint8_t keyQueue[KEY_QUEUE];
static volatile uint8_t keyHead = 0, keyTail = 0;

static void pushKey(uint8_t c) {
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

// --- the touch buttons, at 240x240 as the script draws them (PANEL, BTN_Y,
// BTN_W, BTN_H in the script) ---
#define PANEL 130
#define BTN_Y 124
#define BTN_W 34
#define BTN_H 54

struct Button {
  int x, y, w, h;
  uint32_t bit;   // in touchHeld
  char key;       // what it sends
  bool repeats;   // sent again while held
};
static const Button buttons[] = {
  {PANEL, BTN_Y, 2 * BTN_W + 2, BTN_H, 8, 'z', false},                         // turn
  {PANEL + 2 * (BTN_W + 2), BTN_Y, BTN_W, BTN_H, 16, ' ', false},              // drop
  {PANEL, BTN_Y + BTN_H + 4, BTN_W, BTN_H, 1, 'q', true},                      // left
  {PANEL + BTN_W + 2, BTN_Y + BTN_H + 4, BTN_W, BTN_H, 4, 's', true},          // down
  {PANEL + 2 * (BTN_W + 2), BTN_Y + BTN_H + 4, BTN_W, BTN_H, 2, 'd', true},    // right
};
#define NUM_BUTTONS (sizeof(buttons) / sizeof(buttons[0]))
#define REPEAT_DELAY 220
#define REPEAT_EVERY 70

// the buttons held down, read by the script to draw them
static volatile uint32_t touchHeld = 0;

static void touchTask(void *) {
  int held = -1;           // the button being held
  uint32_t next = 0;       // when it is sent again
  while (true) {
    lgfx::touch_point_t tp;
    int now = -1;
    if (lcd.getTouchRaw(&tp, 1)) {
      // the panel's 480x480 to the script's 240x240; a touch a little
      // outside a button still counts (4 cells of margin)
      int x = tp.x / 2, y = tp.y / 2;
      for (int i = 0; i < (int)NUM_BUTTONS; i++) {
        const Button &b = buttons[i];
        if (x >= b.x - 4 && x < b.x + b.w + 4 && y >= b.y - 4 && y < b.y + b.h + 4) {
          now = i;
          break;
        }
      }
    }
    uint32_t t = millis();
    if (now != held) {
      held = now;
      if (held >= 0) {
        pushKey(buttons[held].key);
        next = t + REPEAT_DELAY;
      }
    } else if (held >= 0 && buttons[held].repeats && (int32_t)(t - next) >= 0) {
      pushKey(buttons[held].key);
      next = t + REPEAT_EVERY;
    }
    touchHeld = held >= 0 ? buttons[held].bit : 0;
    vTaskDelay(pdMS_TO_TICKS(16));
  }
}

char tetris[] = R"EOF(
// the frame buffer's width, whatever the pixel size
#define STRIDE 480
// a block is 10x10 pixels, 1 pixel apart
#define CELL 11
// the well's top-left corner, and its size in blocks
#define BX 8
#define BY 9
#define BW 10
#define BH 20
// the right panel's left edge
#define PANEL 130
// the touch buttons, under the panel: three columns, two rows
// (the sketch tests the touches against the same rectangles)
#define BTN_Y 124
#define BTN_W 34
#define BTN_H 54
// what touchHeld's bits mean: the buttons held down
#define HELD_LEFT 1
#define HELD_RIGHT 2
#define HELD_DOWN 4
#define HELD_TURN 8
#define HELD_DROP 16

uint8_t board[BH, BW];
// I O T S Z J L, as 4x4 grids (the number is the colour)
uint8_t shapes[7, 4, 4] = {
   0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 2, 2, 0, 0, 2, 2, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 3, 0, 0, 3, 3, 3, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 4, 4, 0, 4, 4, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 5, 5, 0, 0, 0, 5, 5, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 6, 0, 0, 0, 6, 6, 6, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 7, 0, 7, 7, 7, 0, 0, 0, 0, 0};
// the hue of each piece (FastLED's rainbow): cyan, yellow, purple, green, red, blue, orange
uint8_t hues[7] = {136, 64, 196, 96, 0, 160, 30};
// an 8x8 font, bold and rounded: 0-9, A-Z, then . : / and +; 8 rows a
// character, the leftmost pixel in the highest bit (ChromaQuest/tools/font8.py)
uint8_t font8[320] = {126, 199, 207, 219, 243, 227, 126, 0, 24, 56, 120, 24, 24, 24, 126, 0, 126, 195, 6, 28, 112, 192, 255, 0, 126, 195, 3, 62, 3, 195, 126, 0, 14, 30, 54, 102, 255, 6, 6, 0, 255, 192, 254, 3, 3, 195, 126, 0, 62, 96, 192, 254, 195, 195, 126, 0, 255, 195, 6, 12, 24, 24, 24, 0, 126, 195, 195, 126, 195, 195, 126, 0, 126, 195, 195, 127, 3, 6, 124, 0, 60, 102, 195, 195, 255, 195, 195, 0, 254, 195, 195, 254, 195, 195, 254, 0, 126, 195, 192, 192, 192, 195, 126, 0, 252, 198, 195, 195, 195, 198, 252, 0, 255, 192, 192, 252, 192, 192, 255, 0, 255, 192, 192, 252, 192, 192, 192, 0, 126, 195, 192, 207, 195, 195, 126, 0, 195, 195, 195, 255, 195, 195, 195, 0, 126, 24, 24, 24, 24, 24, 126, 0, 63, 6, 6, 6, 198, 198, 124, 0, 198, 204, 216, 240, 216, 204, 198, 0, 192, 192, 192, 192, 192, 192, 255, 0, 195, 231, 255, 219, 195, 195, 195, 0, 195, 227, 243, 219, 207, 199, 195, 0, 126, 195, 195, 195, 195, 195, 126, 0, 254, 195, 195, 254, 192, 192, 192, 0, 126, 195, 195, 195, 203, 198, 123, 0, 254, 195, 195, 254, 204, 198, 195, 0, 126, 195, 192, 126, 3, 195, 126, 0, 255, 24, 24, 24, 24, 24, 24, 0, 195, 195, 195, 195, 195, 195, 126, 0, 195, 195, 195, 195, 102, 60, 24, 0, 195, 195, 195, 219, 255, 231, 195, 0, 195, 102, 60, 24, 60, 102, 195, 0, 195, 195, 102, 60, 24, 24, 24, 0, 255, 6, 12, 24, 48, 96, 255, 0, 0, 0, 0, 0, 0, 192, 192, 0, 0, 192, 192, 0, 0, 192, 192, 0, 3, 6, 12, 24, 48, 96, 192, 0, 0, 24, 24, 126, 24, 24, 0, 0};
// how each row shades the text's colour, in sixteenths: scaled by rowMul,
// then lifted toward white by rowLift; a dark band, then a bright edge
int rowMul[8] = {16, 16, 16, 7, 16, 15, 12, 9};
int rowLift[8] = {10, 6, 2, 0, 5, 0, 0, 0};

uint8_t cur[16];          // the falling piece
uint8_t saved[16];
uint8_t turned[16];
int curX;
int curY;
int curP;
int nextP;
int score;
int lines;
int level;
bool gameOver;
uint32_t lastDrop;

uint16_t pal[64];         // per piece: light edge, face, dark edge, shine, soft ring
uint16_t sky[240];        // the background, row by row
uint16_t wellColor;
uint16_t frameLight;
uint16_t frameDark;
uint16_t white;
uint16_t grey;

void makeColors()
{
   for (int p = 0; p < 7; p++)
   {
      int h = hues[p];
      int c = (p + 1) * 8;
      pal[c] = hsv2(h, 120, 255);
      pal[c + 1] = hsv2(h, 255, 235);
      pal[c + 2] = hsv2(h, 255, 120);
      pal[c + 3] = hsv2(h, 20, 255);
      pal[c + 4] = hsv2(h, 200, 255);
   }
   for (int y = 0; y < 240; y++)
      sky[y] = hsv2(165, 200, 20 + y / 6);
   wellColor = hsv2(165, 160, 12);
   frameLight = hsv2(165, 60, 200);
   frameDark = hsv2(165, 120, 80);
   white = hsv2(0, 0, 255);
   grey = hsv2(165, 40, 150);
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

// a bevelled block: light top and left, dark bottom and right, a soft
// ring inside the light edges, a shine
void drawBlock(int px, int py, int p)
{
   uint16_t light = pal[p * 8];
   uint16_t face = pal[p * 8 + 1];
   uint16_t dark = pal[p * 8 + 2];
   uint16_t soft = pal[p * 8 + 4];
   for (int j = 0; j < 10; j++)
   {
      int row = (py + j) * STRIDE + px;
      for (int i = 0; i < 10; i++)
      {
         uint16_t c = face;
         if ((i == 1 || j == 1) && i < 9 && j < 9)
            c = soft;
         if (i == 0 || j == 0)
            c = light;
         if (i == 9 || j == 9)
            c = dark;
         led16[row + i] = c;
      }
   }
   uint16_t shine = pal[p * 8 + 3];
   led16[(py + 2) * STRIDE + px + 2] = shine;
   led16[(py + 2) * STRIDE + px + 3] = shine;
   led16[(py + 3) * STRIDE + px + 2] = shine;
}

// where the piece will land: an outline in its colour
void drawGhost(int px, int py, int p)
{
   uint16_t c = pal[p * 8 + 1];
   for (int i = 0; i < 10; i++)
   {
      led16[py * STRIDE + px + i] = c;
      led16[(py + 9) * STRIDE + px + i] = c;
      led16[(py + i) * STRIDE + px] = c;
      led16[(py + i) * STRIDE + px + 9] = c;
   }
}

// a character at (x, y), sc pixels a dot: a dark shadow down and to the
// right, then each row in its shade of c; a run of dots is one rectangle
void drawGlyph(int x, int y, int g, int sc, uint16_t c)
{
   int v = ((c & 255) << 8) | (c >> 8);
   int cr = (v >> 11) & 31;
   int cg = (v >> 5) & 63;
   int cb = v & 31;
   for (int pass = 0; pass < 2; pass++)
   {
      int off = sc;
      if (pass == 1)
         off = 0;
      for (int r = 0; r < 8; r++)
      {
         int bits = font8[g * 8 + r];
         uint16_t rc = 16904;       // the shadow: a dark blue
         if (pass == 1)
         {
            int rr = (cr * rowMul[r]) >> 4;
            int rg = (cg * rowMul[r]) >> 4;
            int rb = (cb * rowMul[r]) >> 4;
            rr = rr + (((31 - rr) * rowLift[r]) >> 4);
            rg = rg + (((63 - rg) * rowLift[r]) >> 4);
            rb = rb + (((31 - rb) * rowLift[r]) >> 4);
            int w = (rr << 11) | (rg << 5) | rb;
            rc = ((w & 255) << 8) | (w >> 8);
         }
         int b = 0;
         while (b < 8)
         {
            if (((bits >> (7 - b)) & 1) == 1)
            {
               int e = b + 1;
               while (e < 8 && ((bits >> (7 - e)) & 1) == 1)
                  e++;
               fillRect(x + b * sc + off, y + r * sc + off, (e - b) * sc, sc, rc);
               b = e;
            }
            else
               b++;
         }
      }
   }
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
      x = x + 9 * sc;
      i++;
   }
}

int digits[8];
void drawNumber(int x, int y, int v, int sc, uint16_t c)
{
   int n = 0;
   digits[0] = 0;
   while (v > 0 || n == 0)
   {
      digits[n] = v % 10;
      v = v / 10;
      n++;
   }
   for (int k = n - 1; k >= 0; k--)
   {
      drawGlyph(x, y, digits[k], sc, c);
      x = x + 9 * sc;
   }
}

bool collide(int dx, int dy)
{
   for (int y = 0; y < 4; y++)
      for (int x = 0; x < 4; x++)
         if (cur[y * 4 + x] != 0)
         {
            int nx = curX + x + dx;
            int ny = curY + y + dy;
            if (nx < 0 || nx >= BW || ny >= BH)
               return true;
            if (ny >= 0 && board[ny, nx] != 0)
               return true;
         }
   return false;
}

void spawn()
{
   curP = nextP;
   nextP = rand(7);
   for (int i = 0; i < 16; i++)
      cur[i] = shapes[curP * 16 + i];
   curX = 3;
   curY = -1;
   if (collide(0, 0))
      gameOver = true;
}

void newGame()
{
   for (int i = 0; i < BW * BH; i++)
      board[i] = 0;
   score = 0;
   lines = 0;
   level = 1;
   gameOver = false;
   nextP = rand(7);
   spawn();
   lastDrop = millis();
}

// a quarter turn, then a step aside if it doesn't fit (a wall kick)
void rotate()
{
   for (int i = 0; i < 16; i++)
      saved[i] = cur[i];
   for (int y = 0; y < 4; y++)
      for (int x = 0; x < 4; x++)
         turned[y * 4 + x] = cur[(3 - x) * 4 + y];
   for (int i = 0; i < 16; i++)
      cur[i] = turned[i];
   if (!collide(0, 0))
      return;
   if (!collide(-1, 0))
   {
      curX--;
      return;
   }
   if (!collide(1, 0))
   {
      curX++;
      return;
   }
   if (!collide(2, 0))
   {
      curX = curX + 2;
      return;
   }
   for (int i = 0; i < 16; i++)
      cur[i] = saved[i];
}

void clearLines()
{
   int n = 0;
   for (int y = BH - 1; y >= 0; y--)
   {
      bool full = true;
      for (int x = 0; x < BW; x++)
         if (board[y, x] == 0)
            full = false;
      if (full)
      {
         for (int ty = y; ty > 0; ty--)
            for (int x = 0; x < BW; x++)
               board[ty, x] = board[ty - 1, x];
         for (int x = 0; x < BW; x++)
            board[0, x] = 0;
         n++;
         y++;              // the row that came down is looked at again
      }
   }
   if (n == 1)
      score = score + 100 * level;
   if (n == 2)
      score = score + 300 * level;
   if (n == 3)
      score = score + 500 * level;
   if (n == 4)
      score = score + 800 * level;
   lines = lines + n;
   level = lines / 10 + 1;
}

void lock()
{
   for (int y = 0; y < 4; y++)
      for (int x = 0; x < 4; x++)
         if (cur[y * 4 + x] != 0 && curY + y >= 0)
            board[curY + y, curX + x] = cur[y * 4 + x];
   clearLines();
   spawn();
}

int dropInterval()
{
   int t = 800 - (level - 1) * 70;
   if (t < 80)
      t = 80;
   return t;
}

void handleKey(int k)
{
   if (k == 27)                             // Escape: the game is given up
   {
      gameOver = true;
      return;
   }
   if (gameOver)
   {
      if (k == 122 || k == 90)              // Z: a new game
         newGame();
      return;
   }
   if (k == 113 || k == 81)                 // Q: left
   {
      if (!collide(-1, 0))
         curX--;
   }
   if (k == 100 || k == 68)                 // D: right
   {
      if (!collide(1, 0))
         curX++;
   }
   if (k == 122 || k == 90)                 // Z: rotate
      rotate();
   if (k == 115 || k == 83)                 // S: one row down
   {
      if (!collide(0, 1))
      {
         curY++;
         score = score + 1;
      }
   }
   if (k == 32)                             // Space: drop
   {
      while (!collide(0, 1))
      {
         curY++;
         score = score + 2;
      }
      lock();
      lastDrop = millis();
   }
}

// a touch button: a light edge with cut corners, dark inside, bright while held
void drawButton(int x, int y, int w, int h, int bit)
{
   uint16_t edge = frameLight;
   uint16_t face = frameDark;
   if ((touchHeld & bit) != 0)
   {
      edge = white;
      face = pal[6 * 8 + 1];
   }
   fillRect(x + 1, y, w - 2, h, edge);
   fillRect(x, y + 1, w, h - 2, edge);
   fillRect(x + 2, y + 2, w - 4, h - 4, face);
}

// an arrow pointing down, its tip at (cx, cy)
void arrowDown(int cx, int cy, int size, uint16_t c)
{
   for (int i = 0; i < size; i++)
      fillRect(cx - i, cy - i, 2 * i + 1, 1, c);
}

void drawButtons()
{
   int x0 = PANEL;
   int x1 = PANEL + BTN_W + 2;
   int x2 = PANEL + 2 * (BTN_W + 2);
   int y0 = BTN_Y;
   int y1 = BTN_Y + BTN_H + 4;
   drawButton(x0, y0, 2 * BTN_W + 2, BTN_H, HELD_TURN);
   drawButton(x2, y0, BTN_W, BTN_H, HELD_DROP);
   drawButton(x0, y1, BTN_W, BTN_H, HELD_LEFT);
   drawButton(x1, y1, BTN_W, BTN_H, HELD_DOWN);
   drawButton(x2, y1, BTN_W, BTN_H, HELD_RIGHT);
   drawText(x0 + 17, y0 + 23, "TURN", 1, white);
   // drop: two arrows and the floor
   arrowDown(x2 + 17, y0 + 24, 7, white);
   arrowDown(x2 + 17, y0 + 33, 7, white);
   fillRect(x2 + 9, y0 + 37, 17, 2, white);
   // left, down, right
   int cy = y1 + BTN_H / 2;
   for (int i = 0; i < 10; i++)
   {
      fillRect(x0 + 12 + i, cy - i, 1, 2 * i + 1, white);
      fillRect(x2 + 21 - i, cy - i, 1, 2 * i + 1, white);
   }
   arrowDown(x1 + 17, cy + 5, 10, white);
}

void drawScene()
{
   for (int y = 0; y < 240; y++)
   {
      int row = y * STRIDE;
      uint16_t c = sky[y];
      for (int x = 0; x < 240; x++)
         led16[row + x] = c;
   }
   // the well and its frame
   fillRect(BX - 4, BY - 4, BW * CELL + 7, BH * CELL + 7, frameLight);
   fillRect(BX - 2, BY - 2, BW * CELL + 3, BH * CELL + 3, frameDark);
   fillRect(BX - 1, BY - 1, BW * CELL + 1, BH * CELL + 1, wellColor);
   for (int y = 0; y < BH; y++)
      for (int x = 0; x < BW; x++)
      {
         int p = board[y, x];
         if (p != 0)
            drawBlock(BX + x * CELL, BY + y * CELL, p);
         else
            led16[(BY + y * CELL + 5) * STRIDE + BX + x * CELL + 5] = frameDark;
      }
   if (!gameOver)
   {
      int g = 0;
      while (!collide(0, g + 1))
         g++;
      for (int y = 0; y < 4; y++)
         for (int x = 0; x < 4; x++)
            if (cur[y * 4 + x] != 0)
            {
               if (curY + y + g >= 0)
                  drawGhost(BX + (curX + x) * CELL, BY + (curY + y + g) * CELL, curP + 1);
            }
      for (int y = 0; y < 4; y++)
         for (int x = 0; x < 4; x++)
            if (cur[y * 4 + x] != 0 && curY + y >= 0)
               drawBlock(BX + (curX + x) * CELL, BY + (curY + y) * CELL, curP + 1);
   }
   // the title, a colour per letter
   for (int i = 0; i < 6; i++)
   {
      int letter = 29;                       // T E T R I S
      if (i == 1)
         letter = 14;
      if (i == 3)
         letter = 27;
      if (i == 4)
         letter = 18;
      if (i == 5)
         letter = 28;
      drawGlyph(PANEL + 26 + i * 9, 4, letter, 1, pal[(i + 1) * 8 + 1]);
   }
   // the panel
   drawText(PANEL, 20, "SCORE", 1, grey);
   if (score < 100000)
      drawNumber(PANEL, 30, score, 2, white);
   else
      drawNumber(PANEL, 34, score, 1, white);
   drawText(PANEL, 52, "LINES", 1, grey);
   drawNumber(PANEL, 62, lines, 2, white);
   drawText(PANEL + 56, 52, "LEVEL", 1, grey);
   drawNumber(PANEL + 56, 62, level, 2, white);
   drawText(PANEL, 98, "NEXT", 1, grey);
   for (int y = 0; y < 4; y++)
      for (int x = 0; x < 4; x++)
         if (shapes[nextP * 16 + y * 4 + x] != 0)
            drawBlock(PANEL + 50 + x * CELL, 80 + y * CELL, nextP + 1);
   drawButtons();
   if (gameOver)
   {
      fillRect(BX + 2, BY + 74, BW * CELL - 4, 86, frameLight);
      fillRect(BX + 4, BY + 76, BW * CELL - 8, 82, frameDark);
      drawText(BX + 19, BY + 86, "GAME", 2, white);
      drawText(BX + 19, BY + 110, "OVER", 2, white);
      drawText(BX + 19, BY + 138, "TAP TURN", 1, white);
   }
}

int main()
{
   setPixelsize(2);           // 240x240 cells, each 2x2 pixels
   makeColors();
   newGame();
   while (true)
   {
      int k = readKey();
      while (k != 0)
      {
         handleKey(k);
         k = readKey();
      }
      if (!gameOver && millis() - lastDrop >= dropInterval())
      {
         lastDrop = millis();
         if (!collide(0, 1))
            curY++;
         else
            lock();
      }
      drawScene();
      show16();
   }
}
)EOF";

// lcd.init() on core 0 so Bus_RGB's line copy interrupts stay off core 1, where the script draws
static void lcdInitTask(void *done) {
  lcd.init();
  lcd.setBrightness(255);
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
  bindVariable((char *)"uint32_t", (char *)"touchHeld", NULL, (void *)&touchHeld);
  bindFunction((char *)"uint16_t", (char *)"hsv2", (char *)"int,int,int", (void *)hsv16);
  bindFunction((char *)"void", (char *)"show16", NULL, (void *)show16);
  bindFunction((char *)"void", (char *)"setPixelsize", (char *)"int", (void *)setPixelsize);
  bindFunction((char *)"uint32_t", (char *)"millis", NULL, (void *)scriptMillis);
  bindFunction((char *)"uint32_t", (char *)"rand", (char *)"uint32_t", (void *)scriptRand);
  bindFunction((char *)"int", (char *)"readKey", NULL, (void *)readKey);
  bindFunction((char *)"void", (char *)"setBrightness", (char *)"int", (void *)scriptSetBrightness);

  uint32_t t0 = millis();
  static ScriptExecutable exec = parseScript(tetris);
  if (!exec.isExeExists()) {
    showCompileError();
    return;
  }
  printf("tetris: %u bytes of code, compiled in %u ms\n", (unsigned)exec.exe.binary_size, (unsigned)(millis() - t0));

  // the touch screen, read on core 0
  xTaskCreatePinnedToCore(touchTask, "touch", 4096, NULL, 3, NULL, 0);

  exec.execute("main");
}

void loop() {
  delay(1000);
}
