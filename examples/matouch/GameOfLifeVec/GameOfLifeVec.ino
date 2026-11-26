// Game of Life on the 800x480 RGB panel, 400x240 cells of 2x2 pixels
// (setPixelsize(2)), starting from one vertical line in the middle, written so that the
// compiler vectorizes it on the ESP32-S3 (see the README, "Loops vectorized on
// the ESP32-S3"): a generation is the sum of each row's three neighbours into a
// line buffer (sumRow), then the rule on three line buffers (ruleRow), both
// loops over byte arrays with no if. Drawing stays an ordinary loop.
// Every 50 generations it prints the average time of a generation and of its
// drawing, and a checksum of the grid (the same with or without vector code).
//
// Needs the modified LovyanGFX Bus_RGB driver (double frame buffer with show(),
// _frame_buffer, pixel_size, offset) and FastLED (CHSV). Matouch7.h is the panel
// configuration (Waveshare ESP32-S3-Touch-LCD 800x480 pinout).
//
// For the ESP32-S3-Touch-LCD-7: a 7-inch 800x480 RGB touch screen with an
// ESP32-S3 (16MB flash, 8MB OPI PSRAM).
// Board: ESP32S3 Dev Module, PSRAM "OPI PSRAM", Flash Size 16MB.
#include "Matouch7.h"
#include "FastLED.h"
#include "script_executable.h"
#include "binding.h"

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

char gameOfLife[] = R"EOF(
#define width 400
#define height 240

uint8_t copy[height, width];

// three line buffers of horizontal sums, used in rotation
uint8_t hs[3, width];

// hs[s] = left + cell + right, for row j
void sumRow(int j, int s)
{
   for (int i = 1; i < width - 1; i++)
   {
      hs[s, i] = copy[j, i - 1] + copy[j, i] + copy[j, i + 1];
   }
}

// the new row j, in place: n is the sum of the 9 cells around and including
// the cell; it lives if n == 3, or if it is alive and n == 4
void ruleRow(int j, int a, int c, int b)
{
   for (int i = 1; i < width - 1; i++)
   {
      uint8_t n = hs[a, i] + hs[c, i] + hs[b, i];
      copy[j, i] = (n == 3) | ((n == 4) & copy[j, i]);
   }
}

void step()
{
   int a = 0;
   int c = 1;
   int b = 2;
   sumRow(0, a);
   sumRow(1, c);
   for (int j = 1; j < height - 1; j++)
   {
      sumRow(j + 1, b); // the row below, before it changes
      ruleRow(j, a, c, b);
      int t = a;
      a = c;
      c = b;
      b = t;
   }
}

void draw(int o)
{
   uint16_t color = hsv2(o % 256, 255, 255);
   for (int j = 0; j < height; j++)
   {
      for (int i = 0; i < width; i++)
      {
         if (copy[j, i] == 1)
            led16[j * 800 + i] = color;
         else
            led16[j * 800 + i] = 0;
      }
   }
}

int main()
{
   setPixelsize(2);
   for (int h = 0; h < width * height; h++)
   {
      copy[h] = 0;
   }
   for (int h = 0; h < height; h++)
   {
      copy[h, width / 2] = 1;
   }
   uint32_t stepTime = 0;
   uint32_t drawTime = 0;
   for (int g = 1; g < 1000000; g++)
   {
      uint32_t t0 = micros();
      step();
      uint32_t t1 = micros();
      draw(g);
      uint32_t t2 = micros();
      show16();
      stepTime = stepTime + (t1 - t0);
      drawTime = drawTime + (t2 - t1);
      if (g % 50 == 0)
      {
         int sum = 0;
         for (int h = 0; h < width * height; h++)
         {
            sum = sum + copy[h] * (h % 97 + 1);
         }
         printfln("BENCH gen %d: step %d us, draw %d us, checksum %d", g, stepTime / 50, drawTime / 50, sum);
         stepTime = 0;
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
  bindFunction((char *)"void", (char *)"clear", NULL, (void *)clearscreen);
  bindFunction((char *)"void", (char *)"setPixelsize", (char *)"int", (void *)setPixelsize);
  bindFunction((char *)"uint32_t", (char *)"micros", NULL, (void *)scriptMicros);

  static ScriptExecutable exec = parseScript(gameOfLife);
  if (exec.isExeExists()) {
    printf("game of life: %u bytes of code, %u bytes of globals\n", (unsigned)exec.exe.binary_size,
           (unsigned)exec.exe.data_size);
    exec.execute("main");
  } else {
    showCompileError();
  }
}

void loop() {
  delay(1000);
}
