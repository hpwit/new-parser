// Ironforge: a first-person shooter with the technology of the mid-90s, on
// the Guition ESP32-4848S040 (a 4-inch 480x480 touch screen).
//
// The engine: a grid of cells, each with its own floor and ceiling height
// (or the sky), floor and ceiling textures and light: stairs, ledges, a
// balcony, a toxic pool, a yard under the sky, doors that rise into the
// ceiling, a blue key, an exit switch. One ray per view column goes through
// the cells, drawing each one's floor and ceiling, the steps between them
// and the walls; the monsters and pickups are sprites, hidden behind ledges
// with the rows each column still had open at their distance. All the art
// is made by the script at the start; the palette, the wall textures'
// makers, the drone, the pickups and the face come from Vault13H.
//
// Pixel size 2 makes the panel 240x240: the view is the top 240x144 (480x288
// on the panel), then the status bar and the touch pad, drawn by the script.
// The sketch reads the touch screen (up to three fingers) and sets a bit per
// button held in touchHeld:
//   the cross   forward, back, turn (a finger between two arms does both)
//   FIRE        fire, held for more; starts a game
//   USE         opens the door ahead, presses the exit switch
//   GUN         the next gun: the pistol, the shotgun, the chaingun (once found)
// The level (cells and things) is drawn as text in tools/data.py, which
// writes the arrays pasted below. Once a second, the frame rate goes to the
// serial port.
//
// The monsters come from a WAD file on the FFat partition, /ironforge.wad
// (data/ironforge.wad, made by tools/wad.py): the husk, the drone, the wisp
// and the fireball, each frame from 8 angles, in the classic picture format.
// The sketch reads it into PSRAM at the start and binds it for the script:
//   wad[]                         its bytes
//   wadLoaded                     1 when it was read
//   wadSprite("HUSK", f, a)       where picture HUSK<f><a> starts in wad[]
//   wadFind("TITLEPIC")           where any entry starts, by its name
// Without the file the game still runs, with its own small monsters. To put
// it on the partition, see the README's "Ironforge" (the image holds the
// GameLauncher's games too).
//
// Needs the modified LovyanGFX Bus_RGB driver (double frame buffer with
// show(), _frame_buffer, pixel_size, offset). Guition4848S040.h is the panel
// configuration.
//
// Board: ESP32S3 Dev Module, PSRAM "OPI PSRAM", Flash Size 16MB, Partition
// Scheme "16M Flash (3MB APP/9.9MB FATFS)".
#include <FS.h>
#include <FFat.h>
#include "Guition4848S040.h"
#include "script_executable.h"
#include "binding.h"

LGFX lcd;
uint16_t *frame_buffer;
static uint32_t _sizex = 480;
static uint32_t _sizey = 480;

// once a second: the frames shown, and the time the script takes for one
// (its steps and its drawing, between two show16()), so the frame rate it
// would reach without waiting for the panel
static uint32_t fpsFrames = 0;
static int64_t fpsStart = 0;
static int64_t scriptTime = 0;   // microseconds spent in the script this second
static int64_t leftShow = 0;     // when the last show16() returned

void show16() {
  int64_t in = esp_timer_get_time();
  if (leftShow != 0)
    scriptTime += in - leftShow;
  lcd._bus_instance.show();
  frame_buffer = (uint16_t *)lcd._bus_instance._frame_buffer;
  lcd._panel_instance.resetWriteBuffer();
  fpsFrames++;
  int64_t now = esp_timer_get_time();
  if (now - fpsStart >= 1000000) {
    float perFrame = (float)scriptTime / fpsFrames;
    printf("shown %.1f fps, script %.2f ms a frame (%.0f fps without waiting for the panel)\n",
           fpsFrames * 1000000.0f / (now - fpsStart), perFrame / 1000.0f, 1000000.0f / perFrame);
    fpsFrames = 0;
    scriptTime = 0;
    fpsStart = now;
  }
  leftShow = esp_timer_get_time();
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

// --- the buttons, where the script draws them (240x240) ---
// bits of touchHeld: 0 turn left, 1 turn right, 2 forward, 3 back, 4 FIRE, 5 USE,
// 6 GUN (the next gun)
#define PAD_TOP 176
#define CROSS_X 52
#define CROSS_Y 208

static volatile uint32_t touchHeld = 0;

static float scriptSin(float a) {
  return sinf(a);
}

static float scriptHypot(float x, float y) {
  return hypotf(x, y);
}

static float scriptAtan2(float y, float x) {
  return atan2f(y, x);
}

// --- the WAD: read from the FFat partition into PSRAM ---
static uint8_t *wadData = NULL;
static int wadLoaded = 0;

static void loadWad(const char *path) {
  if (!FFat.begin(false)) {
    printf("FFat does not mount: no %s, the game uses its own monsters\n", path);
    return;
  }
  File f = FFat.open(path, FILE_READ);
  if (!f) {
    printf("no %s on the FFat partition: the game uses its own monsters\n", path);
    return;
  }
  size_t size = f.size();
  wadData = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
  if (wadData && f.read(wadData, size) == size && memcmp(wadData + 1, "WAD", 3) == 0) {
    wadLoaded = 1;
    printf("%s: %u bytes\n", path, (unsigned)size);
  } else {
    printf("%s does not read\n", path);
    free(wadData);
    wadData = NULL;
  }
  f.close();
}

// where the entry of that name starts in the WAD, -1 when it is not there
static int wadFind(char *name) {
  if (!wadData)
    return -1;
  int32_t n, dir;
  memcpy(&n, wadData + 4, 4);
  memcpy(&dir, wadData + 8, 4);
  for (int i = 0; i < n; i++) {
    const uint8_t *e = wadData + dir + 16 * i;
    if (strncmp((const char *)e + 8, name, 8) == 0) {
      int32_t pos;
      memcpy(&pos, e, 4);
      return pos;
    }
  }
  return -1;
}

// the picture named prefix + frame letter + angle digit (HUSKA1: frame A,
// angle 1)
static int wadSprite(char *prefix, int frame, int angle) {
  char name[9];
  snprintf(name, sizeof(name), "%s%c%d", prefix, 'A' + frame, angle);
  return wadFind(name);
}

static uint32_t buttonAt(int x, int y) {
  if (y < PAD_TOP)
    return 0;
  if (x >= 164)
    return 1u << 4;
  if (x >= 104)
    return y < 207 ? 1u << 5 : 1u << 6;
  // the cross: the side of its middle the finger is on; between two arms,
  // both (forward and turning)
  int dx = x - CROSS_X, dy = y - CROSS_Y;
  if (abs(dx) < 5 && abs(dy) < 5)
    return 0;
  uint32_t b = 0;
  if (abs(dx) * 5 > abs(dy) * 2)
    b |= dx < 0 ? 1u << 0 : 1u << 1;
  if (abs(dy) * 5 > abs(dx) * 2)
    b |= dy < 0 ? 1u << 2 : 1u << 3;
  return b;
}

static void touchTask(void *) {
  while (true) {
    lgfx::touch_point_t tp[3];
    int n = lcd.getTouchRaw(tp, 3);
    uint32_t held = 0;
    for (int k = 0; k < n; k++)
      held |= buttonAt(tp[k].x / 2, tp[k].y / 2);
    touchHeld = held;
    vTaskDelay(pdMS_TO_TICKS(16));
  }
}

char ironforge[] = R"EOF(
// IRONFORGE -- a first-person shooter with the technology of the mid-90s:
// a grid of cells, each with its own floor and ceiling height, stairs,
// ledges, pits, the sky, textured floors and ceilings, a light level per
// cell, doors that rise into the ceiling, a key, an exit switch. Original
// level, monsters and art; the palette, the wall textures' makers, the
// drone, the pickups and the face come from Vault13H.

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

uint8_t font8[320] = {126, 199, 207, 219, 243, 227, 126, 0, 24, 56, 120, 24, 24, 24, 126, 0, 126, 195, 6, 28, 112, 192, 255, 0, 126, 195, 3, 62, 3, 195, 126, 0, 14, 30, 54, 102, 255, 6, 6, 0, 255, 192, 254, 3, 3, 195, 126, 0, 62, 96, 192, 254, 195, 195, 126, 0, 255, 195, 6, 12, 24, 24, 24, 0, 126, 195, 195, 126, 195, 195, 126, 0, 126, 195, 195, 127, 3, 6, 124, 0, 60, 102, 195, 195, 255, 195, 195, 0, 254, 195, 195, 254, 195, 195, 254, 0, 126, 195, 192, 192, 192, 195, 126, 0, 252, 198, 195, 195, 195, 198, 252, 0, 255, 192, 192, 252, 192, 192, 255, 0, 255, 192, 192, 252, 192, 192, 192, 0, 126, 195, 192, 207, 195, 195, 126, 0, 195, 195, 195, 255, 195, 195, 195, 0, 126, 24, 24, 24, 24, 24, 126, 0, 63, 6, 6, 6, 198, 198, 124, 0, 198, 204, 216, 240, 216, 204, 198, 0, 192, 192, 192, 192, 192, 192, 255, 0, 195, 231, 255, 219, 195, 195, 195, 0, 195, 227, 243, 219, 207, 199, 195, 0, 126, 195, 195, 195, 195, 195, 126, 0, 254, 195, 195, 254, 192, 192, 192, 0, 126, 195, 195, 195, 203, 198, 123, 0, 254, 195, 195, 254, 204, 198, 195, 0, 126, 195, 192, 126, 3, 195, 126, 0, 255, 24, 24, 24, 24, 24, 24, 0, 195, 195, 195, 195, 195, 195, 126, 0, 195, 195, 195, 195, 102, 60, 24, 0, 195, 195, 195, 219, 255, 231, 195, 0, 195, 102, 60, 24, 60, 102, 195, 0, 195, 195, 102, 60, 24, 24, 24, 0, 255, 6, 12, 24, 48, 96, 255, 0, 0, 0, 0, 0, 0, 192, 192, 0, 0, 192, 192, 0, 0, 192, 192, 0, 3, 6, 12, 24, 48, 96, 192, 0, 0, 24, 24, 126, 24, 24, 0, 0};

// ------------------------------------------------------------ textures

uint8_t tex[57344];       // 14 textures, 64 x 64 VGA colours (see makeTextures)
uint8_t spr[14336];       // 14 sprites 32 x 32, 0 = transparent (see makeSprites)
uint32_t seed;
int ceilIdx;
int word[4] = {14, 33, 18, 29};   // EXIT, in font8's glyphs
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
   for (int i = 0; i < 14336; i++)
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

// ------------------------------------------------------------ the engine
//
// The world is a grid of cells, each with its own floor and ceiling height
// (or the sky), floor and ceiling textures and light. Each view column casts
// one ray through the grid, from the eye outwards. In each cell it crosses,
// the ray draws that cell's floor and ceiling between where it entered and
// where it leaves; at the next cell, the step up of a higher floor and the
// step down of a lower ceiling; at a solid cell, the wall, and the column is
// done. What is drawn closes the column from the bottom (floors, steps) and
// from the top (ceilings): top and bot are the rows still open. They are
// kept at each cell boundary, so that the sprites further on show only
// through what is still open there.

#define VW 240
#define VH 144
#define HZ 72
#define STRIDE 480
#define K 181.8
#define MAPW 24
#define MAPH 20
#define SEGS 12
#define MAXE 40
#define SHOTS 8
#define STEP_MS 33
#define DT 0.033
#define SKY 9.0
#define WALLH 2.5
// the WAD's pictures: 64 pixels a cell, as the old sprites were drawn
#define PPU 64.0
// the buttons, a bit each in touchHeld
#define B_LEFT 0
#define B_RIGHT 1
#define B_UP 2
#define B_DOWN 3
#define B_FIRE 4
#define B_USE 5
#define B_WPN 6

// the kinds of cell: # M T B X . : h 1 2 3 4 n o p r q D E K l
#define KINDS 21
#define LEVELS 3
uint8_t levelCells[1440] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 5, 5, 5, 0, 13, 13, 13, 14, 14, 14, 13, 13, 13, 13, 13, 13, 13, 0, 0, 0, 0, 0, 0, 5, 5, 5, 5, 17, 13, 13, 13, 14, 14, 13, 15, 13, 13, 13, 14, 13, 13, 0, 0, 0, 0, 0, 0, 5, 5, 5, 5, 0, 13, 13, 13, 13, 13, 13, 14, 13, 13, 13, 14, 13, 13, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 13, 13, 13, 13, 13, 13, 16, 13, 13, 13, 13, 13, 13, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3, 3, 18, 3, 3, 3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 11, 11, 11, 11, 11, 11, 11, 11, 11, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 7, 7, 10, 10, 10, 10, 10, 7, 7, 3, 0, 0, 0, 5, 5, 5, 5, 5, 0, 0, 0, 0, 0, 3, 7, 7, 9, 9, 9, 9, 9, 7, 7, 3, 0, 0, 0, 5, 5, 5, 5, 5, 0, 0, 0, 0, 0, 3, 7, 7, 8, 8, 8, 8, 8, 7, 7, 3, 0, 0, 0, 5, 5, 5, 5, 5, 17, 6, 6, 6, 6, 6, 7, 7, 7, 12, 12, 12, 7, 7, 7, 3, 0, 0, 0, 5, 5, 5, 5, 5, 0, 0, 0, 0, 0, 3, 7, 7, 7, 12, 12, 12, 7, 7, 7, 3, 0, 0, 0, 5, 5, 5, 5, 5, 0, 0, 0, 0, 0, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3, 3, 3, 19, 3, 3, 3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 5, 5, 5, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 5, 5, 5, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 5, 5, 5, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 5, 5, 5, 0, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 0, 0, 0, 0, 0, 5, 5, 5, 5, 17, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 0, 0, 0, 0, 0, 5, 5, 5, 5, 0, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 0, 0, 0, 0, 0, 5, 5, 5, 5, 0, 20, 20, 20, 7, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20, 20, 20, 7, 20, 20, 20, 20, 20, 20, 8, 9, 9, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20, 20, 20, 7, 20, 20, 20, 20, 20, 20, 8, 9, 9, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20, 20, 20, 7, 7, 7, 7, 7, 7, 7, 7, 20, 20, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20, 20, 20, 7, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20, 20, 20, 7, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 0, 5, 5, 0, 0, 0, 0, 0, 0, 0, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 19, 5, 5, 4, 0, 0, 0, 0, 0, 0, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 0, 5, 5, 0, 0, 0, 0, 0, 0, 0, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 0, 5, 5, 5, 5, 5, 0, 0, 0, 0, 0, 0, 13, 13, 15, 14, 13, 13, 0, 13, 13, 13, 13, 0, 5, 5, 5, 5, 5, 0, 0, 0, 0, 0, 0, 0, 13, 13, 13, 13, 13, 0, 0, 13, 13, 13, 13, 17, 5, 5, 5, 5, 5, 0, 0, 0, 0, 0, 0, 0, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 0, 5, 5, 5, 5, 5, 0, 0, 0, 0, 0, 0, 0, 13, 13, 13, 14, 14, 13, 13, 13, 13, 13, 13, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 13, 13, 16, 14, 14, 16, 13, 13, 13, 13, 13, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 7, 7, 7, 7, 7, 7, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 7, 7, 7, 7, 7, 7, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 7, 8, 9, 10, 11, 7, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 7, 7, 7, 7, 7, 7, 7, 19, 5, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 7, 7, 7, 7, 7, 7, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
uint8_t levelThings[1440] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 10, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
uint8_t kKind[21] = {1, 1, 1, 1, 4, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 2, 2, 3, 5};
uint8_t kSolid[21] = {1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
uint8_t kWall[21] = {0, 1, 2, 3, 5, 2, 1, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 4, 4, 6, 3};
float kFloor[21] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.25, 0.5, 0.75, 1.0, -0.25, 0.0, 0.5, 0.25, 0.75, 0.0, 1.0, 0.0, -0.25};
float kCeil[21] = {0.0, 0.0, 0.0, 0.0, 0.0, 1.25, 1.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 9.0, 9.0, 9.0, 9.0, 1.0, 2.0, 1.0, 2.0};
uint8_t kFloorTex[21] = {0, 0, 0, 0, 0, 7, 8, 7, 8, 8, 8, 8, 9, 10, 10, 10, 10, 8, 8, 8, 13};
uint8_t kCeilTex[21] = {0, 0, 0, 0, 0, 11, 12, 12, 12, 12, 12, 12, 12, 0, 0, 0, 0, 12, 12, 12, 12};
uint8_t kLight[21] = {0, 0, 0, 0, 0, 13, 5, 11, 11, 12, 12, 13, 14, 15, 15, 15, 15, 9, 9, 9, 15};

// the level being played: its cells and things, copied from levelCells[]
// and levelThings[] when it starts
uint8_t cellKind[480];
uint8_t thingMap[480];
int level;

uint16_t lit[4096];       // 16 light levels x 256 colours
float rowK[VH];           // K / the rows from the horizon
float cellCeil[480];      // the ceilings now (doors move)
int doorCell[8];
int doorState[8];         // 0 shut, 1 opening, 2 open, 3 closing
float doorTime[8];
int doors;
uint8_t skyH[1024];       // the mountains on the sky, all around
uint16_t skyRow[VH];

float segD[2880];         // per column, at each cell boundary: the distance
int segT[2880];           // and the rows still open
int segB[2880];
int segN[VW];

// the player
float px;
float py;
float pz;
float vz;
float pa;
float dirX;
float dirY;
float planeX;
float planeY;
float eyeZ;
float bob;
int health;
int bullets;
int shells;
int weapon;               // 0 pistol, 1 shotgun, 2 chaingun
int have[3];
float gunT;               // time since the last shot
int hasKey;
int kills;
int foes;

// the things: 0 husk, 1 drone, 2 wisp, 3 health, 4 bullets, 5 key,
// 6 shells, 7 the shotgun, 8 the chaingun
float ex[40];
float ey[40];
float ez[40];
int et[40];
int es[40];               // 0 waiting, 1 after the player, 2 dead or taken
int ehp[40];
float ecool[40];
float eanim[40];
float eflash[40];
int ne;
float efacing[40];        // the way each monster looks (radians)
int emoving[40];
float edie[40];           // time since it fell
int order[40];
float odist[40];

// the wisps' fireballs
float fx[8];
float fy[8];
float fz[8];
float fdx[8];
float fdy[8];
int fon[8];

int state;                // 0 title, 1 playing, 2 out, 3 dead
float stateT;
float cool;
float flash;
float hurt;
float glow;
float faceT;
int faceLook;
int message;
float messageT;
uint32_t held;
uint32_t before;
int frame;
float playTime;

float absf(float v)
{
   if (v < 0)
      return 0 - v;
   return v;
}

// the square root, through the bound hypot()
float sqrtf2(float v)
{
   return hypot(v, 0.0);
}

int floori(float v)
{
   int i = (int)(v);
   if (v < 0 && i != v)
      i = i - 1;
   return i;
}

int btn(int b)
{
   return (held >> b) & 1;
}

int btnp(int b)
{
   if (((held >> b) & 1) == 1 && ((before >> b) & 1) == 0)
      return 1;
   return 0;
}

// A cast is always put in a variable before an operator is applied to it:
// the compiler drops the operator in `((int)(float expression)) & 63`, as
// the left operand of & + | << >> or % (being looked into).

// ---- the face in the status bar: a vault raider in a steel helmet, 24 x 30,
// worse as health drops: 0 fine (over 80), 1 tired, 2 bruised, 3 cut and
// grimacing, 4 bloody (20 or less), 5 dead. It glances left and right, and
// winces (eyes shut, mouth open) for a moment when hit.

int faceLevel()
{
   if (state == 3 || health <= 0)
      return 5;
   if (health > 80)
      return 0;
   if (health > 60)
      return 1;
   if (health > 40)
      return 2;
   if (health > 20)
      return 3;
   return 4;
}

// the colour of face pixel (x, y), 0 outside the head
int facePixel(int x, int y, int lv, int look, int ouch)
{
   int dx = x - 12;
   int dy = y - 16;
   int e = dx * dx * 144 + dy * dy * 81;     // the head: an ellipse 9 x 12
   if (e > 11664)
      return 0;
   int c = 66;                               // skin, lit from the left
   if (x < 9 && y > 9 && y < 21 && e < 6000)
      c = 90;
   if (dx > 5 || e > 9500)
      c = 138;
   if (lv == 5)
   {
      c = 25;                                // dead: grey
      if (dx > 5 || e > 9500)
         c = 22;
   }
   // the helmet and its rim
   if (y <= 9)
   {
      c = 24;
      if (x < 10 && y > 2 && y < 7)
         c = 27;
      if (dx > 5)
         c = 21;
      if (y == 9)
         c = 19;
      return c;
   }
   // bruise on the cheek, cut and blood on the forehead, blood from the nose
   if (lv >= 2 && lv != 5 && x >= 16 && x <= 17 && y >= 18 && y <= 19)
      c = 113;
   if (lv >= 3 && x == 6 && y >= 10 && y <= 13)
      c = 40;
   if (lv >= 4 && x == 12 && y >= 20 && y <= 21)
      c = 40;
   if (lv == 5 && (x == 15 || x == 16) && y == 10)
      c = 40;
   // eyebrows: straight when fine, drawn up in the middle when it hurts
   int lb = 0;
   if (y == 12 && ((x >= 6 && x <= 9) || (x >= 14 && x <= 17)))
      lb = 1;
   if (lv >= 2 && y == 12 && (x == 8 || x == 9 || x == 14 || x == 15))
      lb = 0;
   if (lv >= 2 && y == 11 && (x == 8 || x == 9 || x == 14 || x == 15))
      lb = 1;
   if (lb)
      c = 186;
   // the eyes
   int ex = -1;
   if (x >= 7 && x <= 9)
      ex = x - 7;
   if (x >= 14 && x <= 16)
      ex = x - 14;
   if (ex >= 0 && y >= 13 && y <= 15)
   {
      if (lv == 5)
      {
         // X X
         int xd = ex - (y - 13);
         if (xd == 0 || ex + (y - 13) == 2)
            return 16;
         return c;
      }
      if (y == 13)
         return c;
      if (ouch)
      {
         if (y == 15)
            return 186;                      // shut tight
         return c;
      }
      if (lv >= 4 && x <= 9 && y == 14)
         return 113;                         // the left eye swollen
      if (ex == 1 + look)
         return 16;                          // the pupil
      return 31;
   }
   // the nose
   if (x == 13 && y >= 17 && y <= 19 && c != 40)
      c = 138;
   // the mouth
   if (ouch && lv != 5 && x >= 11 && x <= 13 && y >= 22 && y <= 24)
   {
      if (x == 12 && y == 23)
         return 40;
      return 16;
   }
   if (lv == 0)
   {
      if ((y == 22 && (x == 9 || x == 15)) || (y == 23 && x >= 10 && x <= 14))
         c = 186;
   }
   else if (lv == 1 || lv == 5)
   {
      if (y == 23 && x >= 10 && x <= 14)
         c = 186;
   }
   else if (lv == 2)
   {
      if ((y == 23 && x >= 10 && x <= 14) || (y == 24 && (x == 9 || x == 15)))
         c = 186;
   }
   else if (lv == 3)
   {
      if ((y == 22 || y == 24) && x >= 10 && x <= 14)
         c = 186;
      if (y == 23 && x >= 10 && x <= 14)
         c = 31;                             // clenched teeth
   }
   else
   {
      if (y == 22 && x >= 11 && x <= 13)
         c = 186;
      if (y == 23 && x >= 10 && x <= 14)
         c = 16;
      if (y == 24 && (x == 9 || x == 15))
         c = 186;
   }
   return c;
}

// ---- making the colours, the textures, the sprites, the sky

void makeLight()
{
   for (int l = 0; l < 16; l++)
      for (int i = 0; i < 256; i++)
      {
         int c = pal[i];
         int v = ((c & 255) << 8) | (c >> 8);
         int r = (v >> 11) & 31;
         int g = (v >> 5) & 63;
         int b = v & 31;
         int f = 3 + l;
         r = (r * f) / 18;
         g = (g * f) / 18;
         b = (b * f) / 18;
         int w = (r << 11) | (g << 5) | b;
         lit[l * 256 + i] = ((w & 255) << 8) | (w >> 8);
      }
   for (int y = 0; y < VH; y++)
   {
      int d = y - HZ;
      if (d == 0)
         d = 1;
      if (d < 0)
         d = 0 - d;
      rowK[y] = K / d;
   }
}

void floorTiles(int t)
{
   int base = t * 4096;
   for (int y = 0; y < 64; y++)
      for (int x = 0; x < 64; x++)
      {
         int c = 24;
         if (((x / 32) + (y / 32)) % 2 == 1)
            c = 22;
         if (x % 32 == 0 || y % 32 == 0)
            c = 19;
         if (x % 32 == 1 || y % 32 == 1)
            c = 26;
         tex[base + y * 64 + x] = c;
      }
}

void grateTex(int t)
{
   int base = t * 4096;
   for (int y = 0; y < 64; y++)
      for (int x = 0; x < 64; x++)
      {
         int c = 17;
         if (x % 8 < 2 || y % 8 < 2)
            c = 23;
         if (x % 8 == 0 || y % 8 == 0)
            c = 26;
         if (x % 32 < 2 || y % 32 < 2)
            c = 21;
         tex[base + y * 64 + x] = c;
      }
}

void slimeTex(int t)
{
   int base = t * 4096;
   for (int y = 0; y < 64; y++)
      for (int x = 0; x < 64; x++)
      {
         int w = (int)(sin(x * 0.2) * 4.0 + sin(y * 0.29 + x * 0.1) * 4.0);
         int c = 120;
         if (w > 3)
            c = 48;
         if (w < -3)
            c = 192;
         if (rnd(17) == 0)
            c = 44;
         tex[base + y * 64 + x] = c;
      }
}

void dirtTex(int t)
{
   int base = t * 4096;
   for (int y = 0; y < 64; y++)
      for (int x = 0; x < 64; x++)
      {
         // blocks of 4x4 texels, not single ones
         int r = ((x / 4) * 7 + (y / 4) * 13 + (x / 4) * (y / 4)) % 10;
         int c = 186;
         if (r < 3)
            c = 114;
         if (r == 9)
            c = 138;
         tex[base + y * 64 + x] = c;
      }
   for (int i = 0; i < 18; i++)
   {
      int cx = rnd(64);
      int cy = rnd(64);
      for (int dy = -1; dy <= 1; dy++)
         for (int dx = -2; dx <= 1; dx++)
            put(t, cx + dx, cy + dy, 21);
      put(t, cx - 1, cy - 1, 26);
   }
}

void lampTex(int t)
{
   int base = t * 4096;
   for (int y = 0; y < 64; y++)
      for (int x = 0; x < 64; x++)
      {
         int c = 21;
         if (x % 32 == 0 || y % 32 == 0)
            c = 18;
         int lx = x % 32;
         int ly = y % 32;
         if (lx >= 10 && lx < 22 && ly >= 10 && ly < 22)
         {
            c = 31;
            if (lx == 10 || lx == 21 || ly == 10 || ly == 21)
               c = 28;
         }
         tex[base + y * 64 + x] = c;
      }
}

void blueDoorTex(int t)
{
   setRamp(21, 24, 27, 30);
   metalTex(t, 32, 0);
   for (int y = 24; y < 40; y++)
      for (int x = 0; x < 64; x++)
      {
         int c = 32;
         if (y == 24 || y == 39)
            c = 9;
         if ((x + y) % 16 == 0)
            c = 104;
         put(t, x, y, c);
      }
}

void lavaTex(int t)
{
   int base = t * 4096;
   for (int y = 0; y < 64; y++)
      for (int x = 0; x < 64; x++)
      {
         int w = (int)(sin(x * 0.17 + y * 0.05) * 5.0 + sin(y * 0.23 - x * 0.11) * 5.0);
         int c = 42;
         if (w > 2)
            c = 44;
         if (w > 6)
            c = 92;
         if (w < -4)
            c = 112;
         if (w < -7)
            c = 184;
         tex[base + y * 64 + x] = c;
      }
}

void makeTextures()
{
   seed = 4242;
   setRamp(20, 22, 24, 27);
   stoneTex(0, 18, 17, 16, 12, 30, 0);
   grimeDeco(0, 30);
   setRamp(21, 24, 27, 30);
   metalTex(1, 32, 1);
   setRamp(196, 148, 172, 27);
   metalTex(2, 32, 0);
   lightsDeco(2);
   setRamp(184, 112, 113, 137);
   stoneTex(3, 19, 17, 8, 16, 16, 1);
   cracksDeco(3, 6, 17);
   setRamp(186, 114, 115, 139);
   woodTex(4);
   bandsDeco(4);
   exitTex(5);
   blueDoorTex(6);
   floorTiles(7);
   grateTex(8);
   slimeTex(9);
   dirtTex(10);
   lampTex(11);
   setRamp(17, 19, 21, 23);
   stoneTex(12, 16, 16, 16, 20, 40, 0);
   lavaTex(13);
}

// the husk: a hunched brute; pose 0 and 1 walking, 2 striking
void huskSprite(int s, int pose)
{
   for (int y = 0; y < 32; y++)
      for (int x = 0; x < 32; x++)
      {
         int c = 0;
         int dx = x - 16;
         // the legs
         int lx = 12;
         int rx = 19;
         if (pose == 1)
         {
            lx = 11;
            rx = 20;
         }
         if (y >= 22 && ((x >= lx && x < lx + 3) || (x >= rx && x < rx + 3)))
            c = 186;
         // the body
         int by = y - 16;
         if (dx * dx * 4 + by * by * 3 < 300)
         {
            c = 138;
            if (dx > 3)
               c = 186;
            if (dx < -3 && by < 0)
               c = 90;
         }
         // the arms
         if (pose < 2 && y >= 11 && y < 25 && (x == 6 || x == 7 || x == 25 || x == 26))
            c = 138;
         if (pose == 2 && y >= 1 && y < 14 && (x == 7 || x == 8 || x == 24 || x == 25))
            c = 138;
         if (pose == 2 && y < 3 && (x == 6 || x == 9 || x == 23 || x == 26))
            c = 31;
         // the head, low between the shoulders
         int hx = x - 16;
         int hy = y - 7;
         if (hx * hx + hy * hy <= 16)
            c = 138;
         if (y == 7 && (x == 14 || x == 18))
            c = 44;
         if (y == 9 && x >= 15 && x <= 17)
            c = 40;
         if (c != 0)
            sput(s, x, y, c);
      }
}

void wispSprite(int s, int k)
{
   for (int y = 0; y < 32; y++)
      for (int x = 0; x < 32; x++)
      {
         float dx = x - 16 + sin(y * 0.4 + k * 2.0) * 1.5;
         float dy = y - 20;
         float w = 10.0;
         if (dy < 0)
            w = 10.0 + dy * 0.5;
         float r = dx * dx + dy * dy * 0.6;
         int c = 0;
         if (dy > -16 && absf(dx) < w && r < 110)
         {
            c = 40;
            if (r < 60)
               c = 92;
            if (r < 25)
               c = 44;
            if (r < 8)
               c = 31;
         }
         if (y == 19 && (x == 13 || x == 19))
            c = 16;
         if (c != 0)
            sput(s, x, y, c);
      }
}

void moreSprites()
{
   huskSprite(6, 0);
   huskSprite(7, 1);
   huskSprite(8, 2);
   wispSprite(9, 0);
   wispSprite(10, 1);
   // 11 a fireball
   for (int y = 8; y < 24; y++)
      for (int x = 8; x < 24; x++)
      {
         int r = (x - 16) * (x - 16) + (y - 16) * (y - 16);
         int c = 0;
         if (r < 50)
            c = 40;
         if (r < 26)
            c = 92;
         if (r < 9)
            c = 44;
         if (c != 0)
            sput(11, x, y, c);
      }
   // 12 the blue key
   for (int y = 18; y < 32; y++)
      for (int x = 11; x < 21; x++)
      {
         int c = 32;
         if (x == 11 || x == 20 || y == 18 || y == 31)
            c = 9;
         if (y >= 21 && y < 24 && x > 12 && x < 19)
            c = 31;
         sput(12, x, y, c);
      }
   // 13 a fallen husk
   for (int i = 0; i < 180; i++)
   {
      int x = 3 + rnd(26);
      int y = 25 + rnd(7);
      int c = 138;
      if (rnd(3) == 0)
         c = 186;
      if (rnd(9) == 0)
         c = 40;
      sput(13, x, y, c);
   }
}

void makeSky()
{
   for (int i = 0; i < 1024; i++)
   {
      float a = i * 0.006136;
      int h = (int)(14.0 + sin(a * 3.0) * 6.0 + sin(a * 7.0 + 1.0) * 4.0 + sin(a * 17.0) * 2.0);
      skyH[i] = h;
   }
   // ash red at the top, a glow at the horizon
   for (int y = 0; y < VH; y++)
   {
      int t = y * 63 / HZ;
      if (t > 63)
         t = 63;
      skyRow[y] = vga(16 + t * 3 / 4, 4 + t / 3, 8 + t / 8);
   }
}

// ---- the level

int cellAt(float x, float y)
{
   int cx = floori(x);
   int cy = floori(y);
   if (cx < 0 || cy < 0 || cx >= MAPW || cy >= MAPH)
      return -1;
   return cy * MAPW + cx;
}

float floorOf(int c)
{
   return kFloor[cellKind[c]];
}

// can something standing at height z, as tall as tall, go into cell c?
int canEnter(int c, float z, float tall)
{
   if (c < 0)
      return 0;
   int k = cellKind[c];
   if (kSolid[k] == 1)
      return 0;
   float f = kFloor[k];
   if (f > z + 0.3)
      return 0;
   float top = f;
   if (z > top)
      top = z;
   if (cellCeil[c] - top < tall)
      return 0;
   return 1;
}

void addThing(int t, int c)
{
   if (ne >= MAXE)
      return;
   ex[ne] = (c % MAPW) + 0.5;
   ey[ne] = (c / MAPW) + 0.5;
   ez[ne] = floorOf(c);
   et[ne] = t;
   es[ne] = 0;
   ehp[ne] = 4;
   if (t == 1 || t == 2)
      ehp[ne] = 2;
   ecool[ne] = 1.0 + rnd(20) * 0.1;
   eanim[ne] = 0;
   eflash[ne] = 0;
   efacing[ne] = rnd(8) * 0.7854;
   emoving[ne] = 0;
   edie[ne] = 0;
   ne++;
}

void setAngle(float a)
{
   pa = a;
   dirX = sin(a + 1.5708);
   dirY = sin(a);
   planeX = 0 - dirY * 0.66;
   planeY = dirX * 0.66;
}

// level n: its cells and things, its doors, the player at its start
void newLevel(int n)
{
   level = n;
   ne = 0;
   doors = 0;
   foes = 0;
   kills = 0;
   hasKey = 0;
   for (int c = 0; c < 480; c++)
   {
      cellKind[c] = levelCells[n * 480 + c];
      thingMap[c] = levelThings[n * 480 + c];
      int k = cellKind[c];
      cellCeil[c] = kCeil[k];
      if ((kKind[k] == 2 || kKind[k] == 3) && doors < 8)
      {
         cellCeil[c] = kFloor[k];
         doorCell[doors] = c;
         doorState[doors] = 0;
         doorTime[doors] = 0;
         doors++;
      }
      int t = thingMap[c];
      if (t == 7)
      {
         px = (c % MAPW) + 0.5;
         py = (c / MAPW) + 0.5;
         pz = floorOf(c);
      }
      if (t >= 1 && t <= 3)
      {
         addThing(t - 1, c);
         foes++;
      }
      if (t >= 4 && t <= 6)
         addThing(t - 1, c);
      if (t >= 8 && t <= 10)
         addThing(t - 2, c);
   }
   for (int k = 0; k < SHOTS; k++)
      fon[k] = 0;
   setAngle(0.0);
   vz = 0;
   playTime = 0;
   message = 10 + n;
   messageT = 2.5;
   state = 1;
   stateT = 0;
}

void newGame()
{
   health = 100;
   bullets = 50;
   shells = 0;
   weapon = 0;
   have[0] = 1;
   have[1] = 0;
   have[2] = 0;
   gunT = 10.0;
   newLevel(0);
}

// ---- drawing the view

int light(int c, float d)
{
   int dl = (int)(d * 1.3);
   int l = kLight[cellKind[c]] - dl;
   if (l < 0)
      l = 0;
   if (l > 15)
      l = 15;
   return l;
}

void castView()
{
   int skyOff = (int)(pa * 163.0);
   for (int x = 0; x < VW; x++)
   {
      float cam = 2.0 * x / VW - 1.0;
      float rx = dirX + planeX * cam;
      float ry = dirY + planeY * cam;
      int mx = floori(px);
      int my = floori(py);
      float ddx = 1000000.0;
      float ddy = 1000000.0;
      if (rx != 0)
         ddx = absf(1.0 / rx);
      if (ry != 0)
         ddy = absf(1.0 / ry);
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
      int top = 0;
      int bot = VH - 1;
      float dPrev = 0.0;
      int c = my * MAPW + mx;
      int n = 0;
      int base = x * SEGS;
      int skyX = (skyOff + x * 188 / VW) & 1023;
      for (int steps = 0; steps < 48; steps++)
      {
         float d;
         int side = 0;
         if (sdx < sdy)
         {
            d = sdx;
            sdx = sdx + ddx;
            mx = mx + sx;
         }
         else
         {
            d = sdy;
            sdy = sdy + ddy;
            my = my + sy;
            side = 1;
         }
         if (d < 0.01)
            d = 0.01;
         int k = cellKind[c];
         float f = kFloor[k];
         float ce = cellCeil[c];
         int l = light(c, d);
         // this cell's floor, from where the ray left it to where it came in
         if (f < eyeZ)
         {
            int ff = (int)((eyeZ - f) * K / d);
            int yFar = HZ + ff;
            int y0 = yFar;
            if (y0 < top)
               y0 = top;
            int y1 = bot;
            if (dPrev > 0.0)
            {
               int fn = (int)((eyeZ - f) * K / dPrev);
               int yNear = HZ + fn;
               if (yNear < y1)
                  y1 = yNear;
            }
            if (y0 <= HZ)
               y0 = HZ + 1;
            if (y0 <= y1)
            {
               int ftb = kFloorTex[k] * 4096;
               int fshift = 0;
               if (kKind[k] == 5)
                  fshift = frame;
               float fh = eyeZ - f;
               int foff = y0 * STRIDE + x;
               for (int fy = y0; fy <= y1; fy++)
               {
                  float frd = fh * rowK[fy];
                  int fmask = 63;
                  if (frd > 2.5)
                     fmask = 62;
                  if (frd > 5.0)
                     fmask = 60;
                  int fu = (int)((px + rx * frd) * 64.0);
                  fu = (fu + fshift) & fmask;
                  int fv = (int)((py + ry * frd) * 64.0);
                  fv = fv & fmask;
                  int fd = (int)(frd * 1.3);
                  int fl = kLight[k] - fd;
                  if (fl < 0)
                     fl = 0;
                  led16[foff] = lit[fl * 256 + tex[ftb + fv * 64 + fu]];
                  foff = foff + STRIDE;
               }
            }
            if (yFar - 1 < bot)
               bot = yFar - 1;
         }
         // its ceiling, or the sky
         if (ce > eyeZ)
         {
            int cf = (int)((ce - eyeZ) * K / d);
            int yFar = HZ - cf;
            int y0 = top;
            int y1 = yFar;
            if (y1 > bot)
               y1 = bot;
            if (dPrev > 0.0)
            {
               int cn = (int)((ce - eyeZ) * K / dPrev);
               int yNear = HZ - cn;
               if (yNear > y0)
                  y0 = yNear;
            }
            if (y1 >= HZ)
               y1 = HZ - 1;
            if (y0 <= y1)
            {
               int off = y0 * STRIDE + x;
               if (ce >= SKY)
               {
                  int mount = HZ - skyH[skyX];
                  for (int y = y0; y <= y1; y++)
                  {
                     uint16_t col = skyRow[y];
                     if (y >= mount)
                        col = pal[17];
                     led16[off] = col;
                     off = off + STRIDE;
                  }
               }
               else
               {
                  int ctb = kCeilTex[k] * 4096;
                  float ch = ce - eyeZ;
                  for (int cy = y0; cy <= y1; cy++)
                  {
                     float crd = ch * rowK[cy];
                     int cmask = 63;
                     if (crd > 2.5)
                        cmask = 62;
                     if (crd > 5.0)
                        cmask = 60;
                     int cu = (int)((px + rx * crd) * 64.0);
                     cu = cu & cmask;
                     int cv = (int)((py + ry * crd) * 64.0);
                     cv = cv & cmask;
                     int cd = (int)(crd * 1.3);
                     int cl = kLight[k] - cd;
                     if (cl < 0)
                        cl = 0;
                     led16[off] = lit[cl * 256 + tex[ctb + cv * 64 + cu]];
                     off = off + STRIDE;
                  }
               }
            }
            if (yFar + 1 > top)
               top = yFar + 1;
         }
         if (top > bot)
            break;
         // the boundary to the next cell
         if (mx < 0 || my < 0 || mx >= MAPW || my >= MAPH)
            break;
         int nc = my * MAPW + mx;
         int nk = cellKind[nc];
         float wx;
         if (side == 0)
            wx = py + d * ry;
         else
            wx = px + d * rx;
         int u = (int)(wx * 64.0);
         u = u & 63;
         if ((side == 0 && rx < 0) || (side == 1 && ry > 0))
            u = 63 - u;
         float zr = d / K;
         if (kSolid[nk] == 1)
         {
            // a wall: everything still open; under the sky it is WALLH
            // high, the sky and the mountains above it
            int wy = top;
            if (ce >= SKY)
            {
               int wt = (int)((WALLH - eyeZ) * K / d);
               wy = HZ - wt;
               if (wy < top)
                  wy = top;
               if (wy > bot + 1)
                  wy = bot + 1;
               int mount = HZ - skyH[skyX];
               int soff = top * STRIDE + x;
               for (int y = top; y < wy; y++)
               {
                  uint16_t col = skyRow[y];
                  if (y >= mount)
                     col = pal[17];
                  led16[soff] = col;
                  soff = soff + STRIDE;
               }
            }
            int tb = kWall[nk] * 4096 + u;
            int off = wy * STRIDE + x;
            float z = eyeZ + (HZ - wy) * zr;
            for (int y = wy; y <= bot; y++)
            {
               int zi = (int)(z * 64.0);
               int v = 63 - (zi & 63);
               led16[off] = lit[l * 256 + tex[tb + v * 64]];
               off = off + STRIDE;
               z = z - zr;
            }
            top = VH;
            bot = -1;
         }
         else
         {
            float nf = kFloor[nk];
            float nce = cellCeil[nc];
            int tb = kWall[nk] * 4096 + u;
            // the step up to a higher floor
            if (nf > f)
            {
               int nn = (int)((eyeZ - nf) * K / d);
               int yN = HZ + nn;
               int y0 = yN;
               if (y0 < top)
                  y0 = top;
               int off = y0 * STRIDE + x;
               float z = eyeZ + (HZ - y0) * zr;
               for (int y = y0; y <= bot; y++)
               {
                  int zi = (int)(z * 64.0);
                  int v = 63 - (zi & 63);
                  led16[off] = lit[l * 256 + tex[tb + v * 64]];
                  off = off + STRIDE;
                  z = z - zr;
               }
               if (yN - 1 < bot)
                  bot = yN - 1;
            }
            // the step down of a lower ceiling (not between two skies)
            if (nce < ce && (nce < SKY || ce < SKY))
            {
               int nn = (int)((nce - eyeZ) * K / d);
               int yN = HZ - nn;
               int y1 = yN;
               if (y1 > bot)
                  y1 = bot;
               int off = top * STRIDE + x;
               float z = eyeZ + (HZ - top) * zr;
               for (int y = top; y <= y1; y++)
               {
                  int zi = (int)(z * 64.0);
                  int v = 63 - (zi & 63);
                  led16[off] = lit[l * 256 + tex[tb + v * 64]];
                  off = off + STRIDE;
                  z = z - zr;
               }
               if (yN + 1 > top)
                  top = yN + 1;
            }
         }
         if (n < SEGS)
         {
            segD[base + n] = d;
            segT[base + n] = top;
            segB[base + n] = bot;
            n++;
         }
         if (top > bot)
            break;
         c = nc;
         dPrev = d;
      }
      // rows nothing reached (the far end): black
      if (top <= bot)
      {
         int off = top * STRIDE + x;
         for (int y = top; y <= bot; y++)
         {
            led16[off] = pal[16];
            off = off + STRIDE;
         }
         if (n < SEGS)
         {
            segD[base + n] = 1000.0;
            segT[base + n] = VH;
            segB[base + n] = -1;
            n++;
         }
      }
      segN[x] = n;
   }
}

// ---- the monsters' sprites, from the WAD file on the FFat partition
// (tools/wad.py writes it): the sketch loads it and binds it as wad[], and
// wadSprite("HUSK", frame, angle) gives where a picture starts in it.
// Pictures are in the classic column format: width, height, left and top
// offsets (16 bits each), each column's offset (32 bits), then each column
// as posts (start row, length, a pad, the pixels, a pad), 255 at its end.
// Angle 1 is a monster seen from the front, 2..8 going round every 45
// degrees; angle 0 is the same from every side. The WAD on the board holds
// four monsters of Freedoom (tools/freedoom.py); tools/wad.py makes one of
// the same names with Ironforge's own monsters.

int huskWalk[32];         // frame A-D x angle 1-8
int huskHit[16];          // frame E-F x angle
int huskDie[4];           // G-J
int droneLook[16];        // A idle, B firing x angle
int droneDie[3];          // C-E
int wispFlame[4];
int fireBall[2];
int titlePic;              // TITLEPIC: the title screen, -1 without
int faceLook3[15];        // STFST<health level><look>
int faceOuch[5];
int faceDead;
int pistol[5];            // PISGA-E, and its flash
int pistolFlash;
int shotgun[4];           // SHTGA-D, flashes A B
int shotgunFlash[2];
int chaingun[2];          // CHGGA-B, flashes A B
int chaingunFlash[2];
int pickupPic[9];         // by thing kind (3..8)
int keyPic[2];
int useWad;

int rd16(int o)
{
   int v = wad[o] | (wad[o + 1] << 8);
   if (v > 32767)
      v = v - 65536;
   return v;
}

int rd32(int o)
{
   return wad[o] | (wad[o + 1] << 8) | (wad[o + 2] << 16) | (wad[o + 3] << 24);
}

int checkSprite(int o)
{
   if (o < 0)
      useWad = 0;
   return o;
}

void loadWadSprites()
{
   titlePic = -1;
   useWad = wadLoaded;
   if (useWad == 0)
      return;
   titlePic = wadFind("TITLEPIC");
   faceLook3[0] = wadFind("STFST00");
   faceLook3[1] = wadFind("STFST01");
   faceLook3[2] = wadFind("STFST02");
   faceLook3[3] = wadFind("STFST10");
   faceLook3[4] = wadFind("STFST11");
   faceLook3[5] = wadFind("STFST12");
   faceLook3[6] = wadFind("STFST20");
   faceLook3[7] = wadFind("STFST21");
   faceLook3[8] = wadFind("STFST22");
   faceLook3[9] = wadFind("STFST30");
   faceLook3[10] = wadFind("STFST31");
   faceLook3[11] = wadFind("STFST32");
   faceLook3[12] = wadFind("STFST40");
   faceLook3[13] = wadFind("STFST41");
   faceLook3[14] = wadFind("STFST42");
   faceOuch[0] = wadFind("STFOUCH0");
   faceOuch[1] = wadFind("STFOUCH1");
   faceOuch[2] = wadFind("STFOUCH2");
   faceOuch[3] = wadFind("STFOUCH3");
   faceOuch[4] = wadFind("STFOUCH4");
   faceDead = wadFind("STFDEAD0");
   pistol[0] = wadFind("PISGA0");
   pistol[1] = wadFind("PISGB0");
   pistol[2] = wadFind("PISGC0");
   pistol[3] = wadFind("PISGD0");
   pistol[4] = wadFind("PISGE0");
   pistolFlash = wadFind("PISFA0");
   shotgun[0] = wadFind("SHTGA0");
   shotgun[1] = wadFind("SHTGB0");
   shotgun[2] = wadFind("SHTGC0");
   shotgun[3] = wadFind("SHTGD0");
   shotgunFlash[0] = wadFind("SHTFA0");
   shotgunFlash[1] = wadFind("SHTFB0");
   chaingun[0] = wadFind("CHGGA0");
   chaingun[1] = wadFind("CHGGB0");
   chaingunFlash[0] = wadFind("CHGFA0");
   chaingunFlash[1] = wadFind("CHGFB0");
   pickupPic[3] = wadFind("MEDIA0");
   pickupPic[4] = wadFind("CLIPA0");
   pickupPic[6] = wadFind("SHELA0");
   pickupPic[7] = wadFind("SHOTA0");
   pickupPic[8] = wadFind("MGUNA0");
   keyPic[0] = wadFind("BKEYA0");
   keyPic[1] = wadFind("BKEYB0");
   // all of them, or the game's own drawings
   if (faceDead < 0 || pistolFlash < 0 || chaingunFlash[1] < 0 || keyPic[1] < 0 || pickupPic[8] < 0)
      useWad = 0;
   for (int a = 0; a < 8; a++)
   {
      for (int f = 0; f < 4; f++)
         huskWalk[f * 8 + a] = checkSprite(wadSprite("HUSK", f, a + 1));
      for (int f = 0; f < 2; f++)
      {
         huskHit[f * 8 + a] = checkSprite(wadSprite("HUSK", 4 + f, a + 1));
         droneLook[f * 8 + a] = checkSprite(wadSprite("DRON", f, a + 1));
      }
   }
   for (int f = 0; f < 4; f++)
   {
      huskDie[f] = checkSprite(wadSprite("HUSK", 6 + f, 0));
      wispFlame[f] = checkSprite(wadSprite("WISP", f, 0));
   }
   for (int f = 0; f < 3; f++)
      droneDie[f] = checkSprite(wadSprite("DRON", 2 + f, 0));
   for (int f = 0; f < 2; f++)
      fireBall[f] = checkSprite(wadSprite("FBAL", f, 0));
}

// which of the 8 views of monster i the player sees: 0 its front, then
// round as the old sprites number them (counter-clockwise with y up; the
// map's y goes down, hence facing less the player's side)
int viewOf(int i)
{
   float toPlayer = atan2(py - ey[i], px - ex[i]);
   float rel = efacing[i] - toPlayer;
   while (rel < 0)
      rel = rel + 6.28318;
   while (rel >= 6.28318)
      rel = rel - 6.28318;
   int a = (int)((rel + 0.3927) / 0.7854);
   a = a % 8;
   return a;
}

// a WAD picture standing at (wx, wy), its feet at height wz, ppu of its
// pixels a cell; clipped by what each column still had open at its distance
void drawPatch(int o, float wx, float wy, float wz, float ppu, int lightCell, int bright)
{
   float inv = 1.0 / (planeX * dirY - dirX * planeY);
   float sx = wx - px;
   float sy = wy - py;
   float tx = inv * (dirY * sx - dirX * sy);
   float ty = inv * (planeX * sy - planeY * sx);
   if (ty < 0.2)
      return;
   float s = K / ty / ppu;
   int pw = rd16(o);
   int lo = rd16(o + 4);
   int to = rd16(o + 6);
   float scr = (VW / 2) * (1.0 + tx / ty);
   float bottom = HZ + (eyeZ - wz) * K / ty;
   float left = scr - lo * s;
   float top = bottom - to * s;
   int x0 = (int)(left);
   if (x0 < 0)
      x0 = 0;
   int x1 = (int)(left + pw * s);
   if (x1 > VW - 1)
      x1 = VW - 1;
   int l = 15;
   if (bright == 0)
      l = light(lightCell, ty);
   int lb = l * 256;
   for (int x = x0; x <= x1; x++)
   {
      int t = 0;
      int b = VH - 1;
      int base = x * SEGS;
      int n = segN[x];
      for (int k = 0; k < n; k++)
         if (segD[base + k] < ty)
         {
            t = segT[base + k];
            b = segB[base + k];
         }
      if (t > b)
         continue;
      int tc = (int)((x - left) / s);
      if (tc < 0 || tc >= pw)
         continue;
      int col = o + rd32(o + 8 + tc * 4);
      while (wad[col] != 255)
      {
         int pt = wad[col];
         int pn = wad[col + 1];
         float y0f = top + pt * s;
         int ya = (int)(y0f);
         int yb = (int)(y0f + pn * s);
         if (ya < t)
            ya = t;
         if (yb > b)
            yb = b;
         int off = ya * STRIDE + x;
         for (int y = ya; y <= yb; y++)
         {
            int r = (int)((y - y0f) / s);
            if (r >= pn)
               r = pn - 1;
            if (r < 0)
               r = 0;
            led16[off] = lit[lb + wad[col + 3 + r]];
            off = off + STRIDE;
         }
         col = col + pn + 4;
      }
   }
}

// a WAD picture 1:1, its top left at (x0, y0), at full light, below row
// maxY: its columns are runs of the game's colours
void drawPic(int o, int x0, int y0, int maxY)
{
   int pw = rd16(o);
   for (int x = 0; x < pw; x++)
   {
      if (x0 + x < 0 || x0 + x >= VW)
         continue;
      int col = o + rd32(o + 8 + x * 4);
      while (wad[col] != 255)
      {
         int pt = wad[col];
         int pn = wad[col + 1];
         int off = (y0 + pt) * STRIDE + x0 + x;
         for (int r = 0; r < pn && y0 + pt + r < maxY; r++)
         {
            if (y0 + pt + r >= 0)
               led16[off] = lit[3840 + wad[col + 3 + r]];
            off = off + STRIDE;
         }
         col = col + pn + 4;
      }
   }
}

void drawTitlePic()
{
   drawPic(titlePic, 0, 0, VH);
}

// a gun as the old games held it: on a 320x200 screen with a 168-row view
// centred on row 84, its picture's left at 1 - its left offset, its top at
// 32 - 16 - its top offset (plus the bob); here that view is 240x144, 0.8
// the size
void drawGunPic(int o, int bx, int by)
{
   if (o < 0)
      return;
   int pw = rd16(o);
   int lo = rd16(o + 4);
   int to = rd16(o + 6);
   float left = (1 - lo + bx - 160) * 0.8 + 120.0;
   float top = (16 - to + by) * 0.8 + 9.6;
   int x0 = (int)(left);
   int x1 = (int)(left + pw * 0.8);
   if (x0 < 0)
      x0 = 0;
   if (x1 > VW - 1)
      x1 = VW - 1;
   for (int x = x0; x <= x1; x++)
   {
      int tc = (int)((x - left) * 1.25);
      if (tc < 0 || tc >= pw)
         continue;
      int col = o + rd32(o + 8 + tc * 4);
      while (wad[col] != 255)
      {
         int pt = wad[col];
         int pn = wad[col + 1];
         float y0f = top + pt * 0.8;
         int ya = (int)(y0f);
         int yb = (int)(y0f + pn * 0.8);
         if (ya < 0)
            ya = 0;
         if (yb > VH - 1)
            yb = VH - 1;
         int off = ya * STRIDE + x;
         for (int y = ya; y <= yb; y++)
         {
            int r = (int)((y - y0f) * 1.25);
            if (r >= pn)
               r = pn - 1;
            if (r < 0)
               r = 0;
            led16[off] = lit[3840 + wad[col + 3 + r]];
            off = off + STRIDE;
         }
         col = col + pn + 4;
      }
   }
}

// the gun in hand, its frame from the time since the shot
void drawWadGun()
{
   int bx = (int)(sin(bob) * 6.0);
   int by = (int)(absf(sin(bob)) * 5.0);
   if (weapon == 0)
   {
      int f = 0;
      if (gunT < 0.1)
         f = 1;
      else if (gunT < 0.2)
         f = 2;
      else if (gunT < 0.3)
         f = 3;
      if (gunT < 0.08)
         drawGunPic(pistolFlash, bx, by);
      drawGunPic(pistol[f], bx, by);
   }
   if (weapon == 1)
   {
      int f = 0;
      if (gunT >= 0.2 && gunT < 0.45)
         f = 1;
      else if (gunT >= 0.45 && gunT < 0.7)
         f = 2;
      else if (gunT >= 0.7 && gunT < 0.85)
         f = 3;
      else if (gunT >= 0.85 && gunT < 1.0)
         f = 2;
      if (gunT < 0.1)
         drawGunPic(shotgunFlash[0], bx, by);
      else if (gunT < 0.2)
         drawGunPic(shotgunFlash[1], bx, by);
      drawGunPic(shotgun[f], bx, by);
   }
   if (weapon == 2)
   {
      int f = 0;
      if (gunT < 0.25)
         f = (frame / 2) % 2;
      if (gunT < 0.1)
         drawGunPic(chaingunFlash[f], bx, by);
      drawGunPic(chaingun[f], bx, by);
   }
}

// a monster, drawn from the WAD
void drawFromWad(int i)
{
   int t = et[i];
   int c = cellAt(ex[i], ey[i]);
   int bright = 0;
   if (eflash[i] > 0)
      bright = 1;
   if (t == 0)
   {
      int o;
      if (es[i] == 2)
      {
         int f = (int)(edie[i] / 0.15);
         if (f > 3)
            f = 3;
         o = huskDie[f];
      }
      else if (eanim[i] > 0)
      {
         int f = 1;
         if (eanim[i] > 0.15)
            f = 0;
         o = huskHit[f * 8 + viewOf(i)];
      }
      else
      {
         int f = 0;
         if (emoving[i] == 1)
            f = (frame / 5) % 4;
         o = huskWalk[f * 8 + viewOf(i)];
      }
      drawPatch(o, ex[i], ey[i], ez[i], PPU, c, bright);
      return;
   }
   if (t == 1)
   {
      if (es[i] == 2)
      {
         int f = (int)(edie[i] / 0.15);
         if (f > 2)
            f = 2;
         drawPatch(droneDie[f], ex[i], ey[i], ez[i], PPU, c, bright);
         return;
      }
      int f = 0;
      if (eanim[i] > 0)
         f = 1;
      float z = ez[i] + 0.25 + sin(frame * 0.1 + i) * 0.05;
      drawPatch(droneLook[f * 8 + viewOf(i)], ex[i], ey[i], z, PPU, c, bright);
      return;
   }
   float z = ez[i] + 0.2 + sin(frame * 0.15 + i) * 0.08;
   drawPatch(wispFlame[(frame / 4 + i) % 4], ex[i], ey[i], z, PPU, c, 1);
}

// the sprite of a thing now, and how tall it stands (in cells)
int spriteOf(int i)
{
   int t = et[i];
   if (t == 0)
   {
      if (es[i] == 2)
         return 13;
      if (eanim[i] > 0)
         return 8;
      return 6 + (frame / 8) % 2;
   }
   if (t == 1)
   {
      if (es[i] == 2)
         return 2;
      if (eanim[i] > 0)
         return 1;
      return 0;
   }
   if (t == 2)
      return 9 + (frame / 4) % 2;
   if (t == 3)
      return 4;
   if (t == 4 || t >= 6)
      return 5;
   return 12;
}

void drawSprite(float sxw, float syw, float sz, float size, int sp, int lightCell, int bright)
{
   float inv = 1.0 / (planeX * dirY - dirX * planeY);
   float sx = sxw - px;
   float sy = syw - py;
   float tx = inv * (dirY * sx - dirX * sy);
   float ty = inv * (planeX * sy - planeY * sx);
   if (ty < 0.2)
      return;
   int scr = (int)((VW / 2) * (1.0 + tx / ty));
   int h = (int)(size * K / ty);
   if (h < 2)
      return;
   int bz = (int)((eyeZ - sz) * K / ty);
   int bottom = HZ + bz;
   int top = bottom - h;
   int left = scr - h / 2;
   int l = 15;
   if (bright == 0)
      l = light(lightCell, ty);
   int x0 = left;
   if (x0 < 0)
      x0 = 0;
   int x1 = left + h;
   if (x1 > VW)
      x1 = VW;
   for (int x = x0; x < x1; x++)
   {
      // what is still open at this distance in this column
      int t = 0;
      int b = VH - 1;
      int base = x * SEGS;
      int n = segN[x];
      for (int k = 0; k < n; k++)
         if (segD[base + k] < ty)
         {
            t = segT[base + k];
            b = segB[base + k];
         }
      if (t > b)
         continue;
      int y0 = top;
      if (y0 < t)
         y0 = t;
      int y1 = top + h - 1;
      if (y1 > b)
         y1 = b;
      int tcol = sp * 1024 + ((x - left) * 32) / h;
      int off = y0 * STRIDE + x;
      for (int y = y0; y <= y1; y++)
      {
         int c = spr[tcol + (((y - top) * 32) / h) * 32];
         if (c != 0)
            led16[off] = lit[l * 256 + c];
         off = off + STRIDE;
      }
   }
}

void drawThings()
{
   int n = 0;
   for (int i = 0; i < ne; i++)
   {
      if (es[i] == 2 && et[i] >= 2)
         continue;
      float dx = ex[i] - px;
      float dy = ey[i] - py;
      order[n] = i;
      odist[n] = dx * dx + dy * dy;
      n++;
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
   for (int k = 0; k < n; k++)
   {
      int i = order[k];
      int t = et[i];
      float size = 0.4;
      float z = ez[i];
      int bright = 0;
      if (t == 0)
         size = 0.95;
      if (t == 1)
      {
         size = 0.6;
         if (es[i] != 2)
            z = z + 0.35 + sin(frame * 0.1 + i) * 0.05;
      }
      if (t == 2)
      {
         size = 0.6;
         z = z + 0.3 + sin(frame * 0.15 + i) * 0.08;
         bright = 1;
      }
      if (useWad == 1 && t <= 2)
      {
         drawFromWad(i);
         continue;
      }
      if (useWad == 1)
      {
         int o = pickupPic[t];
         if (t == 5)
            o = keyPic[(frame / 8) % 2];
         drawPatch(o, ex[i], ey[i], ez[i], PPU, cellAt(ex[i], ey[i]), 0);
         continue;
      }
      int c = cellAt(ex[i], ey[i]);
      if (eflash[i] > 0)
         bright = 1;
      drawSprite(ex[i], ey[i], z, size, spriteOf(i), c, bright);
   }
   for (int s = 0; s < SHOTS; s++)
      if (fon[s] == 1)
      {
         if (useWad == 1)
            drawPatch(fireBall[(frame / 3) % 2], fx[s], fy[s], fz[s] - 0.15, PPU, 0, 1);
         else
            drawSprite(fx[s], fy[s], fz[s] - 0.15, 0.3, 11, 0, 1);
      }
}

// ---- the weapon, the status bar, the words

void fill(int x, int y, int w, int h, int c)
{
   uint16_t col = pal[c];
   for (int j = 0; j < h; j++)
   {
      int off = (y + j) * STRIDE + x;
      for (int i = 0; i < w; i++)
         led16[off + i] = col;
   }
}

void glyph(int g, int x, int y, int sc, int c)
{
   uint16_t col = pal[c];
   for (int r = 0; r < 8; r++)
   {
      int bits = font8[g * 8 + r];
      for (int b = 0; b < 8; b++)
         if (((bits >> (7 - b)) & 1) == 1)
            for (int j = 0; j < sc; j++)
            {
               int off = (y + r * sc + j) * STRIDE + x + b * sc;
               for (int i = 0; i < sc; i++)
                  led16[off + i] = col;
            }
   }
}

// capitals, digits, spaces and . : / + in the 8x8 font, 8 dots apart
void text(char *s, int x, int y, int sc, int c, int shadow)
{
   int i = 0;
   while (s[i] != 0)
   {
      int ch = s[i];
      int g = -1;
      if (ch >= 48 && ch <= 57)
         g = ch - 48;
      if (ch >= 65 && ch <= 90)
         g = ch - 55;
      if (ch == 46)
         g = 36;
      if (ch == 58)
         g = 37;
      if (ch == 47)
         g = 38;
      if (ch == 43)
         g = 39;
      if (g >= 0)
      {
         if (shadow >= 0)
            glyph(g, x + sc, y + sc, sc, shadow);
         glyph(g, x, y, sc, c);
      }
      x = x + 8 * sc;
      i++;
   }
}

int textWidth(char *s, int sc)
{
   int n = 0;
   while (s[n] != 0)
      n++;
   return n * 8 * sc;
}

void centred(char *s, int y, int sc, int c)
{
   text(s, (VW - textWidth(s, sc)) / 2, y, sc, c, 16);
}

void number(int v, int digits, int x, int y, int sc, int c)
{
   for (int i = digits - 1; i >= 0; i--)
   {
      glyph(v % 10, x + i * 8 * sc, y, sc, c);
      v = v / 10;
   }
}

void drawGun()
{
   int b = (int)(absf(sin(bob)) * 4.0);
   int gx = 106;
   int gy = 110 + b;
   if (flash > 0)
   {
      gy = gy + 3;
      for (int y = -10; y <= 10; y++)
         for (int x = -10; x <= 10; x++)
         {
            int r = x * x + y * y;
            int c = 0;
            if (r < 16)
               c = 31;
            else if (r < 50)
               c = 44;
            else if (r < 100 && (x == 0 || y == 0 || x == y || x == 0 - y))
               c = 42;
            if (c != 0 && gy - 8 + y >= 0)
               fill(120 + x, gy - 8 + y, 1, 1, c);
         }
   }
   for (int x = 0; x < 28; x++)
   {
      int c = 27 - x / 4;
      int y0 = gy;
      if (x < 5 || x > 22)
         y0 = gy + 10;
      if (y0 < VH)
         fill(gx + x, y0, 1, VH - y0, c);
   }
   fill(gx + 9, gy - 2, 10, 3, 18);
   fill(gx + 11, gy + 14, 6, 4, 40);
}

void drawFace(int fxp, int fyp)
{
   if (useWad == 1)
   {
      int lv = (100 - health) * 5 / 101;
      if (lv < 0)
         lv = 0;
      if (lv > 4)
         lv = 4;
      int o = faceLook3[lv * 3 + faceLook + 1];
      if (state != 1)
         o = faceLook3[lv * 3 + 1];
      if (hurt > 0)
         o = faceOuch[lv];
      if (health <= 0)
         o = faceDead;
      drawPic(o, fxp, fyp, VH + 32);
      return;
   }
   int lv = faceLevel();
   int ouch = 0;
   if (hurt > 0)
      ouch = 1;
   int look = faceLook;
   if (state != 1)
      look = 0;
   for (int y = 0; y < 30; y++)
   {
      int off = (fyp + y) * STRIDE + fxp;
      for (int x = 0; x < 24; x++)
      {
         int c = facePixel(x, y, lv, look, ouch);
         if (c != 0)
            led16[off + x] = pal[c];
      }
   }
}

void box(int x, int y, int w, int h)
{
   fill(x, y, w, h, 16);
   fill(x, y, w, 1, 8);
   fill(x, y + h - 1, w, 1, 9);
}

void drawHud()
{
   fill(0, VH, VW, 32, 1);
   fill(0, VH, VW, 1, 9);
   box(2, VH + 2, 70, 28);
   text("HEALTH", 13, VH + 4, 1, 11, -1);
   int hc = 14;
   if (health < 25)
      hc = 12;
   number(health, 3, 13, VH + 14, 2, hc);
   box(104, VH + 1, 32, 30);
   drawFace(108, VH + 1);
   box(140, VH + 2, 62, 28);
   int am = bullets;
   if (weapon == 1)
   {
      am = shells;
      text("SHELLS", 147, VH + 4, 1, 11, -1);
   }
   else
      text("BULLETS", 143, VH + 4, 1, 11, -1);
   int ac = 14;
   if (am == 0)
      ac = 12;
   number(am, 3, 147, VH + 14, 2, ac);
   box(206, VH + 2, 32, 28);
   if (hasKey == 1)
   {
      if (useWad == 1)
         drawPic(keyPic[(frame / 8) % 2], 216, VH + 8, VH + 30);
      else
      {
         fill(214, VH + 8, 16, 14, 9);
         fill(216, VH + 10, 12, 10, 32);
         fill(217, VH + 13, 10, 2, 31);
      }
   }
}

void drawPad()
{
   fill(0, 176, VW, 64, 18);
   fill(0, 176, VW, 1, 20);
   // the cross: walk and turn
   int lc = 21;
   fill(40, 182, 24, 52, lc);
   fill(14, 196, 76, 24, lc);
   if (btn(B_UP) == 1)
      fill(42, 184, 20, 20, 26);
   if (btn(B_DOWN) == 1)
      fill(42, 212, 20, 20, 26);
   if (btn(B_LEFT) == 1)
      fill(16, 198, 22, 20, 26);
   if (btn(B_RIGHT) == 1)
      fill(66, 198, 22, 20, 26);
   fill(48, 204, 8, 8, 18);
   // use, and the next gun
   int uc = 22;
   if (btn(B_USE) == 1)
      uc = 27;
   fill(108, 182, 48, 24, uc);
   fill(108, 182, 48, 1, 26);
   text("USE", 120, 190, 1, 31, 16);
   uc = 22;
   if (btn(B_WPN) == 1)
      uc = 27;
   fill(108, 210, 48, 24, uc);
   fill(108, 210, 48, 1, 26);
   text("GUN", 120, 218, 1, 31, 16);
   // fire
   int c = 112;
   if (btn(B_FIRE) == 1)
      c = 40;
   for (int dy = -24; dy <= 24; dy++)
   {
      int w = 0;
      while ((w + 1) * (w + 1) + dy * dy <= 576)
         w++;
      fill(200 - w, 208 + dy, 2 * w + 1, 1, c);
   }
   text("FIRE", 184, 204, 1, 31, 16);
}

void drawBorder(int c, int w)
{
   fill(0, 0, VW, w, c);
   fill(0, VH - w, VW, w, c);
   fill(0, 0, w, VH, c);
   fill(VW - w, 0, w, VH, c);
}

void drawMessage()
{
   if (messageT <= 0)
      return;
   if (message == 1)
      text("HEALTH", 4, 4, 1, 31, 16);
   if (message == 2)
      text("AMMO", 4, 4, 1, 31, 16);
   if (message == 3)
      text("THE BLUE KEY", 4, 4, 1, 31, 16);
   if (message == 4)
      text("THE BLUE DOOR IS LOCKED", 4, 4, 1, 31, 16);
   if (message == 5)
      text("OUT OF AMMO", 4, 4, 1, 31, 16);
   if (message == 6)
      text("SHELLS", 4, 4, 1, 31, 16);
   if (message == 7)
      text("A SHOTGUN", 4, 4, 1, 31, 16);
   if (message == 8)
      text("A CHAINGUN", 4, 4, 1, 31, 16);
   if (message == 10)
      centred("THE FOUNDRY", 30, 2, 92);
   if (message == 11)
      centred("THE FURNACE", 30, 2, 92);
   if (message == 12)
      centred("THE SMELTER YARD", 30, 1, 92);
}

void drawFrame()
{
   eyeZ = pz + 0.6 + sin(bob) * 0.02;
   if (state == 0 && titlePic >= 0)
   {
      // the title screen from the WAD
      drawTitlePic();
      if ((frame / 15) % 2 == 0)
         centred("PRESS FIRE", 120, 2, 44);
      drawHud();
      drawPad();
      return;
   }
   castView();
   drawThings();
   if (state == 1)
   {
      if (useWad == 1)
         drawWadGun();
      else
         drawGun();
      drawMessage();
   }
   if (hurt > 0)
      drawBorder(40, 3);
   else if (glow > 0)
      drawBorder(44, 2);
   if (state == 0)
   {
      centred("IRONFORGE", 24, 3, 92);
      centred("CLEAR THE FOUNDRY", 64, 1, 31);
      centred("FIND THE BLUE KEY", 76, 1, 31);
      centred("REACH THE EXIT", 88, 1, 31);
      if ((frame / 15) % 2 == 0)
         centred("PRESS FIRE", 112, 2, 44);
   }
   if (state == 2)
   {
      if (level + 1 < LEVELS)
         centred("LEVEL CLEARED", 40, 2, 48);
      else
         centred("OUT OF THE FOUNDRY", 40, 1, 48);
      int secs = (int)(playTime);
      fill(80, 60, 80, 1, 16);
      text("KILLS", 64, 70, 1, 31, 16);
      number(kills, 2, 136, 70, 1, 44);
      text("/", 152, 70, 1, 31, 16);
      number(foes, 2, 160, 70, 1, 44);
      text("TIME", 64, 84, 1, 31, 16);
      number(secs / 60, 2, 120, 84, 1, 44);
      text(":", 136, 84, 1, 31, 16);
      number(secs % 60, 2, 144, 84, 1, 44);
      if (stateT > 2.0 && (frame / 15) % 2 == 0)
         centred("PRESS FIRE", 112, 1, 44);
   }
   if (state == 3)
   {
      centred("YOU DIED", 50, 3, 40);
      if (stateT > 2.0 && (frame / 15) % 2 == 0)
         centred("PRESS FIRE", 100, 1, 31);
   }
   drawHud();
   drawPad();
}

// ---- the rules

void tryMove(float nx, float ny)
{
   float m = 0.22;
   float ox = nx + m;
   if (nx < px)
      ox = nx - m;
   if (canEnter(cellAt(ox, py - m), pz, 0.75) == 1 && canEnter(cellAt(ox, py + m), pz, 0.75) == 1)
      px = nx;
   float oy = ny + m;
   if (ny < py)
      oy = ny - m;
   if (canEnter(cellAt(px - m, oy), pz, 0.75) == 1 && canEnter(cellAt(px + m, oy), pz, 0.75) == 1)
      py = ny;
}

// can a see b? solid cells and shut doors are in the way
int lineOfSight(float ax, float ay, float bx, float by)
{
   float dx = bx - ax;
   float dy = by - ay;
   float len = sqrtf2(dx * dx + dy * dy);
   int n = (int)(len * 4.0);
   n = n + 1;
   for (int i = 1; i < n; i++)
   {
      float t = i * 1.0 / n;
      int c = cellAt(ax + dx * t, ay + dy * t);
      if (c < 0)
         return 0;
      int k = cellKind[c];
      if (kSolid[k] == 1 || cellCeil[c] - kFloor[k] < 0.5)
         return 0;
   }
   return 1;
}

void hurtPlayer(int dmg)
{
   if (state != 1)
      return;
   health = health - dmg;
   hurt = 0.3;
   if (health <= 0)
   {
      health = 0;
      state = 3;
      stateT = 0;
   }
}

// the foe nearest the middle of the view within `cone` (a fraction of its
// size), if it can be seen; -1 when none
int target(float cone)
{
   float inv = 1.0 / (planeX * dirY - dirX * planeY);
   int best = -1;
   float bestD = 100.0;
   for (int i = 0; i < ne; i++)
   {
      if (et[i] > 2 || es[i] == 2)
         continue;
      float sx = ex[i] - px;
      float sy = ey[i] - py;
      float tx = inv * (dirY * sx - dirX * sy);
      float ty = inv * (planeX * sy - planeY * sx);
      if (ty > 0.2 && ty < bestD)
      {
         float scr = (VW / 2) * (1.0 + tx / ty);
         float half = cone * K / ty + 4.0;
         if (absf(scr - VW / 2) < half && lineOfSight(px, py, ex[i], ey[i]) == 1)
         {
            best = i;
            bestD = ty;
         }
      }
   }
   return best;
}

void hitFoe(int i, int dmg)
{
   if (i < 0)
      return;
   ehp[i] = ehp[i] - dmg;
   eflash[i] = 0.15;
   es[i] = 1;
   if (ehp[i] <= 0)
   {
      es[i] = 2;
      kills++;
   }
}

// the gun in hand: the pistol one shot, the shotgun a spread that hits
// harder up close, the chaingun fast
void fire()
{
   if (weapon == 1)
   {
      if (shells <= 0)
      {
         message = 5;
         messageT = 1.0;
         return;
      }
      shells--;
      cool = 1.0;
      int i = target(0.6);
      int dmg = 2;
      if (i >= 0)
      {
         float dx = ex[i] - px;
         float dy = ey[i] - py;
         if (dx * dx + dy * dy < 9.0)
            dmg = 4;
      }
      hitFoe(i, dmg);
   }
   else
   {
      if (bullets <= 0)
      {
         message = 5;
         messageT = 1.0;
         return;
      }
      bullets--;
      cool = 0.4;
      if (weapon == 2)
         cool = 0.12;
      hitFoe(target(0.35), 1);
   }
   flash = 0.1;
   gunT = 0;
}

// the next gun the player has
void nextWeapon()
{
   for (int k = 1; k <= 3; k++)
   {
      int w = (weapon + k) % 3;
      if (have[w] == 1)
      {
         weapon = w;
         gunT = 10.0;
         return;
      }
   }
}

void useAhead()
{
   int c = cellAt(px + dirX * 0.8, py + dirY * 0.8);
   if (c < 0)
      return;
   int kk = kKind[cellKind[c]];
   if (kk == 4)
   {
      state = 2;
      stateT = 0;
      return;
   }
   if (kk == 3 && hasKey == 0)
   {
      message = 4;
      messageT = 1.5;
      return;
   }
   for (int d = 0; d < doors; d++)
      if (doorCell[d] == c && (doorState[d] == 0 || doorState[d] == 3))
         doorState[d] = 1;
}

void moveDoors()
{
   int pc = cellAt(px, py);
   for (int d = 0; d < doors; d++)
   {
      int c = doorCell[d];
      int k = cellKind[c];
      float shut = kFloor[k];
      float high = kCeil[k];
      if (doorState[d] == 1)
      {
         cellCeil[c] = cellCeil[c] + DT * 1.5;
         if (cellCeil[c] >= high)
         {
            cellCeil[c] = high;
            doorState[d] = 2;
            doorTime[d] = 4.0;
         }
      }
      else if (doorState[d] == 2)
      {
         doorTime[d] = doorTime[d] - DT;
         if (doorTime[d] <= 0 && pc != c)
            doorState[d] = 3;
      }
      else if (doorState[d] == 3)
      {
         if (pc == c)
            doorState[d] = 1;
         cellCeil[c] = cellCeil[c] - DT * 1.5;
         if (cellCeil[c] <= shut)
         {
            cellCeil[c] = shut;
            doorState[d] = 0;
         }
      }
   }
}

void moveFoe(int i, float sp, float keep)
{
   float dx = px - ex[i];
   float dy = py - ey[i];
   float d = sqrtf2(dx * dx + dy * dy);
   emoving[i] = 0;
   if (d < 0.01)
      return;
   efacing[i] = atan2(dy, dx);
   if (d < keep)
      return;
   emoving[i] = 1;
   float vx = dx / d * sp * DT;
   float vy = dy / d * sp * DT;
   float z = ez[i];
   float m = 0.25;
   float ox = ex[i] + vx + m;
   if (vx < 0)
      ox = ex[i] + vx - m;
   int c = cellAt(ox, ey[i]);
   if (canEnter(c, z, 0.7) == 1)
      ex[i] = ex[i] + vx;
   float oy = ey[i] + vy + m;
   if (vy < 0)
      oy = ey[i] + vy - m;
   c = cellAt(ex[i], oy);
   if (canEnter(c, z, 0.7) == 1)
      ey[i] = ey[i] + vy;
   c = cellAt(ex[i], ey[i]);
   if (c >= 0)
      ez[i] = floorOf(c);
}

void updateThings()
{
   for (int i = 0; i < ne; i++)
   {
      if (eflash[i] > 0)
         eflash[i] = eflash[i] - DT;
      if (eanim[i] > 0)
         eanim[i] = eanim[i] - DT;
      if (es[i] == 2)
      {
         edie[i] = edie[i] + DT;
         continue;
      }
      int t = et[i];
      float dx = px - ex[i];
      float dy = py - ey[i];
      float d = sqrtf2(dx * dx + dy * dy);
      // the pickups
      if (t >= 3)
      {
         if (d < 0.5)
         {
            if (t == 3 && health < 100)
            {
               health = health + 25;
               if (health > 100)
                  health = 100;
               es[i] = 2;
               message = 1;
               messageT = 1.0;
               glow = 0.2;
            }
            if (t == 4)
            {
               bullets = bullets + 20;
               if (bullets > 200)
                  bullets = 200;
               es[i] = 2;
               message = 2;
               messageT = 1.0;
               glow = 0.2;
            }
            if (t == 6)
            {
               shells = shells + 8;
               if (shells > 50)
                  shells = 50;
               es[i] = 2;
               message = 6;
               messageT = 1.0;
               glow = 0.2;
            }
            if (t == 7 || t == 8)
            {
               // a new gun: in hand at once, with its ammo
               weapon = t - 6;
               have[weapon] = 1;
               if (t == 7)
                  shells = shells + 8;
               else
                  bullets = bullets + 40;
               es[i] = 2;
               message = t;
               messageT = 1.5;
               glow = 0.3;
            }
            if (t == 5)
            {
               hasKey = 1;
               es[i] = 2;
               message = 3;
               messageT = 1.5;
               glow = 0.3;
            }
         }
         continue;
      }
      // waking: seen, or near
      if (es[i] == 0)
      {
         if (d < 9.0 && lineOfSight(ex[i], ey[i], px, py) == 1)
            es[i] = 1;
         continue;
      }
      ecool[i] = ecool[i] - DT;
      int sees = 0;
      if (d < 12.0 && lineOfSight(ex[i], ey[i], px, py) == 1)
         sees = 1;
      if (t == 0)
      {
         // the husk: comes close, strikes
         moveFoe(i, 1.3, 0.7);
         if (d < 1.0 && ecool[i] <= 0)
         {
            eanim[i] = 0.3;
            ecool[i] = 1.0;
            hurtPlayer(8 + rnd(6));
         }
      }
      if (t == 1)
      {
         // the drone: keeps away, shoots when it sees
         moveFoe(i, 1.0, 3.5);
         if (sees == 1 && ecool[i] <= 0)
         {
            eanim[i] = 0.2;
            ecool[i] = 1.4 + rnd(10) * 0.1;
            if (rnd(10) < 6)
               hurtPlayer(4 + rnd(4));
         }
      }
      if (t == 2)
      {
         // the wisp: drifts nearer, throws fire
         moveFoe(i, 0.8, 4.0);
         if (sees == 1 && ecool[i] <= 0)
         {
            ecool[i] = 2.2;
            for (int s = 0; s < SHOTS; s++)
               if (fon[s] == 0)
               {
                  fon[s] = 1;
                  fx[s] = ex[i];
                  fy[s] = ey[i];
                  fz[s] = ez[i] + 0.5;
                  fdx[s] = dx / d * 3.5;
                  fdy[s] = dy / d * 3.5;
                  break;
               }
         }
      }
   }
   for (int s = 0; s < SHOTS; s++)
      if (fon[s] == 1)
      {
         fx[s] = fx[s] + fdx[s] * DT;
         fy[s] = fy[s] + fdy[s] * DT;
         int c = cellAt(fx[s], fy[s]);
         if (c < 0 || kSolid[cellKind[c]] == 1 || cellCeil[c] < fz[s])
            fon[s] = 0;
         float dx = fx[s] - px;
         float dy = fy[s] - py;
         if (fon[s] == 1 && dx * dx + dy * dy < 0.12)
         {
            fon[s] = 0;
            hurtPlayer(10 + rnd(6));
         }
      }
}

void step()
{
   frame++;
   if (hurt > 0)
      hurt = hurt - DT;
   if (glow > 0)
      glow = glow - DT;
   if (flash > 0)
      flash = flash - DT;
   if (messageT > 0)
      messageT = messageT - DT;
   stateT = stateT + DT;
   faceT = faceT - DT;
   if (faceT <= 0)
   {
      faceT = 1.0 + rnd(20) * 0.1;
      faceLook = rnd(3) - 1;
   }
   if (state == 0)
   {
      setAngle(pa + DT * 0.3);
      if (btnp(B_FIRE) == 1)
         newGame();
      return;
   }
   if (state == 2 || state == 3)
   {
      if (stateT > 2.0 && btnp(B_FIRE) == 1)
      {
         if (state == 2 && level + 1 < LEVELS)
            newLevel(level + 1);
         else
         {
            state = 0;
            stateT = 0;
         }
      }
      return;
   }
   playTime = playTime + DT;
   // walking and turning
   if (btn(B_LEFT) == 1)
      setAngle(pa - DT * 2.4);
   if (btn(B_RIGHT) == 1)
      setAngle(pa + DT * 2.4);
   float sp = 0;
   if (btn(B_UP) == 1)
      sp = 3.0;
   if (btn(B_DOWN) == 1)
      sp = -2.2;
   if (sp != 0)
   {
      tryMove(px + dirX * sp * DT, py + dirY * sp * DT);
      bob = bob + DT * 10.0;
   }
   // standing, falling: the floor under the feet
   int c = cellAt(px, py);
   float f = floorOf(c);
   if (pz < f)
   {
      pz = f;
      vz = 0;
   }
   if (pz > f)
   {
      vz = vz - DT * 9.0;
      pz = pz + vz * DT;
      if (pz <= f)
      {
         pz = f;
         vz = 0;
      }
   }
   // the slime burns
   if (kKind[cellKind[c]] == 5 && pz <= f + 0.01 && frame % 15 == 0)
      hurtPlayer(5);
   if (cool > 0)
      cool = cool - DT;
   if (btn(B_FIRE) == 1 && cool <= 0)
      fire();
   if (btnp(B_USE) == 1)
      useAhead();
   if (btnp(B_WPN) == 1)
      nextWeapon();
   gunT = gunT + DT;
   moveDoors();
   updateThings();
}

int main()
{
   setPixelsize(2);           // 240x240, each 2x2 pixels
   makePalette();
   makeLight();
   makeTextures();
   makeSprites();
   moreSprites();
   makeSky();
   loadWadSprites();
   newGame();
   state = 0;
   px = 16.5;
   py = 12.5;
   pz = 0;
   setAngle(4.0);
   held = 0;
   before = 0;
   uint32_t last = millis();
   while (true)
   {
      while (millis() - last >= STEP_MS)
      {
         last = last + STEP_MS;
         before = held;
         held = touchHeld;
         step();
      }
      drawFrame();
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
  bindFunction((char *)"void", (char *)"show16", NULL, (void *)show16);
  bindFunction((char *)"void", (char *)"setPixelsize", (char *)"int", (void *)setPixelsize);
  bindFunction((char *)"uint32_t", (char *)"millis", NULL, (void *)scriptMillis);
  bindFunction((char *)"uint32_t", (char *)"rand", (char *)"uint32_t", (void *)scriptRand);
  bindFunction((char *)"void", (char *)"setBrightness", (char *)"int", (void *)scriptSetBrightness);
  bindFunction((char *)"float", (char *)"sin", (char *)"float", (void *)scriptSin);
  bindFunction((char *)"float", (char *)"hypot", (char *)"float,float", (void *)scriptHypot);
  bindFunction((char *)"float", (char *)"atan2", (char *)"float,float", (void *)scriptAtan2);
  // the monsters' WAD
  loadWad("/ironforge.wad");
  bindVariable((char *)"uint8_t *", (char *)"wad", NULL, (void *)&wadData);
  bindVariable((char *)"int", (char *)"wadLoaded", NULL, (void *)&wadLoaded);
  bindFunction((char *)"int", (char *)"wadSprite", (char *)"char*,int,int", (void *)wadSprite);
  bindFunction((char *)"int", (char *)"wadFind", (char *)"char*", (void *)wadFind);

  uint32_t t0 = millis();
  static ScriptExecutable exec = parseScript(ironforge);
  if (!exec.isExeExists()) {
    showCompileError();
    return;
  }
  printf("ironforge: %u bytes of code, compiled in %u ms\n", (unsigned)exec.exe.binary_size, (unsigned)(millis() - t0));

  // the touch screen, read on core 0
  xTaskCreatePinnedToCore(touchTask, "touch", 4096, NULL, 3, NULL, 0);

  exec.execute("main");
}

void loop() {
  delay(1000);
}
