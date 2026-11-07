// Bouncing balls on the 800x480 RGB panel, drawn by an ESPLiveScript script
// straight into the frame buffer (led16), one frame per show16().
//
// Needs the modified LovyanGFX Bus_RGB driver (double frame buffer with show(),
// _frame_buffer, pixel_size, offset) and FastLED (CHSV). Matouch7.h is the panel
// configuration (Waveshare ESP32-S3-Touch-LCD 800x480 pinout).
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

int scriptRand(uint32_t s) {
  return rand() % (s + 1);
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

char script[] = R"EOF(
#define max_nb_balls 20
#define rmax 30
#define rmin 12
#define W 800
#define H 480
#define true 1

struct ball
{
   float vx, vy, xc, yc, r;
   // positions in the two previous frames: the buffer drawn into still holds
   // the frame from two show16() ago, the ball is erased where it was then
   float ox1, oy1, ox2, oy2;
   uint16_t color;
   void drawBall(float cx, float cy, uint16_t c)
   {
      int startx = cx - r;
      int r2 = r * r;
      int starty = cy - r;
      int _xc = cx;
      int _yc = cy;
      for (int i = startx; i <= _xc; i++)
         for (int j = starty; j <= _yc; j++)
         {
            int distance = (i - cx) ^ 2 + (j - cy) ^ 2;
            if (distance <= r2)
            {
               int i2 = 2 * _xc - i;
               int j2 = 2 * _yc - j;
               led16[j * W + i] = c;
               led16[j * W + i2] = c;
               led16[j2 * W + i] = c;
               led16[j2 * W + i2] = c;
            }
         }
   }
   void eraseBall()
   {
      drawBall(ox2, oy2, 0);
   }
   void updateBall()
   {
      xc += vx;
      yc += vy;
      if (xc >= (W - r - 1))
      {
         xc = W - r - 1;
         vx = -vx;
      }
      if (xc < (r + 1))
      {
         xc = r + 1;
         vx = -vx;
      }
      if (yc >= H - r - 1)
      {
         yc = H - r - 1;
         vy = -vy;
      }
      if (yc < r + 1)
      {
         yc = r + 1;
         vy = -vy;
      }
      ox2 = ox1;
      oy2 = oy1;
      ox1 = xc;
      oy1 = yc;
      drawBall(xc, yc, color);
   }
   ball()
   {
      vx = (rand(300) / 255 + 0.7) * 4;
      vy = (rand(280) / 255 + 0.5) * 4;
      r = (rmax - rmin) * (rand(280) / 180) + rmin;
      xc = W / 2 * (rand(280) / 255 + 0.3) + 15;
      yc = H / 2 * (rand(280) / 255 + 0.3) + 15;
      color = hsv2(rand(255), 255, 255);
      ox1 = xc;
      oy1 = yc;
      ox2 = xc;
      oy2 = yc;
   }
};

ball Balls[max_nb_balls];

void main()
{
   int num = max_nb_balls;
   printfln("number of balls:%d", num);
   while (true)
   {
      // erase all the balls first, then draw them all: no full screen clear,
      // a 768 KB memset per frame starves the panel's line copy from PSRAM
      for (int i = 0; i < num; i++)
         Balls[i].eraseBall();
      for (int i = 0; i < num; i++)
         Balls[i].updateBall();
      show16();
   }
}
)EOF";

// lcd.init() on core 0: Bus_RGB's line copy interrupts are allocated on the core that runs it
static void lcdInitTask(void *done) {
  lcd.init();
  xTaskNotifyGive((TaskHandle_t)done);
  vTaskDelete(NULL);
}

// the script runs on core 1, away from the display interrupts
static void scriptTask(void *exec) {
  ((ScriptExecutable *)exec)->execute("main");
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
  bindFunction((char *)"uint32_t", (char *)"rand", (char *)"uint32_t", (void *)scriptRand);

  static ScriptExecutable exec = parseScript(script);
  if (exec.isExeExists()) {
    printf("bouncing balls: %u bytes of code\n", (unsigned)exec.exe.binary_size);
    xTaskCreatePinnedToCore(scriptTask, "script", 8192, &exec, 1, NULL, 1);
  } else {
    showCompileError();
  }
}

void loop() {
  delay(1000);
}
