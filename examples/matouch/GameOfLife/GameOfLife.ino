// Game of Life on the 800x480 RGB panel, drawn by an ESPLiveScript script
// straight into the frame buffer (led16), one generation per show16().
// 800x480 cells, one pixel each (setPixelsize(1)). setPixelsize(2..5) gives bigger
// cells: the frame buffer keeps its 800 pixel stride, only its top-left part is shown.
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
uint8_t copy[480,800];
uint8_t line1[800];
uint8_t linec[800];

void create_leds(int o)
{
   uint16_t color = hsv2(o % 256, 255, 255);
   for (int j = 1; j < height - 1; j++)
   {
      for (int h = 0; h < width; h++)
      {
         line1[h] = linec[h];
         linec[h] = copy[j,h];
      }
      for (int i = 1; i < width - 1; i++)
      {
         int res = linec[i - 1] + linec[i + 1];
         res = res + line1[i - 1] + line1[i + 1] + line1[i];
         res = res + copy[j+1,i-1] + copy[j+1,i+1] + copy[j+1,i];
         bool alive = false;
         int l = copy[j,i];
         if (res == 3)
            alive = true;
         else
            alive = (l == 1 && res == 2) ? true : false;
         if (alive)
         {
            led16[j*800+i] = color;
            copy[j,i] = 1;
         }
         else
         {
            led16[j*800+i] = 0;
            copy[j,i] = 0;
         }
      }
   }
}

int main()
{
   setPixelsize(2);
   printfln("width:%d height:%d", width, height);
   while (true)
   {
      for (int h = 0; h < 480 * 800; h++)
      {
         copy[h] = 0;
      }
      for (int h = 0; h < height; h++)
      {
         copy[h,width/2] = 1;
      }
      clear();
      show16();
      for (int i = 0; i < 500; i++)
      {
         create_leds(i);
         show16();
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
