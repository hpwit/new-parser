// Vault13H: a first-person maze shooter in the style of the early-90s VGA
// games, written as an ESPLiveScript script drawing straight into the frame
// buffer (led16) of the 800x480 RGB panel at 400x240 (setPixelsize(2)),
// played with a USB keyboard.
//
// The view is 320x240 in the 256 colours of the VGA default palette (mode
// 13h), each pixel 2x2: the game fills 640x480 in the middle of the panel,
// black bars on the sides. Walls are ray-cast one column at a time and
// textured (stone, brick, wood, brushed metal, worn variants); distance
// shading uses the palette itself, which holds each hue bright, medium and
// dark, as VGA games did. Drones guard the vault (two hits each); gold is
// score, red-cross kits are health, green boxes are ammo; the green EXIT
// panel ends the level. Three looks, one per level: the keep, the crypt,
// the base. Everything (palette, textures, sprites, font) is made by the
// script at start: no art files.
//
// Keys: Space starts and fires; Z forward, S back, Q and D turn (or the
// arrows), while held (release_char() tells the script when a key is let
// go; it needs the key-release hook in keyboard.h, see the README). On the
// title screen 1 2 3 pick the look to start in. Escape quits.
//
// The keys: the keyboard library calls print_char() on its USB task for
// every key; it only stores the character, and the script takes them with
// readKey() once per frame, so all the script's code runs on one task.
//
// Needs the modified LovyanGFX Bus_RGB driver (double frame buffer with show(),
// _frame_buffer, pixel_size, offset) and the ESP32_USB_Host_HID library with
// the key hooks (see the README's "A USB keyboard driving a script").
// Matouch7.h is the panel configuration (Waveshare ESP32-S3-Touch-LCD 800x480
// pinout).
//
// For the ESP32-S3-Touch-LCD-7: a 7-inch 800x480 RGB touch screen with an
// ESP32-S3 (16MB flash, 8MB OPI PSRAM). The keyboard goes on its USB host port.
// Board: ESP32S3 Dev Module, PSRAM "OPI PSRAM", Flash Size 16MB.
#include "Matouch7.h"
#include "keyboard.h"
#include "script_executable.h"
#include "binding.h"

LGFX lcd;
uint16_t *frame_buffer;
static uint32_t _sizex = 800;
static uint32_t _sizey = 480;

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

// the ray caster's trigonometry (cos(a) is sin(a + pi/2) in the script)
float scriptSin(float a) {
  return sinf(a);
}

float scriptHypot(float x, float y) {
  return hypotf(x, y);
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

// a direction let go: its letter + 128, so the walk or the turn stops (the
// other keys' releases don't matter)
void release_char(key_return k) {
  uint8_t c = tolower(k.key_char);
  if (k.key_code == HID_KEY_UP)
    c = 'z';
  else if (k.key_code == HID_KEY_LEFT)
    c = 'q';
  else if (k.key_code == HID_KEY_DOWN)
    c = 's';
  else if (k.key_code == HID_KEY_RIGHT)
    c = 'd';
  if (c != 'z' && c != 'q' && c != 's' && c != 'd')
    return;
  uint8_t next = (keyHead + 1) % KEY_QUEUE;
  if (next != keyTail) {
    keyQueue[keyHead] = c | 0x80;
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

char vault13h[] = R"EOF(
// VAULT 13H -- a first-person maze shooter in the style of the early-90s VGA
// games: a 320 x 240 picture in the 256 colours of the VGA default palette
// (mode 13h), each pixel 2 x 2 on the panel: the game fills 640 x 480 in the
// middle of the 800 x 480 screen, black bars on the sides.
// Three looks: the keep (grey stone, blue banners), the crypt (red brick,
// torches), the base (teal metal, hazard stripes); 1 2 3 on the title
// screen pick the one to start in, and each level changes to the next.
// Arrows: walk and turn (held). Space: fire, and start. Escape: quit.
// Reach the green EXIT panel. Drones guard the vault: two hits each. Gold is
// score, red-cross kits are health, green boxes are ammo.
//
// How it is drawn (no frame buffer clear, every view pixel written once):
// - walls: one ray per column through the map grid; each column's 64 texels
//   are shaded once into colc[] and stretched down the column;
// - distance shading uses the palette itself, as VGA games did: the default
//   palette holds each hue three times, bright, medium and dark (72 entries
//   apart), and 16 greys; shadeTab[] maps a colour to its darker copies;
// - sprites (drones, pickups) sorted far to near, clipped by zbuf[];
// - the weapon, the status bar and the text on top.

// the 320-pixel view starts 40 logical pixels in; rows 200..239 are the
// status bar (#define lines take no trailing comment: it would be pasted in)
#define VX 40
#define VW 320
#define VH 200
#define HALF 100
#define MAPW 24
#define MAXE 32
#define NSPAWN 30

// ------------------------------------------------------------ the palette

uint16_t pal[256];        // VGA colour -> panel colour (byte-swapped RGB565)
uint8_t shadeTab[768];    // 3 levels x 256: the colour at distance level s
int egaR[16] = {0, 0, 0, 0, 42, 42, 42, 42, 21, 21, 21, 21, 63, 63, 63, 63};
int egaG[16] = {0, 0, 42, 42, 0, 0, 21, 42, 21, 21, 63, 63, 21, 21, 63, 63};
int egaB[16] = {0, 42, 0, 42, 0, 42, 0, 42, 21, 63, 21, 63, 21, 63, 21, 63};
int grays[16] = {0, 5, 8, 11, 14, 17, 20, 24, 28, 32, 36, 40, 45, 50, 56, 63};
// the 9 ramps of the hue blocks (6-bit levels): bright, medium, dark, each
// vivid, pastel and pale
int ramp[45] = {0, 16, 31, 47, 63, 31, 39, 47, 55, 63, 45, 49, 54, 58, 63,
                0, 7, 14, 21, 28, 14, 17, 21, 24, 28, 20, 22, 24, 26, 28,
                0, 4, 8, 12, 16, 8, 10, 12, 14, 16, 11, 12, 13, 15, 16};
// 24 hues around the wheel: blue, magenta, red, yellow, green, cyan
int hueR[24] = {0, 1, 2, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0};
int hueG[24] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 2, 1};
int hueB[24] = {4, 4, 4, 4, 4, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 4, 4, 4};

uint16_t vga(int r, int g, int b)
{
   int v = ((r >> 1) << 11) | (g << 5) | (b >> 1);
   return ((v & 255) << 8) | (v >> 8);
}

void makePalette()
{
   for (int i = 0; i < 16; i++)
   {
      pal[i] = vga(egaR[i], egaG[i], egaB[i]);
      pal[16 + i] = vga(grays[i], grays[i], grays[i]);
   }
   for (int g = 0; g < 9; g++)
   {
      for (int h = 0; h < 24; h++)
         pal[32 + g * 24 + h] = vga(ramp[g * 5 + hueR[h]], ramp[g * 5 + hueG[h]], ramp[g * 5 + hueB[h]]);
   }
   for (int i = 248; i < 256; i++)
      pal[i] = 0;
   for (int s = 0; s < 3; s++)
   {
      for (int i = 0; i < 256; i++)
      {
         int c = i;
         if (i >= 32 && i < 248)
         {
            int level = (i - 32) / 72 + s;
            if (level > 2)
               level = 2;
            c = 32 + level * 72 + (i - 32) % 72;
         }
         else if (i >= 16 && i < 32)
         {
            c = i - s * 3;
            if (c < 16)
               c = 16;
         }
         shadeTab[s * 256 + i] = c;
      }
   }
}

// ------------------------------------------------------------ the map

// 0 floor, 1 main wall, 2 decorated wall, 3 second wall, 4 door-like wall,
// 5 the EXIT panel
uint8_t worldMap[576] = {
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
   1, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
   1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 1,
   1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 1,
   1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
   1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 4, 0, 1,
   1, 1, 1, 2, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 4, 0, 0, 0, 0, 1,
   1, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 3, 4, 0, 0, 0, 0, 1,
   1, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 1,
   1, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 3, 4, 4, 4, 4, 0, 1,
   1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1,
   1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1,
   1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
   1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1,
   1, 2, 0, 0, 0, 0, 2, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1,
   1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1,
   1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1,
   1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
   1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1,
   1, 0, 4, 0, 4, 0, 4, 0, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1,
   1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
   1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
   1, 0, 4, 0, 4, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 1,
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
};
// spawn points: type (0 drone, 1 gold, 2 kit, 3 ammo) and centre
int spT[30] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3};
float spX[30] = {11.5, 5.5, 13.5, 20.5, 4.5, 12.5, 20.5, 4.5, 12.5, 20.5, 10.5, 17.5,
                 6.5, 15.5, 1.5, 22.5, 11.5, 5.5, 22.5, 2.5, 14.5,
                 7.5, 21.5, 9.5, 18.5, 2.5, 10.5, 15.5, 2.5, 22.5};
float spY[30] = {2.5, 8.5, 8.5, 3.5, 12.5, 14.5, 12.5, 17.5, 17.5, 17.5, 21.5, 20.5,
                 2.5, 4.5, 8.5, 7.5, 12.5, 14.5, 16.5, 20.5, 21.5,
                 4.5, 10.5, 17.5, 22.5, 4.5, 8.5, 12.5, 14.5, 20.5};

// ------------------------------------------------------------ text

uint8_t font8[320] = {126, 199, 207, 219, 243, 227, 126, 0, 24, 56, 120, 24, 24, 24, 126, 0, 126, 195, 6, 28, 112, 192, 255, 0, 126, 195, 3, 62, 3, 195, 126, 0, 14, 30, 54, 102, 255, 6, 6, 0, 255, 192, 254, 3, 3, 195, 126, 0, 62, 96, 192, 254, 195, 195, 126, 0, 255, 195, 6, 12, 24, 24, 24, 0, 126, 195, 195, 126, 195, 195, 126, 0, 126, 195, 195, 127, 3, 6, 124, 0, 60, 102, 195, 195, 255, 195, 195, 0, 254, 195, 195, 254, 195, 195, 254, 0, 126, 195, 192, 192, 192, 195, 126, 0, 252, 198, 195, 195, 195, 198, 252, 0, 255, 192, 192, 252, 192, 192, 255, 0, 255, 192, 192, 252, 192, 192, 192, 0, 126, 195, 192, 207, 195, 195, 126, 0, 195, 195, 195, 255, 195, 195, 195, 0, 126, 24, 24, 24, 24, 24, 126, 0, 63, 6, 6, 6, 198, 198, 124, 0, 198, 204, 216, 240, 216, 204, 198, 0, 192, 192, 192, 192, 192, 192, 255, 0, 195, 231, 255, 219, 195, 195, 195, 0, 195, 227, 243, 219, 207, 199, 195, 0, 126, 195, 195, 195, 195, 195, 126, 0, 254, 195, 195, 254, 192, 192, 192, 0, 126, 195, 195, 195, 203, 198, 123, 0, 254, 195, 195, 254, 204, 198, 195, 0, 126, 195, 192, 126, 3, 195, 126, 0, 255, 24, 24, 24, 24, 24, 24, 0, 195, 195, 195, 195, 195, 195, 126, 0, 195, 195, 195, 195, 102, 60, 24, 0, 195, 195, 195, 219, 255, 231, 195, 0, 195, 102, 60, 24, 60, 102, 195, 0, 195, 195, 102, 60, 24, 24, 24, 0, 255, 6, 12, 24, 48, 96, 255, 0, 0, 0, 0, 0, 0, 192, 192, 0, 0, 192, 192, 0, 0, 192, 192, 0, 3, 6, 12, 24, 48, 96, 192, 0, 0, 24, 24, 126, 24, 24, 0, 0};
uint8_t txt[156] = {31, 10, 30, 21, 29, 255, 1, 3, 17, 28, 25, 10, 12, 14, 255, 29, 24, 255, 28, 29, 10, 27, 29, 21, 14, 31, 14, 21, 28, 12, 24, 27, 14, 17, 14, 10, 21, 29, 17, 10, 22, 22, 24, 21, 14, 31, 14, 21, 255, 12, 21, 14, 10, 27, 34, 24, 30, 255, 13, 18, 14, 13, 29, 17, 14, 255, 20, 14, 14, 25, 29, 17, 14, 255, 12, 27, 34, 25, 29, 29, 17, 14, 255, 11, 10, 28, 14, 10, 27, 27, 24, 32, 28, 255, 22, 24, 31, 14, 255, 255, 28, 25, 10, 12, 14, 255, 15, 18, 27, 14, 15, 18, 23, 13, 255, 29, 17, 14, 255, 14, 33, 18, 29, 28, 25, 10, 12, 14, 255, 29, 24, 255, 27, 14, 29, 27, 34, 14, 28, 12, 255, 26, 30, 18, 29, 24, 30, 29, 255, 24, 15, 255, 10, 22, 22, 24};
int txtStart[16] = {0, 9, 23, 28, 33, 39, 43, 54, 62, 70, 79, 87, 110, 123, 137, 145};
int txtLen[16] = {9, 14, 5, 5, 6, 4, 11, 8, 8, 9, 8, 23, 13, 14, 8, 11};

// ------------------------------------------------------------ textures

uint8_t tex[24576];       // walls 1..5 and a worn main wall: 64 x 64 VGA colours
uint8_t spr[6144];        // 6 sprites 32 x 32, 0 = transparent
uint32_t seed;
int ceilIdx;
int floorIdx;
int theme;

int word[4] = {14, 33, 18, 29};
int bx[4] = {4, 70, 172, 256};
int bw[4] = {62, 98, 80, 60};

int rnd(int n)
{
   seed = seed * 1103515245 + 12345;
   return ((seed >> 16) & 32767) % n;
}

void brickTex(int t, int c0, int c1, int c2, int hi, int mortar, int bw, int bh)
{
   int base = t * 4096;
   for (int y = 0; y < 64; y++)
   {
      int row = y / bh;
      int shift = (row % 2) * (bw / 2);
      for (int x = 0; x < 64; x++)
      {
         int xx = (x + shift) % 64;
         int c = mortar;
         if (y % bh != bh - 1 && xx % bw != bw - 1)
         {
            int k = (row * 7 + (xx / bw) * 13) % 3;
            c = c0;
            if (k == 1)
               c = c1;
            if (k == 2)
               c = c2;
            if (rnd(11) == 0)
               c = c1;
            if (y % bh == 0 || xx % bw == 0)
               c = hi;
         }
         tex[base + y * 64 + x] = c;
      }
   }
}

void panelTex(int t, int plate, int light, int dark, int rivet)
{
   int base = t * 4096;
   for (int y = 0; y < 64; y++)
   {
      for (int x = 0; x < 64; x++)
      {
         int px = x % 32;
         int py = y % 32;
         int c = plate;
         if (rnd(13) == 0)
            c = light;
         if (px == 0 || py == 0)
            c = light;
         if (px == 31 || py == 31)
            c = dark;
         if ((px == 3 || px == 28) && (py == 3 || py == 28))
            c = rivet;
         tex[base + y * 64 + x] = c;
      }
   }
}

void planksTex(int t, int c0, int c1, int dark)
{
   int base = t * 4096;
   for (int x = 0; x < 64; x++)
   {
      int board = x / 16;
      int c = c0;
      if (board % 2 == 1)
         c = c1;
      for (int y = 0; y < 64; y++)
      {
         int v = c;
         if (x % 16 == 0 || x % 16 == 15)
            v = dark;
         else if ((y + board * 23 + x / 4) % 17 == 0)
            v = dark;
         else if (rnd(7) == 0)
            v = c1;
         tex[base + y * 64 + x] = v;
      }
   }
}

void stripesTex(int t, int a, int b, int mid)
{
   int base = t * 4096;
   for (int y = 0; y < 64; y++)
   {
      for (int x = 0; x < 64; x++)
      {
         int c = mid;
         if (y < 12 || y > 51)
         {
            c = b;
            if (((x + y) / 8) % 2 == 0)
               c = a;
         }
         else if (y == 12 || y == 51)
            c = 16;
         else if (rnd(9) == 0)
            c = mid + 1;
         tex[base + y * 64 + x] = c;
      }
   }
}

void put(int t, int x, int y, int c)
{
   if (x >= 0 && x < 64 && y >= 0 && y < 64)
      tex[t * 4096 + y * 64 + x] = c;
}

// ---- richer surfaces: every texel is a step (0 dark .. 3 highlight) on a
// 4-colour ramp, so bevels, grain and grime shade any material

int rk[4];                // the ramp: dark, mid, light, highlight
int cutAt[64];            // stone rows: 1 where a vertical joint is

void setRamp(int a, int b, int c, int d)
{
   rk[0] = a;
   rk[1] = b;
   rk[2] = c;
   rk[3] = d;
}

int rampAt(int v)
{
   if (v < 0)
      v = 0;
   if (v > 3)
      v = 3;
   return rk[v];
}

// some texels a step darker, a few a step lighter
int speckle()
{
   int r = rnd(100);
   if (r < 15)
      return -1;
   if (r < 25)
      return 1;
   return 0;
}

// blocks of rowH lines; regular: bricks of minW, every other row half a
// brick across; else stones from minW to maxW wide
void stoneTex(int t, int mortar, int mortarDark, int rowH, int minW, int maxW, int regular)
{
   int base = t * 4096;
   for (int row = 0; row < 64 / rowH; row++)
   {
      for (int x = 0; x < 64; x++)
         cutAt[x] = 0;
      int x0 = rnd(maxW);
      if (regular)
         x0 = (row % 2) * (minW / 2);
      int x = x0;
      while (x < x0 + 64)
      {
         cutAt[x % 64] = 1;
         if (regular)
            x = x + minW;
         else
            x = x + minW + rnd(maxW - minW + 1);
      }
      int shade = 1;
      for (int xx = 0; xx < 64; xx++)
      {
         if (cutAt[xx])
         {
            // one rnd() a statement: the order of two in one expression
            // is up to the compiler
            shade = 1 + rnd(2);
            shade = shade - rnd(4) / 3;
         }
         for (int yy = 0; yy < rowH; yy++)
         {
            int y = row * rowH + yy;
            int c = mortar;
            if (rnd(3) == 0)
               c = mortarDark;
            if (yy != rowH - 1 && !cutAt[xx])
            {
               int v = shade + speckle();
               if (yy == 0 || cutAt[(xx + 63) % 64])
                  v = 3;
               else if (yy == 1)
                  v = v + 1;
               if (yy == rowH - 2 || cutAt[(xx + 1) % 64])
                  v = 0;
               c = rampAt(v);
               if (rnd(90) == 0)
                  c = mortarDark;
            }
            tex[base + y * 64 + xx] = c;
         }
      }
   }
}

// cracks: short dark lines wandering down
void cracksDeco(int t, int n, int c)
{
   for (int i = 0; i < n; i++)
   {
      int x = rnd(64);
      int y = rnd(48);
      int len = 8 + rnd(14);
      for (int k = 0; k < len; k++)
      {
         put(t, x, y, c);
         y++;
         x = x + rnd(3) - 1;
      }
   }
}

// soot or rust: darker blotches toward the floor
void grimeDeco(int t, int n)
{
   for (int i = 0; i < n; i++)
   {
      int x = rnd(64);
      int y = 64 - rnd(28);
      y = y - rnd(16);
      for (int k = 0; k < 5; k++)
      {
         int xx = x + rnd(5) - 2;
         int yy = y + rnd(5) - 2;
         if (xx >= 0 && xx < 64 && yy >= 0 && yy < 64)
         {
            int c = tex[t * 4096 + yy * 64 + xx];
            if (c >= 32 && c < 176)
               c = c + 72;
            else if (c > 17 && c < 32)
               c = c - 2;
            tex[t * 4096 + yy * 64 + xx] = c;
         }
      }
   }
}

// brushed metal plates: vertical streaks, bevelled edges, rivets with a
// shadow, grime rising from the floor
void metalTex(int t, int size, int grime)
{
   int base = t * 4096;
   for (int x = 0; x < 64; x++)
   {
      int streak = speckle();
      for (int y = 0; y < 64; y++)
      {
         int px = x % size;
         int py = y % size;
         int v = 1 + streak;
         if (rnd(100) < 6)
            v = v + 1;
         if (grime && y > 40 && rnd(100) < (y - 40) * 2)
            v = v - 1;
         if (px == 1 || py == 1)
            v = 2;
         if (px == 0 || py == 0)
            v = 3;
         if (px == size - 1 || py == size - 1)
            v = 0;
         if ((px == 4 || px == size - 5) && (py == 4 || py == size - 5))
            v = 3;
         if ((px == 5 || px == size - 4) && (py == 5 || py == size - 4))
            v = 0;
         tex[base + y * 64 + x] = rampAt(v);
      }
   }
}

// boards 16 wide: wavy grain, knots, nails, bevelled edges
void woodTex(int t)
{
   int base = t * 4096;
   for (int b = 0; b < 4; b++)
   {
      int shade = 1 + rnd(2);
      int kx = b * 16 + 4 + rnd(8);
      int ky = 8 + rnd(48);
      for (int xx = 0; xx < 16; xx++)
      {
         int x = b * 16 + xx;
         for (int y = 0; y < 64; y++)
         {
            int v = shade;
            int w = (y * 2 + (xx - 8) * (xx - 8) / 3 + b * 11) % 13;
            if (w == 0)
               v = v - 1;
            if (w == 1 && rnd(3) == 0)
               v = v - 1;
            if (rnd(100) < 8)
               v = v + 1;
            int dx = x - kx;
            int dy = y - ky;
            int r2 = dx * dx * 3 + dy * dy;
            if (r2 < 7)
               v = 0;
            else if (r2 < 20)
               v = shade - 1;
            if (xx == 0)
               v = 3;
            if (xx == 15)
               v = 0;
            if ((y == 6 || y == 57) && (xx == 3 || xx == 12))
               v = 0;
            tex[base + y * 64 + x] = rampAt(v);
         }
      }
   }
}

// air vent: dark slots with lit lips
void ventDeco(int t)
{
   for (int y = 20; y < 44; y++)
   {
      for (int x = 14; x < 50; x++)
      {
         int c = 17;
         if (y % 4 == 0)
            c = rk[3];
         if (y % 4 == 1)
            c = rk[2];
         if (x == 14 || y == 20)
            c = rk[3];
         if (x == 49 || y == 43)
            c = rk[0];
         put(t, x, y, c);
      }
   }
}

void bannerDeco(int t, int cloth, int trim)
{
   for (int y = 6; y < 56; y++)
   {
      for (int x = 20; x < 44; x++)
      {
         int c = cloth;
         if (x == 20 || x == 43 || y == 6)
            c = trim;
         if (y > 49 && (x - 20) % 6 < 3)
            c = trim;
         int dx = x - 32;
         int dy = y - 26;
         int d2 = dx * dx + dy * dy;
         if (d2 < 49 && d2 > 24)
            c = trim;
         if (d2 < 6)
            c = trim;
         put(t, x, y, c);
      }
   }
}

void torchDeco(int t)
{
   for (int y = 34; y < 44; y++)
      for (int x = 29; x < 35; x++)
         put(t, x, y, 18);
   for (int x = 26; x < 38; x++)
      put(t, x, 34, 21);
   for (int y = 16; y < 34; y++)
   {
      for (int x = 24; x < 40; x++)
      {
         int dx = x - 32;
         int dy = y - 28;
         int r = dx * dx * 4 + dy * dy;
         if (r < 40)
            put(t, x, y, 92);
         else if (r < 90)
            put(t, x, y, 44);
         else if (r < 150 && y > 18)
            put(t, x, y, 42);
      }
   }
}

void lightsDeco(int t)
{
   for (int y = 10; y < 54; y++)
      for (int x = 10; x < 54; x++)
         put(t, x, y, 17);
   for (int y = 10; y < 54; y++)
   {
      put(t, 10, y, 21);
      put(t, 53, y, 25);
   }
   for (int y = 14; y < 50; y = y + 6)
   {
      for (int x = 14; x < 50; x = x + 6)
      {
         int k = rnd(5);
         int c = 48;
         if (k == 1)
            c = 40;
         if (k == 2)
            c = 44;
         if (k == 3)
            c = 17;
         put(t, x, y, c);
         put(t, x + 1, y, c);
         put(t, x, y + 1, c);
         put(t, x + 1, y + 1, c);
      }
   }
}

void mossDeco(int t)
{
   for (int i = 0; i < 260; i++)
   {
      int x = rnd(64);
      int y = 40 + rnd(24);
      y = y - rnd(30);
      int c = 144;
      if (rnd(3) == 0)
         c = 120;
      put(t, x, y, c);
   }
}

void bandsDeco(int t)
{
   for (int x = 0; x < 64; x++)
   {
      for (int y = 10; y < 14; y++)
         put(t, x, y, 19);
      for (int y = 50; y < 54; y++)
         put(t, x, y, 19);
      put(t, x, 10, 23);
      put(t, x, 50, 23);
   }
   for (int y = 28; y < 36; y++)
      for (int x = 48; x < 54; x++)
         put(t, x, y, 44);
}

void exitTex(int t)
{
   panelTex(t, 192, 120, 16, 48);
   for (int y = 18; y < 46; y++)
      for (int x = 6; x < 58; x++)
         put(t, x, y, 16);
   // E X I T in the 8x8 font, 1.5x wide
   for (int i = 0; i < 4; i++)
   {
      for (int r = 0; r < 8; r++)
      {
         int bits = font8[word[i] * 8 + r];
         for (int b = 0; b < 8; b++)
         {
            if ((bits >> (7 - b)) & 1)
            {
               put(t, 9 + i * 12 + b, 24 + r * 2, 72);
               put(t, 9 + i * 12 + b, 25 + r * 2, 48);
            }
         }
      }
   }
}

// sprites 32 x 32
void sput(int s, int x, int y, int c)
{
   if (x >= 0 && x < 32 && y >= 0 && y < 32)
      spr[s * 1024 + y * 32 + x] = c;
}

void makeSprites()
{
   for (int i = 0; i < 6144; i++)
      spr[i] = 0;
   // 0 drone, 1 drone firing: a metal ball with one eye
   for (int s = 0; s < 2; s++)
   {
      for (int y = 2; y < 30; y++)
      {
         for (int x = 2; x < 30; x++)
         {
            int dx = x - 16;
            int dy = y - 15;
            if (dx * dx + dy * dy <= 169)
            {
               int lx = x - 11;
               int ly = y - 9;
               int v = 30 - (lx * lx + ly * ly) / 22;
               if (v < 18)
                  v = 18;
               sput(s, x, y, v);
            }
         }
      }
      for (int y = 13; y < 18; y++)
      {
         sput(s, 1, y, 21);
         sput(s, 2, y, 21);
         sput(s, 30, y, 21);
         sput(s, 29, y, 21);
      }
      sput(s, 16, 0, 40);
      sput(s, 16, 1, 19);
      for (int y = 10; y < 21; y++)
      {
         for (int x = 11; x < 22; x++)
         {
            int dx = x - 16;
            int dy = y - 15;
            int d2 = dx * dx + dy * dy;
            int c = 0;
            if (d2 <= 25)
               c = 16;
            if (d2 <= 12)
            {
               c = 40;
               if (s == 1)
                  c = 44;
            }
            if (d2 <= 3)
            {
               c = 92;
               if (s == 0)
                  c = 66;
            }
            if (c != 0)
               sput(s, x, y, c);
         }
      }
   }
   // 2 wreck: scrap on the floor
   for (int i = 0; i < 160; i++)
   {
      int x = 4 + rnd(24);
      int y = 22 + rnd(10);
      sput(2, x, y, 18 + rnd(6));
   }
   for (int x = 6; x < 26; x++)
      sput(2, x, 31, 17);
   sput(2, 12, 24, 40);
   sput(2, 13, 24, 40);
   // 3 gold: a pile of coins
   for (int y = 20; y < 32; y++)
   {
      for (int x = 4; x < 28; x++)
      {
         int dx = x - 16;
         int dy = (y - 32) * 2;
         if (dx * dx + dy * dy < 150)
         {
            int c = 44;
            if ((x + y * 3) % 7 == 0)
               c = 116;
            if ((x * 5 + y) % 11 == 0)
               c = 92;
            sput(3, x, y, c);
         }
      }
   }
   // 4 health kit: a white box, a red cross
   for (int y = 18; y < 32; y++)
   {
      for (int x = 6; x < 26; x++)
      {
         int c = 31;
         if (y == 18 || x == 6 || x == 25)
            c = 27;
         if (y == 31)
            c = 23;
         if ((x >= 14 && x < 18 && y >= 20 && y < 30) || (y >= 23 && y < 27 && x >= 10 && x < 22))
            c = 40;
         sput(4, x, y, c);
      }
   }
   // 5 ammo: a green box of shells
   for (int y = 20; y < 32; y++)
   {
      for (int x = 7; x < 25; x++)
      {
         int c = 120;
         if (y == 20 || x == 7 || x == 24)
            c = 48;
         if (y == 31)
            c = 192;
         sput(5, x, y, c);
      }
   }
   for (int i = 0; i < 4; i++)
   {
      for (int y = 15; y < 20; y++)
      {
         int c = 116;
         if (y < 17)
            c = 44;
         sput(5, 10 + i * 4, y, c);
         sput(5, 11 + i * 4, y, c);
      }
   }
}

// the looks: 0 keep, 1 crypt, 2 base
void makeTextures(int th)
{
   seed = 1234 + th * 77;
   if (th == 0)
   {
      // grey stone of uneven blocks, a blue banner, oak, a steel door
      setRamp(20, 22, 24, 27);
      stoneTex(0, 18, 17, 16, 12, 30, 0);
      stoneTex(1, 18, 17, 16, 12, 30, 0);
      bannerDeco(1, 32, 44);
      stoneTex(5, 18, 17, 16, 12, 30, 0);
      cracksDeco(5, 7, 17);
      grimeDeco(5, 40);
      setRamp(186, 114, 115, 139);
      woodTex(2);
      setRamp(21, 24, 27, 30);
      metalTex(3, 32, 1);
      ceilIdx = 21;
      floorIdx = 24;
   }
   else if (th == 1)
   {
      // red brick, a torch, mossy stone, a banded oak door
      setRamp(184, 112, 113, 137);
      stoneTex(0, 19, 17, 8, 16, 16, 1);
      stoneTex(1, 19, 17, 8, 16, 16, 1);
      torchDeco(1);
      stoneTex(5, 19, 17, 8, 16, 16, 1);
      cracksDeco(5, 6, 17);
      mossDeco(5);
      setRamp(19, 21, 23, 25);
      stoneTex(2, 17, 16, 16, 10, 26, 0);
      mossDeco(2);
      grimeDeco(2, 30);
      setRamp(186, 114, 115, 139);
      woodTex(3);
      bandsDeco(3);
      ceilIdx = 17;
      floorIdx = 210;
   }
   else
   {
      // teal plates, a computer wall, hazard stripes, grey plates
      setRamp(196, 148, 172, 27);
      metalTex(0, 32, 1);
      metalTex(1, 32, 0);
      lightsDeco(1);
      metalTex(5, 32, 1);
      ventDeco(5);
      grimeDeco(5, 30);
      stripesTex(2, 44, 16, 22);
      setRamp(20, 23, 26, 29);
      metalTex(3, 16, 1);
      ceilIdx = 18;
      floorIdx = 22;
   }
   exitTex(4);
}

// ------------------------------------------------------------ the game

float px;
float py;
float dirX;
float dirY;
float planeX;
float planeY;
float zbuf[320];
uint16_t colc[64];
uint16_t ceilA[200];      // per view row: ceiling/floor colour, and the
uint16_t ceilB[200];      // dithered second colour (odd columns)

float ex[32];
float ey[32];
int et[32];               // 0 drone, 1 gold, 2 kit, 3 ammo
int es[32];               // 0 waiting, 1 chasing, 2 dead / taken
int ehp[32];
float efire[32];
float elos[32];
float eanim[32];
int eseen[32];
int ne;
int order[32];
float odist[32];

int health;
int ammo;
int score;
int level;
int lives;
int state;                // 0 title, 1 playing, 2 level clear, 3 dead
float stateT;
float cool;
float flash;
float hurt;
float glow;
float walk;
int kUp;
int kDown;
int kLeft;
int kRight;
int startTheme;

float fabsf2(float v)
{
   if (v < 0)
      return -v;
   return v;
}

float cosf2(float a)
{
   return sin(a + 1.5707963);
}

int solid(float x, float y)
{
   int mx = x;
   int my = y;
   if (mx < 0 || my < 0 || mx >= MAPW || my >= MAPW)
      return 1;
   return worldMap[my * MAPW + mx];
}

int lineOfSight(float ax, float ay, float bx, float by)
{
   float dx = bx - ax;
   float dy = by - ay;
   float d = hypot(dx, dy);
   int n = d * 4;
   for (int i = 1; i < n; i++)
   {
      float f = i * 1.0 / n;
      if (solid(ax + dx * f, ay + dy * f) != 0)
         return 0;
   }
   return 1;
}

void makeSky()
{
   for (int y = 0; y < VH; y++)
   {
      int idx = ceilIdx;
      int d = HALF - y;            // rows from the horizon
      if (y >= HALF)
      {
         idx = floorIdx;
         d = y - HALF + 1;
      }
      int s = 2;
      int s2 = 2;
      if (d > 55)
      {
         s = 0;
         s2 = 0;
      }
      else if (d > 22)
      {
         s = 1;
         s2 = 1;
      }
      // two dithered rows on each band edge
      if (d > 55 && d <= 58)
         s2 = 1;
      if (d > 22 && d <= 25)
         s2 = 2;
      ceilA[y] = pal[shadeTab[s * 256 + idx]];
      ceilB[y] = pal[shadeTab[s2 * 256 + idx]];
   }
}

void addEntity(int t, float x, float y)
{
   if (ne >= MAXE)
      return;
   ex[ne] = x;
   ey[ne] = y;
   et[ne] = t;
   es[ne] = 0;
   ehp[ne] = 2;
   efire[ne] = 1.5 + rnd(100) / 100.0;
   elos[ne] = 0;
   eanim[ne] = 0;
   eseen[ne] = 0;
   ne++;
}

void setupLevel()
{
   theme = (startTheme + level - 1) % 3;
   makeTextures(theme);
   makeSky();
   px = 1.5;
   py = 1.5;
   dirX = 1;
   dirY = 0;
   planeX = 0;
   planeY = 0.66;
   ne = 0;
   int drones = 4 + level * 2;
   if (drones > 12)
      drones = 12;
   for (int i = 0; i < NSPAWN; i++)
   {
      if (spT[i] != 0 || i < drones)
         addEntity(spT[i], spX[i], spY[i]);
   }
   kUp = 0;
   kDown = 0;
   kLeft = 0;
   kRight = 0;
}

void newGame()
{
   health = 100;
   ammo = 24;
   score = 0;
   level = 1;
   setupLevel();
}

// ------------------------------------------------------------ drawing

void castWalls()
{
   int st = 800;
   for (int x = 0; x < VW; x++)
   {
      float camX = 2.0 * x / VW - 1.0;
      float rx = dirX + planeX * camX;
      float ry = dirY + planeY * camX;
      int mx = px;
      int my = py;
      float ddx = 1000000.0;
      float ddy = 1000000.0;
      if (rx != 0)
         ddx = fabsf2(1.0 / rx);
      if (ry != 0)
         ddy = fabsf2(1.0 / ry);
      int sx = 1;
      int sy = 1;
      float sdx = (mx + 1.0 - px) * ddx;
      float sdy = (my + 1.0 - py) * ddy;
      if (rx < 0)
      {
         sx = -1;
         sdx = (px - mx) * ddx;
      }
      if (ry < 0)
      {
         sy = -1;
         sdy = (py - my) * ddy;
      }
      int side = 0;
      int w = 0;
      int n = 0;
      while (w == 0 && n < 64)
      {
         if (sdx < sdy)
         {
            sdx = sdx + ddx;
            mx = mx + sx;
            side = 0;
         }
         else
         {
            sdy = sdy + ddy;
            my = my + sy;
            side = 1;
         }
         w = worldMap[my * MAPW + mx];
         n++;
      }
      float dist = sdy - ddy;
      if (side == 0)
         dist = sdx - ddx;
      if (dist < 0.05)
         dist = 0.05;
      zbuf[x] = dist;
      int lineH = VH / dist;
      float wx = px + dist * rx;
      if (side == 0)
         wx = py + dist * ry;
      int wi = wx;
      int tx = (wx - wi) * 64;
      // y grows southward on the map: these faces would show the texture
      // mirrored (the EXIT text backwards)
      if (side == 0 && rx < 0)
         tx = 63 - tx;
      if (side == 1 && ry > 0)
         tx = 63 - tx;
      int s = 2;
      if (dist < 3.0)
         s = 0;
      else if (dist < 7.0)
         s = 1;
      if (side == 1 && s < 2)
         s = s + 1;
      int slot = w - 1;
      if (w == 1 && (mx * 7 + my * 13) % 4 == 0)
         slot = 5;              // some cells show the worn main wall
      int tb = slot * 4096 + tx;
      int sb = s * 256;
      for (int t = 0; t < 64; t++)
         colc[t] = pal[shadeTab[sb + tex[tb + t * 64]]];
      int top = HALF - lineH / 2;
      int bot = top + lineH;
      int step = (64 * 65536) / lineH;
      int tpos = 0;
      if (top < 0)
      {
         tpos = -top * step;
         top = 0;
      }
      if (bot > VH)
         bot = VH;
      int off = VX + x;
      int odd = x & 1;
      for (int y = 0; y < top; y++)
      {
         if (odd)
            led16[off] = ceilB[y];
         else
            led16[off] = ceilA[y];
         off = off + st;
      }
      for (int y = top; y < bot; y++)
      {
         led16[off] = colc[(tpos >> 16) & 63];
         tpos = tpos + step;
         off = off + st;
      }
      for (int y = bot; y < VH; y++)
      {
         if (odd)
            led16[off] = ceilB[y];
         else
            led16[off] = ceilA[y];
         off = off + st;
      }
   }
}

void drawSprites()
{
   int st = 800;
   int n = 0;
   for (int i = 0; i < ne; i++)
   {
      if (es[i] != 2 || et[i] == 0)
      {
         float dx = ex[i] - px;
         float dy = ey[i] - py;
         order[n] = i;
         odist[n] = dx * dx + dy * dy;
         n++;
      }
   }
   // far to near
   for (int i = 1; i < n; i++)
   {
      int o = order[i];
      float d = odist[i];
      int j = i - 1;
      while (j >= 0 && odist[j] < d)
      {
         order[j + 1] = order[j];
         odist[j + 1] = odist[j];
         j--;
      }
      order[j + 1] = o;
      odist[j + 1] = d;
   }
   float inv = 1.0 / (planeX * dirY - dirX * planeY);
   for (int k = 0; k < n; k++)
   {
      int i = order[k];
      float sx = ex[i] - px;
      float sy = ey[i] - py;
      float tx = inv * (dirY * sx - dirX * sy);
      float ty = inv * (planeX * sy - planeY * sx);
      if (ty > 0.15)
      {
         int scr = (VW / 2) * (1.0 + tx / ty);
         int h = VH / ty;
         int top = HALF - h / 2;
         int left = scr - h / 2;
         int sp = 3;
         if (et[i] == 2)
            sp = 4;
         if (et[i] == 3)
            sp = 5;
         if (et[i] == 0)
         {
            sp = 0;
            if (eanim[i] > 0)
               sp = 1;
            if (es[i] == 2)
               sp = 2;
         }
         int s = 2;
         if (ty < 3.0)
            s = 0;
         else if (ty < 7.0)
            s = 1;
         int sb = s * 256;
         int x0 = left;
         if (x0 < 0)
            x0 = 0;
         int x1 = left + h;
         if (x1 > VW)
            x1 = VW;
         int y0 = top;
         if (y0 < 0)
            y0 = 0;
         int y1 = top + h;
         if (y1 > VH)
            y1 = VH;
         for (int x = x0; x < x1; x++)
         {
            if (ty < zbuf[x])
            {
               int tcol = ((x - left) * 32) / h;
               int base = sp * 1024 + tcol;
               int off = y0 * st + VX + x;
               for (int y = y0; y < y1; y++)
               {
                  int c = spr[base + (((y - top) * 32) / h) * 32];
                  if (c != 0)
                     led16[off] = pal[shadeTab[sb + c]];
                  off = off + st;
               }
            }
         }
      }
   }
}

void fillRect(int x, int y, int w, int h, int c)
{
   int st = 800;
   uint16_t col = pal[c];
   for (int j = y; j < y + h; j++)
   {
      int off = j * st + VX + x;
      for (int i = 0; i < w; i++)
         led16[off + i] = col;
   }
}

void drawGun()
{
   int bob = sin(walk) * 3;
   if (bob < 0)
      bob = -bob;
   int gx = 146;
   int gy = 150 + bob;
   if (flash > 0)
   {
      gy = gy + 4;
      for (int y = -16; y <= 16; y++)
      {
         for (int x = -16; x <= 16; x++)
         {
            int r = x * x + y * y;
            int ray = 0;
            if (x == 0 || y == 0 || x == y || x == -y)
               ray = 1;
            int c = 0;
            if (r < 30)
               c = 31;
            else if (r < 80)
               c = 92;
            else if (r < 150)
               c = 44;
            else if (r < 256 && ray)
               c = 42;
            int yy = gy - 10 + y;
            if (c != 0 && yy >= 0)
               fillRect(160 + x, yy, 1, 1, c);
         }
      }
   }
   // the barrel and the body, lit from the left
   for (int x = 0; x < 28; x++)
   {
      int c = 27 - x / 4;
      int y0 = gy;
      if (x < 4 || x > 23)
         y0 = gy + 14;
      int h = VH - y0;
      if (h > 0)
         fillRect(gx + x, y0, 1, h, c);
   }
   fillRect(gx + 8, gy - 2, 12, 3, 18);
   fillRect(gx + 11, gy + 20, 6, 4, 40);
}

void drawChar(int g, int x, int y, int sc, int c)
{
   if (g == 255)
      return;
   int st = 800;
   uint16_t col = pal[c];
   for (int r = 0; r < 8; r++)
   {
      int bits = font8[g * 8 + r];
      for (int b = 0; b < 8; b++)
      {
         if ((bits >> (7 - b)) & 1)
         {
            for (int j = 0; j < sc; j++)
            {
               int off = (y + r * sc + j) * st + VX + x + b * sc;
               for (int i = 0; i < sc; i++)
                  led16[off + i] = col;
            }
         }
      }
   }
}

void drawMsg(int id, int x, int y, int sc, int c, int shadow)
{
   int n = txtLen[id];
   if (x < 0)
      x = (VW - n * 8 * sc) / 2;
   for (int i = 0; i < n; i++)
   {
      int g = txt[txtStart[id] + i];
      if (shadow >= 0)
         drawChar(g, x + i * 8 * sc + sc, y + sc, sc, shadow);
      drawChar(g, x + i * 8 * sc, y, sc, c);
   }
}

void drawNumber(int v, int digits, int x, int y, int sc, int c)
{
   for (int i = digits - 1; i >= 0; i--)
   {
      drawChar(v % 10, x + i * 8 * sc, y, sc, c);
      v = v / 10;
   }
}

void drawHud()
{
   fillRect(0, 200, VW, 40, 1);
   fillRect(0, 200, VW, 1, 9);
   fillRect(0, 239, VW, 1, 16);
   // four sunken boxes
   for (int i = 0; i < 4; i++)
   {
      fillRect(bx[i], 203, bw[i], 34, 16);
      fillRect(bx[i], 203, bw[i], 1, 8);
      fillRect(bx[i], 236, bw[i], 1, 9);
      drawMsg(2 + i, bx[i] + (bw[i] - txtLen[2 + i] * 8) / 2, 206, 1, 11, -1);
   }
   drawNumber(level, 2, 4 + 23, 218, 1, 14);
   drawNumber(score, 6, 70 + 25, 218, 1, 14);
   int hc = 14;
   if (health < 25)
      hc = 12;
   drawNumber(health, 3, 172 + 28, 218, 1, hc);
   int ac = 14;
   if (ammo == 0)
      ac = 12;
   drawNumber(ammo, 2, 256 + 22, 218, 1, ac);
}

void drawBorder(int c, int w)
{
   fillRect(0, 0, VW, w, c);
   fillRect(0, VH - w, VW, w, c);
   fillRect(0, 0, w, VH, c);
   fillRect(VW - w, 0, w, VH, c);
}

void drawFrame()
{
   castWalls();
   drawSprites();
   if (state == 1)
      drawGun();
   if (hurt > 0)
      drawBorder(40, 4);
   else if (glow > 0)
      drawBorder(44, 3);
   if (state == 0)
   {
      drawMsg(0, -1, 34, 4, 44, 186);
      drawMsg(8 + theme, -1, 76, 2, 31, 16);
      drawMsg(11, -1, 140, 1, 11, 16);
      drawMsg(12, -1, 152, 1, 11, 16);
      if ((millis() / 500) % 2 == 0)
         drawMsg(1, -1, 172, 2, 31, 16);
   }
   if (state == 2)
   {
      drawMsg(6, -1, 70, 3, 48, 16);
      drawMsg(8 + (startTheme + level) % 3, -1, 110, 2, 31, 16);
   }
   if (state == 3)
   {
      drawMsg(7, -1, 70, 4, 40, 16);
      drawMsg(13, -1, 120, 2, 31, 16);
   }
   drawHud();
}

// ------------------------------------------------------------ the rules

void tryMove(float nx, float ny)
{
   float m = 0.22;
   int hit = 0;
   float ox = nx + m;
   if (nx < px)
      ox = nx - m;
   int w = solid(ox, py);
   if (w == 0)
      px = nx;
   else
      hit = w;
   float oy = ny + m;
   if (ny < py)
      oy = ny - m;
   w = solid(px, oy);
   if (w == 0)
      py = ny;
   else if (hit == 0)
      hit = w;
   if (hit == 5 && state == 1)
   {
      state = 2;
      stateT = 2.5;
      score = score + 500;
   }
}

void turn(float a)
{
   float c = cosf2(a);
   float s = sin(a);
   float d = dirX;
   dirX = dirX * c - dirY * s;
   dirY = d * s + dirY * c;
   float p = planeX;
   planeX = planeX * c - planeY * s;
   planeY = p * s + planeY * c;
}

void fire()
{
   if (cool > 0)
      return;
   if (ammo <= 0)
      return;
   ammo--;
   cool = 0.35;
   flash = 0.12;
   float inv = 1.0 / (planeX * dirY - dirX * planeY);
   int best = -1;
   float bestD = zbuf[VW / 2];
   for (int i = 0; i < ne; i++)
   {
      if (et[i] == 0 && es[i] != 2)
      {
         float sx = ex[i] - px;
         float sy = ey[i] - py;
         float tx = inv * (dirY * sx - dirX * sy);
         float ty = inv * (planeX * sy - planeY * sx);
         if (ty > 0.2 && ty < bestD)
         {
            int scr = (VW / 2) * (1.0 + tx / ty);
            int h = VH / ty;
            int d = scr - VW / 2;
            if (d < 0)
               d = -d;
            if (d < h / 3)
            {
               best = i;
               bestD = ty;
            }
         }
      }
   }
   if (best >= 0)
   {
      ehp[best] = ehp[best] - 1;
      es[best] = 1;
      eanim[best] = 0;
      if (ehp[best] <= 0)
      {
         es[best] = 2;
         score = score + 100;
      }
   }
}

void updateEntities(float dt)
{
   for (int i = 0; i < ne; i++)
   {
      if (es[i] != 2)
      {
         float dx = px - ex[i];
         float dy = py - ey[i];
         float d = hypot(dx, dy);
         if (et[i] != 0)
         {
            if (d < 0.6)
            {
               if (et[i] == 1)
               {
                  score = score + 50;
                  es[i] = 2;
               }
               if (et[i] == 2 && health < 100)
               {
                  health = health + 25;
                  if (health > 100)
                     health = 100;
                  es[i] = 2;
               }
               if (et[i] == 3 && ammo < 99)
               {
                  ammo = ammo + 12;
                  if (ammo > 99)
                     ammo = 99;
                  es[i] = 2;
               }
               if (es[i] == 2)
                  glow = 0.25;
            }
         }
         else
         {
            elos[i] = elos[i] - dt;
            if (elos[i] <= 0)
            {
               elos[i] = 0.25;
               eseen[i] = 0;
               if (d < 12.0)
                  eseen[i] = lineOfSight(ex[i], ey[i], px, py);
               if (eseen[i])
                  es[i] = 1;
            }
            eanim[i] = eanim[i] - dt;
            if (es[i] == 1 && d > 1.4)
            {
               float sp = (1.1 + level * 0.1) * dt / d;
               float nx = ex[i] + dx * sp;
               float ny = ey[i] + dy * sp;
               if (solid(nx, ey[i]) == 0)
                  ex[i] = nx;
               if (solid(ex[i], ny) == 0)
                  ey[i] = ny;
            }
            efire[i] = efire[i] - dt;
            if (eseen[i] && efire[i] <= 0)
            {
               efire[i] = 1.1 + rnd(120) / 100.0;
               eanim[i] = 0.25;
               int chance = 80 - d * 6;
               if (rnd(100) < chance)
               {
                  health = health - (4 + rnd(8));
                  hurt = 0.3;
                  if (health <= 0)
                  {
                     health = 0;
                     state = 3;
                  }
               }
            }
         }
      }
   }
}

void handleKey(int k)
{
   if (k == 122 || k == 90)
      kUp = 1;
   if (k == 115 || k == 83)
      kDown = 1;
   if (k == 113 || k == 81)
      kLeft = 1;
   if (k == 100 || k == 68)
      kRight = 1;
   if (k == 250)
      kUp = 0;
   if (k == 243)
      kDown = 0;
   if (k == 241)
      kLeft = 0;
   if (k == 228)
      kRight = 0;
   if (state == 0 && k >= 49 && k <= 51)
   {
      // 1 2 3 on the title screen: the look to start in
      startTheme = k - 49;
      level = 1;
      setupLevel();
      px = 2.5;
      py = 12.5;
      return;
   }
   if (k == 32)
   {
      if (state == 0)
      {
         newGame();
         state = 1;
      }
      else if (state == 3)
      {
         newGame();
         state = 1;
      }
      else if (state == 1)
         fire();
   }
}

void step(float dt)
{
   cool = cool - dt;
   flash = flash - dt;
   hurt = hurt - dt;
   glow = glow - dt;
   if (state == 0)
   {
      turn(dt * 0.25);
      return;
   }
   if (state == 2)
   {
      stateT = stateT - dt;
      if (stateT <= 0)
      {
         level++;
         setupLevel();
         state = 1;
      }
      return;
   }
   if (state != 1)
      return;
   if (kLeft)
      turn(-2.4 * dt);
   if (kRight)
      turn(2.4 * dt);
   float mv = 0;
   if (kUp)
      mv = 3.2 * dt;
   if (kDown)
      mv = -2.2 * dt;
   if (mv != 0)
   {
      tryMove(px + dirX * mv, py + dirY * mv);
      walk = walk + dt * 9.0;
   }
   updateEntities(dt);
}

void main()
{
   setPixelsize(2);              // 400 x 240, each 2 x 2: 320 x 240 is 640 x 480
   startTheme = 0;
   makePalette();
   makeSprites();
   level = 1;
   health = 100;
   ammo = 24;
   setupLevel();
   px = 2.5;
   py = 12.5;
   state = 0;
   uint32_t last = millis();
   while (true)
   {
      int k = readKey();
      while (k != 0)
      {
         if (k == 27)
            return;
         handleKey(k);
         k = readKey();
      }
      uint32_t now = millis();
      float dt = (now - last) / 1000.0;
      last = now;
      if (dt > 0.1)
         dt = 0.1;
      step(dt);
      drawFrame();
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
  bindFunction((char *)"void", (char *)"show16", NULL, (void *)show16);
  bindFunction((char *)"void", (char *)"setPixelsize", (char *)"int", (void *)setPixelsize);
  bindFunction((char *)"uint32_t", (char *)"millis", NULL, (void *)scriptMillis);
  bindFunction((char *)"uint32_t", (char *)"rand", (char *)"uint32_t", (void *)scriptRand);
  bindFunction((char *)"int", (char *)"readKey", NULL, (void *)readKey);
  bindFunction((char *)"float", (char *)"sin", (char *)"float", (void *)scriptSin);
  bindFunction((char *)"float", (char *)"hypot", (char *)"float,float", (void *)scriptHypot);

  static ScriptExecutable exec = parseScript(vault13h);
  if (!exec.isExeExists()) {
    showCompileError();
    return;
  }
  printf("vault13h: %u bytes of code\n", (unsigned)exec.exe.binary_size);

  // the USB keyboard: keys go to print_char(), releases to release_char()
  hid_keyboard_print_char = print_char;
  hid_keyboard_release_char = release_char;  // walking and turning last while a key is held
  app_main_keyboard();

  exec.execute("main");
}

void loop() {
  delay(1000);
}
