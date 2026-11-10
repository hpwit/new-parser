// Compiles the Tetris script (the one of matouch/Tetris) and saves the
// compiled binary to the SD card as /tetris.bin, for ExecuteTetrisBinary to
// load and play without compiling anything.
//
// Compiling only needs to know what the script's bound names are: the
// bindings below give the same names and types as ExecuteTetrisBinary's,
// with functions that do nothing. The binary keeps the names, not these
// addresses; the sketch that loads it binds them to its own functions.
//
// Run this once with a microSD card in the board's slot, then upload
// ExecuteTetrisBinary. The screen stays off: this sketch only prints.
//
// For the ESP32-S3-Touch-LCD-7 (800x480, ESP32-S3, 16MB flash, 8MB OPI PSRAM).
// Board: ESP32S3 Dev Module, PSRAM "OPI PSRAM", Flash Size 16MB.
#include <FS.h>
#include <SD_MMC.h>
#include "script_executable.h"
#include "binding.h"

// the card, read in 1 bit SD mode
#define PIN_SD_CMD 11
#define PIN_SD_CLK 12
#define PIN_SD_D0 13
#define BINARY_FILE "/tetris.bin"

// only their names and types matter here
static uint16_t *frame_buffer = NULL;
static uint32_t _sizex = 400, _sizey = 240;
static uint16_t hsv16(uint8_t h, uint8_t s, uint8_t v) { return 0; }
static void show16() {}
static void setPixelsize(int h) {}
static uint32_t scriptMillis() { return 0; }
static uint32_t scriptRand(uint32_t n) { return 0; }
static int readKey() { return 0; }

char tetris[] = R"EOF(
// the frame buffer's width, whatever the pixel size
#define STRIDE 800
// a block is 10x10 pixels, 1 pixel apart
#define CELL 11
// the well's top-left corner, and its size in blocks
#define BX 145
#define BY 10
#define BW 10
#define BH 20
// the right panel's left edge
#define PANEL 278

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

void drawScene()
{
   for (int y = 0; y < 240; y++)
   {
      int row = y * STRIDE;
      uint16_t c = sky[y];
      for (int x = 0; x < 400; x++)
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
      drawGlyph(20 + i * 18, 24, letter, 2, pal[(i + 1) * 8 + 1]);
   }
   drawText(22, 150, "Q LEFT", 1, grey);
   drawText(22, 166, "D RIGHT", 1, grey);
   drawText(22, 182, "Z TURN", 1, grey);
   drawText(22, 198, "S DOWN", 1, grey);
   drawText(22, 214, "SPACE DROP", 1, grey);
   // the panel
   drawText(PANEL, 14, "SCORE", 1, grey);
   drawNumber(PANEL, 28, score, 2, white);
   drawText(PANEL, 62, "LINES", 1, grey);
   drawNumber(PANEL, 76, lines, 2, white);
   drawText(PANEL, 110, "LEVEL", 1, grey);
   drawNumber(PANEL, 124, level, 2, white);
   drawText(PANEL, 158, "NEXT", 1, grey);
   for (int y = 0; y < 4; y++)
      for (int x = 0; x < 4; x++)
         if (shapes[nextP * 16 + y * 4 + x] != 0)
            drawBlock(PANEL + x * CELL, 166 + y * CELL, nextP + 1);
   if (gameOver)
   {
      fillRect(BX + 2, BY + 74, BW * CELL - 4, 86, frameLight);
      fillRect(BX + 4, BY + 76, BW * CELL - 8, 82, frameDark);
      drawText(BX + 19, BY + 86, "GAME", 2, white);
      drawText(BX + 19, BY + 110, "OVER", 2, white);
      drawText(BX + 23, BY + 138, "Z AGAIN", 1, white);
   }
}

int main()
{
   setPixelsize(2);           // 400x240 cells, each 2x2 pixels
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

void setup() {
  Serial.begin(115200);
  delay(1000);

  bindVariable((char *)"uint32_t", (char *)"width", NULL, (void *)&_sizex);
  bindVariable((char *)"uint32_t", (char *)"height", NULL, (void *)&_sizey);
  bindVariable((char *)"uint16_t *", (char *)"led16", NULL, (void *)&frame_buffer);
  bindFunction((char *)"uint16_t", (char *)"hsv2", (char *)"int,int,int", (void *)hsv16);
  bindFunction((char *)"void", (char *)"show16", NULL, (void *)show16);
  bindFunction((char *)"void", (char *)"setPixelsize", (char *)"int", (void *)setPixelsize);
  bindFunction((char *)"uint32_t", (char *)"millis", NULL, (void *)scriptMillis);
  bindFunction((char *)"uint32_t", (char *)"rand", (char *)"uint32_t", (void *)scriptRand);
  bindFunction((char *)"int", (char *)"readKey", NULL, (void *)readKey);

  uint32_t t0 = millis();
  uint32_t size = 0;
  uint8_t *bin = parseScriptToBinary(tetris, &size);
  if (bin == NULL)
    return;  // the error is already printed
  printf("tetris compiled in %u ms: %u bytes of binary\n", (unsigned)(millis() - t0), (unsigned)size);

  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
  if (!SD_MMC.begin("/sdcard", true)) {  // true: 1 bit mode; a card that fails is never formatted
    printf("no SD card\n");
    free(bin);
    return;
  }
  File f = SD_MMC.open(BINARY_FILE, FILE_WRITE);
  if (!f) {
    printf("cannot write %s\n", BINARY_FILE);
    free(bin);
    return;
  }
  size_t written = f.write(bin, size);
  f.close();

  // read it back to be sure the card has it
  bool same = false;
  f = SD_MMC.open(BINARY_FILE, FILE_READ);
  if (f && f.size() == size) {
    uint8_t *back = (uint8_t *)malloc(size);
    same = back && f.read(back, size) == size && memcmp(back, bin, size) == 0;
    free(back);
  }
  if (f)
    f.close();
  free(bin);
  SD_MMC.end();

  if (written != size || !same) {
    printf("%s: the write failed (%u of %u bytes)\n", BINARY_FILE, (unsigned)written, (unsigned)size);
    return;
  }
  printf("saved %s (%u bytes, read back and checked)\n", BINARY_FILE, (unsigned)size);
  printf("now upload ExecuteTetrisBinary to play it\n");
}

void loop() {
  delay(1000);
}
