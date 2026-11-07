// Scrolling rainbow on the 800x480 RGB panel, drawn by an ESPLiveScript script
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

char rainbow[] = R"EOF(
uint16_t line[800];

int main()
{
   for (int t = 0; t < 1000000; t++)
   {
      for (int x = 0; x < width; x++)
      {
         line[x] = hsv2(((x * 256) / width + t) % 256, 255, 255);
      }
      for (int y = 0; y < height; y++)
      {
         for (int x = 0; x < width; x++)
         {
            led16[y * 800 + x] = line[x];
         }
      }
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

  static ScriptExecutable exec = parseScript(rainbow);
  if (exec.isExeExists()) {
    printf("rainbow: %u bytes of code\n", (unsigned)exec.exe.binary_size);
    exec.execute("main");
  } else {
    showCompileError();
  }
}

void loop() {
  delay(1000);
}
