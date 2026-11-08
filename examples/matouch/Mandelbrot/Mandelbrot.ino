// A Mandelbrot zoom on the 800x480 RGB panel, drawn by an ESPLiveScript
// script straight into the frame buffer (led16), one frame per show16().
// The script zooms towards a point on the edge of the set and starts over
// after 300 frames. setPixelsize(1), in the script, draws every pixel of
// the 800x480 screen; 2, 3 or 4 make bigger cells, and faster frames.
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

// HSV -> RGB565, byte swapped like LovyanGFX's frame buffer
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
  if (++frames % 20 == 0) {
    uint32_t t = millis();
    printf("%.2f fps\n", 20000.0f / (t - t0));
    t0 = t;
  }
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

char mandelbrot[] = R"EOF(
#define MAXITER 64

int main()
{
   setPixelsize(1);   // one pixel per cell: the full 800x480 (2, 3, 4: bigger cells, faster)
   printfln("width:%d height:%d", width, height);
   while (true)
   {
      float scale = 3.0 / width;          // the width of a cell in the plane
      for (int frame = 0; frame < 300; frame++)
      {
         float left = -0.743643887 - scale * width / 2;
         float top = 0.131825904 - scale * height / 2;
         for (int y = 0; y < height; y++)
         {
            float ci = top + y * scale;
            for (int x = 0; x < width; x++)
            {
               float cr = left + x * scale;
               float zr = 0.0;
               float zi = 0.0;
               float zr2 = 0.0;
               float zi2 = 0.0;
               int iter = 0;
               while (iter < MAXITER && zr2 + zi2 <= 4.0)
               {
                  zi = 2.0 * zr * zi + ci;
                  zr = zr2 - zi2 + cr;
                  zr2 = zr * zr;
                  zi2 = zi * zi;
                  iter++;
               }
               if (iter == MAXITER)
                  led16[y * 800 + x] = 0;
               else
                  led16[y * 800 + x] = hsv2((iter * 8 + frame) % 256, 255, 255);
            }
         }
         show16();
         scale = scale * 0.96;             // zoom in by 4% each frame
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
  bindFunction((char *)"void", (char *)"setPixelsize", (char *)"int", (void *)setPixelsize);

  static ScriptExecutable exec = parseScript(mandelbrot);
  if (exec.isExeExists()) {
    printf("mandelbrot: %u bytes of code\n", (unsigned)exec.exe.binary_size);
    exec.execute("main");
  } else {
    showCompileError();
  }
}

void loop() {
  delay(1000);
}
