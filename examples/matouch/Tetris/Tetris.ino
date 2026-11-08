// Tetris on the 800x480 RGB panel at 400x240 (setPixelsize(2)), written as
// an ESPLiveScript script drawing straight into the frame buffer (led16),
// played with a USB keyboard:
//   Q left, D right, Z rotate, S down one row, Space drop.
//   After a game over, Z starts a new one.
// Every tetromino has its colour, drawn as bevelled blocks; a ghost shows
// where the piece will land; the panel shows the score, the lines, the
// level (faster every 10 lines) and the next piece.
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
  if (k.key_char == 0)
    return;
  uint8_t next = (keyHead + 1) % KEY_QUEUE;
  if (next != keyTail) {  // full: the key is dropped
    keyQueue[keyHead] = k.key_char;
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
// the hue of each piece: cyan, yellow, purple, green, red, blue, orange
uint8_t hues[7] = {128, 42, 192, 90, 0, 160, 18};
// 3x5 pixel font, 0-9 then A-Z: 5 rows of 3 bits, the top row first
int font[36] = {31599, 11415, 29671, 29647, 23497, 31183, 31215, 29330, 31727, 31695,
                11245, 27566, 14627, 27502, 31143, 31140, 14699, 23533, 29847, 4714,
                23861, 18727, 24557, 27501, 11114, 27556, 11123, 27565, 14478, 29842,
                23407, 23402, 23549, 23213, 23186, 29351};

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

uint16_t pal[32];         // per piece: light edge, face, dark edge, shine
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
      pal[(p + 1) * 4] = hsv2(h, 110, 255);
      pal[(p + 1) * 4 + 1] = hsv2(h, 240, 215);
      pal[(p + 1) * 4 + 2] = hsv2(h, 255, 110);
      pal[(p + 1) * 4 + 3] = hsv2(h, 30, 255);
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

// a bevelled block: light top and left, dark bottom and right, a shine
void drawBlock(int px, int py, int p)
{
   uint16_t light = pal[p * 4];
   uint16_t face = pal[p * 4 + 1];
   uint16_t dark = pal[p * 4 + 2];
   for (int j = 0; j < 10; j++)
   {
      int row = (py + j) * STRIDE + px;
      for (int i = 0; i < 10; i++)
      {
         uint16_t c = face;
         if (i == 0 || j == 0)
            c = light;
         if (i == 9 || j == 9)
            c = dark;
         led16[row + i] = c;
      }
   }
   uint16_t shine = pal[p * 4 + 3];
   led16[(py + 2) * STRIDE + px + 2] = shine;
   led16[(py + 2) * STRIDE + px + 3] = shine;
   led16[(py + 3) * STRIDE + px + 2] = shine;
}

// where the piece will land: an outline in its dark colour
void drawGhost(int px, int py, int p)
{
   uint16_t c = pal[p * 4 + 2];
   for (int i = 0; i < 10; i++)
   {
      led16[py * STRIDE + px + i] = c;
      led16[(py + 9) * STRIDE + px + i] = c;
      led16[(py + i) * STRIDE + px] = c;
      led16[(py + i) * STRIDE + px + 9] = c;
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
      x = x + 4 * sc;
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
      drawGlyph(20 + i * 18, 22, letter, 4, pal[(i + 1) * 4 + 1]);
   }
   drawText(22, 150, "Q LEFT", 2, grey);
   drawText(22, 166, "D RIGHT", 2, grey);
   drawText(22, 182, "Z TURN", 2, grey);
   drawText(22, 198, "S DOWN", 2, grey);
   drawText(22, 214, "SPACE DROP", 2, grey);
   // the panel
   drawText(PANEL, 14, "SCORE", 2, grey);
   drawNumber(PANEL, 30, score, 3, white);
   drawText(PANEL, 62, "LINES", 2, grey);
   drawNumber(PANEL, 78, lines, 3, white);
   drawText(PANEL, 110, "LEVEL", 2, grey);
   drawNumber(PANEL, 126, level, 3, white);
   drawText(PANEL, 158, "NEXT", 2, grey);
   for (int y = 0; y < 4; y++)
      for (int x = 0; x < 4; x++)
         if (shapes[nextP * 16 + y * 4 + x] != 0)
            drawBlock(PANEL + x * CELL, 166 + y * CELL, nextP + 1);
   if (gameOver)
   {
      fillRect(BX + 4, BY + 80, BW * CELL - 8, 56, frameDark);
      drawText(BX + 13, BY + 88, "GAME", 4, white);
      drawText(BX + 13, BY + 112, "OVER", 4, white);
      drawText(BX + 22, BY + 140, "Z AGAIN", 2, white);
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

  static ScriptExecutable exec = parseScript(tetris);
  if (!exec.isExeExists()) {
    showCompileError();
    return;
  }
  printf("tetris: %u bytes of code\n", (unsigned)exec.exe.binary_size);

  // the USB keyboard: keys go to print_char()
  hid_keyboard_print_char = print_char;
  app_main_keyboard();

  exec.execute("main");
}

void loop() {
  delay(1000);
}
