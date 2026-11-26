// Cyclic cellular automaton on the 800x480 RGB panel, written so that the
// compiler vectorizes it on the ESP32-S3 (see the README, "ESP32-S3: vector
// code"). 400x240 cells of 2x2 pixels, each in one of 16 states: a cell moves
// to the next state when one of its four neighbours is already there. From
// random noise grow small crystals, then large domains, then spirals that
// turn for ever; each cell that changes glows, and the colours drift.
//
// Each frame is three passes over every row, all vectorized (16 cells per
// pass): the step, the glow (15 when the cell changed, then one less a
// frame), and the colour index (state + 16 * glow); then an ordinary loop
// draws through a 256-colour palette.
//
// The script is compiled twice: first with the vectorizer off
// (setLoopVectorize(false)) for 300 frames, then on, for good. Every 100
// frames it prints the time of a frame's computing and drawing, and a
// checksum of the picture: the same in both runs.
//
// Needs the modified LovyanGFX Bus_RGB driver and FastLED (CHSV); Matouch7.h
// is the panel configuration. Board: ESP32S3 Dev Module, PSRAM "OPI PSRAM",
// Flash Size 16MB.
#include "Matouch7.h"
#include "FastLED.h"
#include "script_executable.h"
#include "binding.h"
#include "vectorize.h"

LGFX lcd;
uint16_t *frame_buffer;
static uint32_t _sizex = 800;
static uint32_t _sizey = 480;

// same conversion as keybard5inch_v2: HSV -> RGB565, byte swapped like LovyanGFX's frame buffer
static uint16_t hsv16(uint8_t h, uint8_t s, uint8_t v) {
  CRGB d = CHSV(h, s, v);
  uint16_t j = lcd.color565(d.red, d.green, d.blue);
  return ((j & 0xff) << 8) | ((j & 0xFF00) >> 8);
}

void show16() {
  static uint32_t frames = 0, t0 = 0;
  lcd._bus_instance.show();
  frame_buffer = (uint16_t *)lcd._bus_instance._frame_buffer;
  lcd._panel_instance.resetWriteBuffer();
  if (++frames % 100 == 0) {
    uint32_t t = millis();
    printf("%.1f fps\n", 100000.0f / (t - t0));
    t0 = t;
  }
}

static uint32_t scriptMicros() {
  return micros();
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

char spirals[] = R"EOF(
#define width 400
#define height 240

// two grids of states 0..15, used in turn: cur -> nxt, then nxt -> cur
uint8_t cur[height, width];
uint8_t nxt[height, width];
// how bright each cell glows: 15 when it just changed, then one less a frame
uint8_t glow[height, width];
// the colour of each cell: its state + 16 * its glow, an index into pal
uint8_t idx[height, width];
uint16_t pal[256];
uint32_t seed = 12345;

// a cell moves to the next state (15 wraps to 0) when one of its four
// neighbours is already there
void cycleCurToNxt(int j)
{
   for (int i = 1; i < width - 1; i++)
   {
      uint8_t c = cur[j, i];
      uint8_t n = (c + 1) & 15;
      uint8_t hit = (cur[j, i - 1] == n) | (cur[j, i + 1] == n) | (cur[j - 1, i] == n) | (cur[j + 1, i] == n);
      nxt[j, i] = (c + hit) & 15;
   }
}

void cycleNxtToCur(int j)
{
   for (int i = 1; i < width - 1; i++)
   {
      uint8_t c = nxt[j, i];
      uint8_t n = (c + 1) & 15;
      uint8_t hit = (nxt[j, i - 1] == n) | (nxt[j, i + 1] == n) | (nxt[j - 1, i] == n) | (nxt[j + 1, i] == n);
      cur[j, i] = (c + hit) & 15;
   }
}

// 15 where the cell changed (0 - 1 is all ones), else the glow fades by one
void glowRow(int j)
{
   for (int i = 0; i < width; i++)
   {
      uint8_t changed = nxt[j, i] != cur[j, i];
      glow[j, i] = ((0 - changed) & 15) | (glow[j, i] - (glow[j, i] > 0));
   }
}

// the palette index of the new state: state + 16 * glow
void indexFromNxt(int j)
{
   for (int i = 0; i < width; i++)
   {
      uint8_t g2 = glow[j, i] + glow[j, i];
      uint8_t g4 = g2 + g2;
      uint8_t g8 = g4 + g4;
      idx[j, i] = nxt[j, i] + g8 + g8;
   }
}

void indexFromCur(int j)
{
   for (int i = 0; i < width; i++)
   {
      uint8_t g2 = glow[j, i] + glow[j, i];
      uint8_t g4 = g2 + g2;
      uint8_t g8 = g4 + g4;
      idx[j, i] = cur[j, i] + g8 + g8;
   }
}

int random16()
{
   seed = seed * 1103515245 + 12345;
   return (seed >> 16) & 15;
}

void seedGrid()
{
   for (int h = 0; h < width * height; h++)
   {
      cur[h] = random16();
      nxt[h] = cur[h];
      glow[h] = 0;
   }
}

// one frame: a step one way or the other, then the glow and the colours
void frame(int f)
{
   if ((f & 1) == 0)
   {
      for (int j = 1; j < height - 1; j++)
         cycleCurToNxt(j);
      for (int j = 0; j < height; j++)
      {
         glowRow(j);
         indexFromNxt(j);
      }
   }
   else
   {
      for (int j = 1; j < height - 1; j++)
         cycleNxtToCur(j);
      // glowRow compares nxt (old) with cur (new): the same test
      for (int j = 0; j < height; j++)
      {
         glowRow(j);
         indexFromCur(j);
      }
   }
}

// the palette for a hue: index state + 16 * glow
void buildPalette(int hue)
{
   for (int g = 0; g < 16; g++)
   {
      for (int s = 0; s < 16; s++)
      {
         pal[s + g * 16] = hsv2((s * 16 + hue) % 256, 255, 60 + g * 13);
      }
   }
}

void draw()
{
   for (int j = 0; j < height; j++)
   {
      for (int i = 0; i < width; i++)
      {
         led16[j * 800 + i] = pal[idx[j, i]];
      }
   }
}

// frames: how many to show, 0 for ever (the noise comes back every 1500)
int main()
{
   setPixelsize(2);
   seedGrid();
   uint32_t computeTime = 0;
   uint32_t drawTime = 0;
   int f = 0;
   while (frames == 0 || f < frames)
   {
      if (f > 0 && f % 1500 == 0)
         seedGrid();
      uint32_t t0 = micros();
      frame(f);
      uint32_t t1 = micros();
      buildPalette(f / 2);
      draw();
      uint32_t t2 = micros();
      show16();
      computeTime = computeTime + (t1 - t0);
      drawTime = drawTime + (t2 - t1);
      f++;
      if (f % 100 == 0)
      {
         int sum = 0;
         for (int h = 0; h < width * height; h++)
            sum = sum + idx[h] * (h % 89 + 1);
         printfln("BENCH %s frame %d: compute %d us, draw %d us, checksum %d", vectorized ? "vector" : "scalar", f, computeTime / 100, drawTime / 100, sum);
         computeTime = 0;
         drawTime = 0;
      }
   }
}
)EOF";

// lcd.init() on core 0 so Bus_RGB's line copy interrupts stay off core 1, where the script draws
static void lcdInitTask(void *done) {
  lcd.init();
  xTaskNotifyGive((TaskHandle_t)done);
  vTaskDelete(NULL);
}

static int frames = 0;
static int vectorized = 0;

static void runSpirals(bool vector, int count) {
  setLoopVectorize(vector);
  vectorized = vector;
  frames = count;
  ScriptExecutable exec = parseScript(spirals);
  if (!exec.isExeExists()) {
    showCompileError();
    return;
  }
  printf("%s: %u bytes of code\n", vector ? "vectorized" : "not vectorized", (unsigned)exec.exe.binary_size);
  exec.execute("main");
  exec.free();
}

void setup() {
  Serial.begin(115200);
  xTaskCreatePinnedToCore(lcdInitTask, "lcdInit", 4096, xTaskGetCurrentTaskHandle(), 5, NULL, 0);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  frame_buffer = (uint16_t *)lcd._bus_instance._frame_buffer;

  // a pointer, not an array: show16() swaps the frame buffers
  bindVariable((char *)"uint16_t *", (char *)"led16", NULL, (void *)&frame_buffer);
  bindVariable((char *)"int", (char *)"frames", NULL, (void *)&frames);
  bindVariable((char *)"int", (char *)"vectorized", NULL, (void *)&vectorized);
  bindFunction((char *)"uint16_t", (char *)"hsv2", (char *)"int,int,int", (void *)hsv16);
  bindFunction((char *)"void", (char *)"show16", NULL, (void *)show16);
  bindFunction((char *)"void", (char *)"setPixelsize", (char *)"int", (void *)setPixelsize);
  bindFunction((char *)"uint32_t", (char *)"micros", NULL, (void *)scriptMicros);

  runSpirals(false, 300); // ordinary loops, for comparison
  runSpirals(true, 0);    // the S3's vector code, for good
}

void loop() {
  delay(1000);
}
