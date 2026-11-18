// SporeHop: a little platformer made within the limits of a PICO-8 cart,
// on the Guition ESP32-4848S040 (a 4-inch 480x480 touch screen).
//
// The limits: a 128x128 screen, the 16 colours of the PICO-8 palette, 8x8
// sprites, a 3x5 font, six buttons (left, right, up, down, O, X), a game
// that steps 30 times a second. The script draws everything in those 16
// colours, with pset(), rectfill(), spr() and text() of its own; its
// sprites and rooms are drawn as text in tools/data.py, which writes the
// arrays pasted below.
//
// On the screen: pixel size 3 makes the panel 160x160 big pixels; the cart's
// 128x128 is the top, 384x384 on the panel, and the six buttons are under
// it, drawn by the script in the same colours. The sketch reads the touch
// screen (up to three fingers) and sets a bit per button held in touchHeld,
// which the script reads as the cart's btn() and btnp().
//
// The game: Pip, a little mushroom, gathers the glowing spores of each room;
// when they are all gathered, the door opens to the next one. Eight rooms,
// spikes and pits, springs, beetles (jump on them), five lives.
//   left, right   walk
//   O or up       jump (hold it to jump higher)
//   X             a puff of spores: the beetles it reaches are stunned
// O starts a game, and comes back to the title after the end.
//
// Needs the modified LovyanGFX Bus_RGB driver (double frame buffer with
// show(), _frame_buffer, pixel_size, offset). Guition4848S040.h is the panel
// configuration.
//
// Board: ESP32S3 Dev Module, PSRAM "OPI PSRAM", Flash Size 16MB.
#include "Guition4848S040.h"
#include "script_executable.h"
#include "binding.h"

LGFX lcd;
uint16_t *frame_buffer;
static uint32_t _sizex = 480;
static uint32_t _sizey = 480;

void show16() {
  lcd._bus_instance.show();
  frame_buffer = (uint16_t *)lcd._bus_instance._frame_buffer;
  lcd._panel_instance.resetWriteBuffer();
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

// --- the six buttons, where the script draws them (160x160, times 3) ---
// left, right, up, down, O, X: bit 0 to 5 of touchHeld
static const int16_t buttonX[6] = {2, 28, 54, 80, 108, 134};
#define BUTTON_Y 129
#define BUTTON_W 24
#define BUTTON_H 30

static volatile uint32_t touchHeld = 0;

static void touchTask(void *) {
  while (true) {
    lgfx::touch_point_t tp[3];
    int n = lcd.getTouchRaw(tp, 3);
    uint32_t held = 0;
    for (int k = 0; k < n; k++) {
      int x = tp[k].x / 3, y = tp[k].y / 3;
      // a touch a little outside a button still counts
      if (y < BUTTON_Y - 4)
        continue;
      for (int b = 0; b < 6; b++)
        if (x >= buttonX[b] - 1 && x < buttonX[b] + BUTTON_W + 1)
          held |= 1u << b;
    }
    touchHeld = held;
    vTaskDelay(pdMS_TO_TICKS(16));
  }
}

char sporeHop[] = R"EOF(
// the frame buffer's width in pixels of the panel; at pixel size 3 the
// picture is 160x160 and its rows are STRIDE apart
#define STRIDE 480
// the cart: 128x128, centred across, at the top; the buttons under it
#define GX 16
#define GY 0
#define PADY 128
// the game runs 30 steps a second
#define STEP_MS 33
// positions in sixteenths of a pixel
#define FP 16
#define GRAV 4
#define JUMP 58
#define HOP 20
#define SPRING 88
#define MAXFALL 64
#define ACC 3
#define MAXRUN 20
#define BEETLES 6
#define PARTS 24
// the buttons, a bit each in touchHeld (the order of the cart's btn())
#define B_LEFT 0
#define B_RIGHT 1
#define B_UP 2
#define B_DOWN 3
#define B_O 4
#define B_X 5

// sprites: 0 PIP0, 1 PIP1, 2 PIP2, 3 GRASS, 4 DIRT, 5 ROCK, 6 SPIKES, 7 SPORE0, 8 SPORE1, 9 DOOR0, 10 DOOR1, 11 SPRING, 12 BEETLE0, 13 BEETLE1, 14 STUNNED, 15 PUFF, 16 HEART
#define SPR_PIP0 0
#define SPR_PIP1 1
#define SPR_PIP2 2
#define SPR_GRASS 3
#define SPR_DIRT 4
#define SPR_ROCK 5
#define SPR_SPIKES 6
#define SPR_SPORE0 7
#define SPR_SPORE1 8
#define SPR_DOOR0 9
#define SPR_DOOR1 10
#define SPR_SPRING 11
#define SPR_BEETLE0 12
#define SPR_BEETLE1 13
#define SPR_STUNNED 14
#define SPR_PUFF 15
#define SPR_HEART 16
#define ROOMS 8
uint8_t sprites[1088] = {16, 16, 8, 8, 8, 8, 16, 16, 16, 8, 7, 8, 8, 7, 8, 16, 8, 8, 8, 8, 8, 8, 8, 8, 16, 15, 15, 15, 15, 15, 15, 16, 16, 15, 0, 15, 15, 0, 15, 16, 16, 15, 15, 15, 15, 15, 15, 16, 16, 16, 15, 16, 16, 15, 16, 16, 16, 16, 4, 16, 16, 4, 16, 16, 16, 16, 8, 8, 8, 8, 16, 16, 16, 8, 7, 8, 8, 7, 8, 16, 8, 8, 8, 8, 8, 8, 8, 8, 16, 15, 15, 15, 15, 15, 15, 16, 16, 15, 0, 15, 15, 0, 15, 16, 16, 15, 15, 15, 15, 15, 15, 16, 16, 15, 16, 16, 16, 16, 15, 16, 4, 16, 16, 16, 16, 16, 16, 4, 16, 16, 8, 8, 8, 8, 16, 16, 16, 8, 7, 8, 8, 7, 8, 16, 8, 8, 8, 8, 8, 8, 8, 8, 16, 15, 15, 15, 15, 15, 15, 16, 16, 15, 0, 15, 15, 0, 15, 16, 16, 15, 15, 15, 15, 15, 15, 16, 16, 15, 16, 15, 15, 16, 15, 16, 16, 4, 16, 16, 16, 16, 4, 16, 11, 3, 11, 11, 3, 11, 11, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 3, 4, 4, 4, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 4, 4, 4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 4, 4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 5, 13, 6, 6, 6, 6, 6, 6, 6, 13, 5, 5, 5, 5, 5, 5, 6, 13, 5, 5, 5, 5, 5, 5, 6, 13, 13, 13, 13, 13, 13, 13, 13, 6, 6, 6, 6, 13, 6, 6, 6, 5, 5, 5, 6, 13, 5, 5, 5, 5, 5, 5, 6, 13, 5, 5, 5, 13, 13, 13, 13, 13, 13, 13, 13, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 6, 16, 16, 16, 6, 16, 16, 16, 6, 16, 16, 16, 6, 16, 16, 6, 7, 6, 16, 6, 7, 6, 16, 6, 5, 6, 16, 6, 5, 6, 6, 5, 5, 5, 6, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 9, 9, 16, 16, 16, 16, 16, 9, 10, 10, 9, 16, 16, 16, 9, 10, 7, 7, 10, 9, 16, 16, 9, 10, 7, 7, 10, 9, 16, 16, 16, 9, 10, 10, 9, 16, 16, 16, 16, 16, 9, 9, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 9, 9, 16, 16, 16, 16, 16, 9, 10, 10, 9, 16, 16, 16, 16, 9, 10, 10, 9, 16, 16, 16, 16, 16, 9, 9, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 5, 5, 5, 5, 5, 5, 16, 5, 5, 13, 13, 13, 13, 5, 5, 5, 13, 6, 6, 6, 6, 13, 5, 5, 13, 6, 13, 13, 6, 13, 5, 5, 13, 6, 6, 10, 6, 13, 5, 5, 13, 6, 6, 6, 6, 13, 5, 5, 13, 6, 6, 6, 6, 13, 5, 5, 13, 6, 6, 6, 6, 13, 5, 16, 5, 5, 5, 5, 5, 5, 16, 5, 5, 13, 13, 13, 13, 5, 5, 5, 13, 1, 1, 1, 1, 13, 5, 5, 13, 1, 1, 1, 1, 13, 5, 5, 13, 1, 1, 12, 1, 13, 5, 5, 13, 1, 1, 1, 1, 13, 5, 5, 13, 1, 1, 1, 1, 13, 5, 5, 13, 1, 1, 1, 1, 13, 5, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 12, 12, 12, 12, 12, 12, 16, 16, 16, 6, 16, 16, 6, 16, 16, 16, 16, 16, 6, 6, 16, 16, 16, 16, 16, 6, 16, 16, 6, 16, 16, 16, 12, 12, 12, 12, 12, 12, 16, 16, 13, 13, 13, 13, 13, 13, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 1, 1, 1, 1, 16, 16, 16, 1, 2, 2, 2, 2, 1, 16, 1, 2, 14, 2, 2, 14, 2, 1, 1, 2, 2, 2, 2, 2, 2, 1, 16, 1, 1, 1, 1, 1, 1, 16, 16, 0, 16, 0, 16, 0, 16, 16, 0, 16, 0, 16, 0, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 1, 1, 1, 1, 16, 16, 16, 1, 2, 2, 2, 2, 1, 16, 1, 2, 14, 2, 2, 14, 2, 1, 1, 2, 2, 2, 2, 2, 2, 1, 16, 1, 1, 1, 1, 1, 1, 16, 16, 16, 0, 16, 0, 16, 0, 16, 16, 0, 16, 0, 16, 0, 16, 16, 16, 10, 16, 16, 16, 16, 10, 16, 16, 16, 1, 1, 1, 1, 16, 16, 16, 1, 13, 13, 13, 13, 1, 16, 1, 13, 0, 13, 13, 0, 13, 1, 1, 13, 13, 13, 13, 13, 13, 1, 16, 1, 1, 1, 1, 1, 1, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 7, 7, 16, 16, 16, 16, 16, 7, 6, 6, 7, 16, 7, 16, 7, 6, 6, 6, 6, 7, 16, 7, 16, 7, 6, 6, 7, 7, 6, 7, 16, 16, 7, 7, 6, 6, 6, 7, 16, 7, 16, 7, 6, 6, 6, 7, 16, 16, 7, 16, 7, 7, 7, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 8, 8, 16, 8, 8, 16, 16, 8, 8, 8, 8, 8, 8, 8, 16, 8, 8, 8, 8, 8, 8, 8, 16, 16, 8, 8, 8, 8, 8, 16, 16, 16, 16, 8, 8, 8, 16, 16, 16, 16, 16, 16, 8, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16};
uint8_t rooms[1920] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 5, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 5, 1, 1, 1, 1, 1, 3, 3, 3, 3, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 0, 0, 0, 2, 2, 2, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 8, 0, 4, 0, 0, 0, 0, 8, 0, 5, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 7, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 8, 0, 0, 0, 0, 3, 3, 0, 0, 0, 0, 8, 0, 5, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 3, 3, 3, 3, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 6, 3, 3, 3, 3, 3, 3, 3, 3, 6, 0, 0, 5, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 7, 0, 0, 0, 0, 8, 0, 0, 0, 0, 8, 0, 0, 0, 0, 5, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

// the 3x5 font: 0-9, A-Z, then / : and ! (5 rows of 3 bits, the top row first)
int font[39] = {31599, 11415, 29671, 29647, 23497, 31183, 31215, 29330, 31727, 31695,
                11245, 27566, 14627, 27502, 31143, 31140, 14699, 23533, 29847, 4714,
                23861, 18727, 24557, 27501, 11114, 27556, 11123, 27565, 14478, 29842,
                23407, 23402, 23549, 23213, 23186, 29351, 4772, 1040, 9346};

// the 16 colours, as the frame buffer wants them
uint16_t pal[16];

// the room being played
uint8_t map[240];
int room;
int spores;
int sporesLeft;
int doorX;
int doorY;

// Pip
int px;
int py;
int pvx;
int pvy;
int facing;
int onGround;
int coyote;
int walkTime;
int puffTime;
int cloudX;
int cloudY;
int cloudTime;

// the beetles
int bx[6];
int by[6];
int bdir[6];
int bon[6];
int bstun[6];

// sparks
int qx[24];
int qy[24];
int qvx[24];
int qvy[24];
int qlife[24];
int qcol[24];

int lives;
int score;
int state;          // 0 title, 1 playing, 2 dying, 3 room cleared, 4 game over, 5 the end
int stateTime;
int frame;
int playFrames;
uint32_t held;
uint32_t before;

// r, g, b (0-255) as the frame buffer wants them: RGB565, byte swapped
uint16_t pack(int r, int g, int b)
{
   int v = ((r & 248) << 8) | ((g & 252) << 3) | (b >> 3);
   return ((v & 255) << 8) | (v >> 8);
}

void makePalette()
{
   pal[0] = pack(0, 0, 0);
   pal[1] = pack(29, 43, 83);
   pal[2] = pack(126, 37, 83);
   pal[3] = pack(0, 135, 81);
   pal[4] = pack(171, 82, 54);
   pal[5] = pack(95, 87, 79);
   pal[6] = pack(194, 195, 199);
   pal[7] = pack(255, 241, 232);
   pal[8] = pack(255, 0, 77);
   pal[9] = pack(255, 163, 0);
   pal[10] = pack(255, 236, 39);
   pal[11] = pack(0, 228, 54);
   pal[12] = pack(41, 173, 255);
   pal[13] = pack(131, 118, 156);
   pal[14] = pack(255, 119, 168);
   pal[15] = pack(255, 204, 170);
}

// --- drawing, in the cart's 128x128 (pset, rectfill, spr, text) ---

void pset(int x, int y, int c)
{
   if (x >= 0 && x < 128 && y >= 0 && y < 128)
      led16[(y + GY) * STRIDE + x + GX] = pal[c];
}

void rectfill(int x, int y, int w, int h, int c)
{
   if (x < 0)
   {
      w = w + x;
      x = 0;
   }
   if (y < 0)
   {
      h = h + y;
      y = 0;
   }
   if (x + w > 128)
      w = 128 - x;
   if (y + h > 128)
      h = 128 - y;
   uint16_t col = pal[c];
   for (int j = 0; j < h; j++)
   {
      int row = (y + j + GY) * STRIDE + x + GX;
      for (int i = 0; i < w; i++)
         led16[row + i] = col;
   }
}

// a sprite, flipped left to right when flip is 1
void spr(int n, int x, int y, int flip)
{
   int base = n * 64;
   for (int j = 0; j < 8; j++)
   {
      int yy = y + j;
      if (yy >= 0 && yy < 128)
      {
         int row = (yy + GY) * STRIDE + GX;
         for (int i = 0; i < 8; i++)
         {
            int c = sprites[base + j * 8 + i];
            if (flip == 1)
               c = sprites[base + j * 8 + 7 - i];
            int xx = x + i;
            if (c < 16 && xx >= 0 && xx < 128)
               led16[row + xx] = pal[c];
         }
      }
   }
}

// a sprite at twice its size
void spr2(int n, int x, int y)
{
   int base = n * 64;
   for (int j = 0; j < 8; j++)
      for (int i = 0; i < 8; i++)
      {
         int c = sprites[base + j * 8 + i];
         if (c < 16)
            rectfill(x + i * 2, y + j * 2, 2, 2, c);
      }
}

void glyph(int x, int y, int g, int sc, int c)
{
   int bits = font[g];
   for (int r = 0; r < 5; r++)
      for (int b = 0; b < 3; b++)
         if ((bits >> ((4 - r) * 3 + 2 - b)) & 1)
            rectfill(x + b * sc, y + r * sc, sc, sc, c);
}

// capitals, digits, / : ! and spaces, 4 dots apart
void text(char *s, int x, int y, int sc, int c)
{
   int i = 0;
   while (s[i] != 0)
   {
      int ch = s[i];
      if (ch >= 48 && ch <= 57)
         glyph(x, y, ch - 48, sc, c);
      if (ch >= 65 && ch <= 90)
         glyph(x, y, ch - 55, sc, c);
      if (ch == 47)
         glyph(x, y, 36, sc, c);
      if (ch == 58)
         glyph(x, y, 37, sc, c);
      if (ch == 33)
         glyph(x, y, 38, sc, c);
      x = x + 4 * sc;
      i++;
   }
}

// with a dark copy under it, one dot down
void textShadow(char *s, int x, int y, int sc, int c)
{
   text(s, x, y + sc, sc, 0);
   text(s, x, y, sc, c);
}

int digits[8];
// a number; returns where it ends
int printNumber(int v, int x, int y, int minDigits, int c)
{
   int n = 0;
   while (v > 0 || n < minDigits)
   {
      digits[n] = v % 10;
      v = v / 10;
      n++;
   }
   for (int k = n - 1; k >= 0; k--)
   {
      glyph(x, y, digits[k], 1, c);
      x = x + 4;
   }
   return x;
}

// --- the buttons: held now, and pressed since the last step ---

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

// --- the room ---

int tileAt(int tx, int ty)
{
   if (tx < 0 || tx >= 16)
      return 1;            // the sides are walls
   if (ty < 0 || ty >= 15)
      return 0;            // open above, a fall below
   return map[ty * 16 + tx];
}

int solidAt(int tx, int ty)
{
   int t = tileAt(tx, ty);
   if (t == 1 || t == 2)
      return 1;
   return 0;
}

// does a box (in pixels, the room's top-left at 0,0) touch a wall?
int boxHits(int x, int y, int w, int h)
{
   int tx0 = x >> 3;
   int tx1 = (x + w - 1) >> 3;
   int ty0 = y >> 3;
   int ty1 = (y + h - 1) >> 3;
   if (x < 0)
      tx0 = -1;
   if (y < 0)
      ty0 = -1;
   for (int ty = ty0; ty <= ty1; ty++)
      for (int tx = tx0; tx <= tx1; tx++)
         if (solidAt(tx, ty) == 1)
            return 1;
   return 0;
}

void spark(int x, int y, int c, int n)
{
   for (int k = 0; k < PARTS && n > 0; k++)
      if (qlife[k] == 0)
      {
         qx[k] = x * FP;
         qy[k] = y * FP;
         qvx[k] = rand(33) - 16;
         qvy[k] = 0 - rand(28) - 4;
         qlife[k] = 12 + rand(12);
         qcol[k] = c;
         n--;
      }
}

void loadRoom(int r)
{
   room = r;
   spores = 0;
   sporesLeft = 0;
   for (int k = 0; k < BEETLES; k++)
      bon[k] = 0;
   for (int k = 0; k < PARTS; k++)
      qlife[k] = 0;
   int nb = 0;
   for (int i = 0; i < 240; i++)
   {
      int t = rooms[r * 240 + i];
      int tx = i % 16;
      int ty = i / 16;
      if (t == 7)
      {
         px = tx * 8 * FP;
         py = ty * 8 * FP;
         t = 0;
      }
      if (t == 8)
      {
         if (nb < BEETLES)
         {
            bx[nb] = tx * 8 * FP;
            by[nb] = ty * 8 * FP;
            bdir[nb] = 1;
            if (nb % 2 == 1)
               bdir[nb] = -1;
            bon[nb] = 1;
            bstun[nb] = 0;
            nb++;
         }
         t = 0;
      }
      if (t == 4)
         sporesLeft++;
      if (t == 5)
      {
         doorX = tx;
         doorY = ty;
      }
      map[i] = t;
   }
   pvx = 0;
   pvy = 0;
   facing = 0;
   onGround = 0;
   coyote = 0;
   puffTime = 0;
   cloudTime = 0;
}

void newGame()
{
   lives = 5;
   score = 0;
   playFrames = 0;
   loadRoom(0);
   state = 1;
   stateTime = 0;
}

void die()
{
   if (state != 1)
      return;
   spark((px / FP) + 4, (py / FP) + 4, 8, 10);
   spark((px / FP) + 4, (py / FP) + 4, 15, 8);
   lives--;
   state = 2;
   stateTime = 0;
}

// --- one step of the game ---

void movePip()
{
   // across: speed up, slow down, walls stop
   int dir = 0;
   if (btn(B_LEFT) == 1)
   {
      dir = -1;
      facing = 1;
   }
   if (btn(B_RIGHT) == 1)
   {
      dir = 1;
      facing = 0;
   }
   if (dir != 0)
   {
      pvx = pvx + dir * ACC;
      if (pvx > MAXRUN)
         pvx = MAXRUN;
      if (pvx < 0 - MAXRUN)
         pvx = 0 - MAXRUN;
      walkTime++;
   }
   else
   {
      if (pvx > 0)
      {
         pvx = pvx - 2;
         if (pvx < 0)
            pvx = 0;
      }
      if (pvx < 0)
      {
         pvx = pvx + 2;
         if (pvx > 0)
            pvx = 0;
      }
      walkTime = 0;
   }
   int nx = px + pvx;
   if (boxHits((nx / FP) + 1, (py / FP) + 1, 6, 7) == 0)
      px = nx;
   else
   {
      int step = 1;
      if (pvx < 0)
         step = -1;
      int x = px / FP;
      while (boxHits(x + step + 1, (py / FP) + 1, 6, 7) == 0)
         x = x + step;
      px = x * FP;
      pvx = 0;
   }

   // jumping: O or up, a few steps after leaving an edge still count
   if (onGround == 1)
      coyote = 5;
   else if (coyote > 0)
      coyote--;
   if ((btnp(B_O) == 1 || btnp(B_UP) == 1) && coyote > 0)
   {
      pvy = 0 - JUMP;
      coyote = 0;
      spark((px / FP) + 4, (py / FP) + 8, 6, 3);
   }
   // let go early: a shorter hop
   if (pvy < 0 - HOP && btn(B_O) == 0 && btn(B_UP) == 0)
      pvy = 0 - HOP;

   // down: gravity, floors and ceilings stop
   pvy = pvy + GRAV;
   if (pvy > MAXFALL)
      pvy = MAXFALL;
   int ny = py + pvy;
   onGround = 0;
   if (boxHits((px / FP) + 1, (ny / FP) + 1, 6, 7) == 0)
      py = ny;
   else
   {
      int step = 1;
      if (pvy < 0)
         step = -1;
      int y = py / FP;
      while (boxHits((px / FP) + 1, y + step + 1, 6, 7) == 0)
         y = y + step;
      py = y * FP;
      if (pvy > 0)
         onGround = 1;
      pvy = 0;
   }

   // what Pip touches: spores, spikes, springs, the door
   int cx = (px / FP) + 4;
   int cy = (py / FP) + 4;
   int t = tileAt(cx >> 3, cy >> 3);
   if (t == 4)
   {
      map[(cy >> 3) * 16 + (cx >> 3)] = 0;
      spores++;
      score = score + 10;
      spark(cx, cy, 10, 6);
   }
   int feet = (py / FP) + 7;
   int tf = tileAt(cx >> 3, feet >> 3);
   if (tf == 3 && (feet & 7) >= 3)
      die();
   if (tf == 6 && pvy >= 0)
   {
      pvy = 0 - SPRING;
      spark(cx, feet, 12, 5);
   }
   if (t == 5 && spores == sporesLeft)
   {
      score = score + 50;
      state = 3;
      stateTime = 0;
   }
   if ((py / FP) > 15 * 8)
      die();

   // X: a puff of spores that stuns the beetles it reaches
   if (puffTime > 0)
      puffTime--;
   if (btnp(B_X) == 1 && puffTime == 0)
   {
      cloudX = (px / FP) + 8;
      if (facing == 1)
         cloudX = (px / FP) - 8;
      cloudY = py / FP;
      cloudTime = 18;
      puffTime = 30;
   }
   if (cloudTime > 0)
      cloudTime--;
}

void moveBeetles()
{
   for (int k = 0; k < BEETLES; k++)
   {
      if (bon[k] == 0)
         continue;
      int x = bx[k] / FP;
      int y = by[k] / FP;
      if (cloudTime > 0 && x + 7 > cloudX && x < cloudX + 8 && y + 7 > cloudY && y < cloudY + 8)
      {
         if (bstun[k] == 0)
            spark(x + 4, y + 2, 10, 3);
         bstun[k] = 120;
      }
      if (bstun[k] > 0)
         bstun[k] = bstun[k] - 1;
      else
      {
         // walk; turn at a wall or at the edge of the floor
         int ahead = x + 8;
         if (bdir[k] < 0)
            ahead = x - 1;
         int floorTile = solidAt(ahead >> 3, (y + 8) >> 3);
         if (solidAt(ahead >> 3, (y + 4) >> 3) == 1 || floorTile == 0 || tileAt(ahead >> 3, (y + 4) >> 3) == 3)
            bdir[k] = 0 - bdir[k];
         else
            bx[k] = bx[k] + bdir[k] * 8;
      }
      // Pip on top squashes it; from the side it hurts (not while stunned)
      int ppx = px / FP;
      int ppy = py / FP;
      if (ppx + 7 > x + 1 && ppx < x + 7 && ppy + 8 > y + 2 && ppy < y + 7)
      {
         if (pvy > 0 && ppy + 8 < y + 7)
         {
            bon[k] = 0;
            pvy = 0 - 40;
            score = score + 25;
            spark(x + 4, y + 4, 2, 6);
            spark(x + 4, y + 4, 14, 4);
         }
         else if (bstun[k] == 0)
            die();
      }
   }
}

void moveSparks()
{
   for (int k = 0; k < PARTS; k++)
      if (qlife[k] > 0)
      {
         qx[k] = qx[k] + qvx[k];
         qy[k] = qy[k] + qvy[k];
         qvy[k] = qvy[k] + 3;
         qlife[k] = qlife[k] - 1;
      }
}

void update()
{
   frame++;
   if (state == 0)
   {
      if (btnp(B_O) == 1 || btnp(B_X) == 1)
         newGame();
      return;
   }
   if (state == 1)
   {
      playFrames++;
      movePip();
      if (state == 1)
         moveBeetles();
   }
   moveSparks();
   if (state == 2)
   {
      stateTime++;
      if (stateTime > 45)
      {
         if (lives <= 0)
         {
            state = 4;
            stateTime = 0;
         }
         else
         {
            loadRoom(room);
            state = 1;
         }
      }
   }
   if (state == 3)
   {
      stateTime++;
      if (stateTime > 40)
      {
         if (room + 1 >= ROOMS)
         {
            state = 5;
            stateTime = 0;
         }
         else
         {
            loadRoom(room + 1);
            state = 1;
         }
      }
   }
   if (state == 4 || state == 5)
   {
      stateTime++;
      if (stateTime > 30 && (btnp(B_O) == 1 || btnp(B_X) == 1))
      {
         state = 0;
         stateTime = 0;
      }
   }
}

// --- drawing a frame ---

void drawRoom()
{
   rectfill(0, 8, 128, 120, 1);
   // a few far-off stars, the same for each room
   for (int k = 0; k < 14; k++)
   {
      int sx = (k * 37 + room * 23) % 128;
      int sy = 10 + (k * 53 + room * 11) % 60;
      int c = 13;
      if (k % 4 == 0)
         c = 6;
      pset(sx, sy, c);
   }
   for (int ty = 0; ty < 15; ty++)
      for (int tx = 0; tx < 16; tx++)
      {
         int t = map[ty * 16 + tx];
         int x = tx * 8;
         int y = 8 + ty * 8;
         if (t == 1)
         {
            if (tileAt(tx, ty - 1) == 1)
               spr(SPR_DIRT, x, y, 0);
            else
               spr(SPR_GRASS, x, y, 0);
         }
         if (t == 2)
            spr(SPR_ROCK, x, y, 0);
         if (t == 3)
            spr(SPR_SPIKES, x, y, 0);
         if (t == 4)
         {
            if ((frame / 8 + tx) % 2 == 0)
               spr(SPR_SPORE0, x, y, 0);
            else
               spr(SPR_SPORE1, x, y, 0);
         }
         if (t == 5)
         {
            if (spores == sporesLeft)
               spr(SPR_DOOR1, x, y, 0);
            else
               spr(SPR_DOOR0, x, y, 0);
         }
         if (t == 6)
            spr(SPR_SPRING, x, y, 0);
      }
}

void drawThings()
{
   for (int k = 0; k < BEETLES; k++)
      if (bon[k] == 1)
      {
         int s = SPR_BEETLE0;
         if ((frame / 6) % 2 == 1)
            s = SPR_BEETLE1;
         if (bstun[k] > 0)
            s = SPR_STUNNED;
         int flip = 0;
         if (bdir[k] < 0)
            flip = 1;
         spr(s, bx[k] / FP, 8 + by[k] / FP, flip);
      }
   if (cloudTime > 0)
      spr(SPR_PUFF, cloudX, 8 + cloudY, (frame / 3) % 2);
   if (state == 1 || state == 3)
   {
      int s = SPR_PIP0;
      if (onGround == 0)
         s = SPR_PIP2;
      else if (walkTime > 0 && (walkTime / 4) % 2 == 1)
         s = SPR_PIP1;
      spr(s, px / FP, 8 + py / FP, facing);
   }
   for (int k = 0; k < PARTS; k++)
      if (qlife[k] > 0)
         pset(qx[k] / FP, 8 + qy[k] / FP, qcol[k]);
}

void drawHud()
{
   rectfill(0, 0, 128, 8, 0);
   text("ROOM", 1, 1, 1, 6);
   printNumber(room + 1, 19, 1, 1, 7);
   spr(SPR_SPORE1, 30, 0, 0);
   int x = printNumber(spores, 38, 1, 1, 10);
   text("/", x, 1, 1, 5);
   printNumber(sporesLeft, x + 4, 1, 1, 10);
   printNumber(score, 64, 1, 5, 7);
   for (int k = 0; k < lives && k < 5; k++)
      spr(SPR_HEART, 92 + k * 7, 0, 0);
}

void drawTitle()
{
   rectfill(0, 0, 128, 128, 1);
   for (int k = 0; k < 30; k++)
      pset((k * 37) % 128, (k * 53 + frame / 4) % 128, 13);
   rectfill(0, 96, 128, 32, 3);
   for (int x = 0; x < 128; x = x + 8)
      spr(SPR_GRASS, x, 96, 0);
   textShadow("SPORE", 24, 18, 4, 8);
   textShadow("HOP", 40, 42, 4, 10);
   // Pip hops on the grass
   int hop = (frame % 30) - 15;
   int h = (225 - hop * hop) / 12;
   spr2(SPR_PIP2, 56, 80 - h);
   spr(SPR_SPORE0, 30, 86, 0);
   spr(SPR_SPORE0, 90, 86, 0);
   if ((frame / 15) % 2 == 0)
      textShadow("PRESS O TO START", 32, 108, 1, 7);
   text("A 16 COLOUR CART", 32, 118, 1, 13);
}

void drawEnd()
{
   rectfill(16, 40, 96, 48, 0);
   rectfill(17, 41, 94, 46, 1);
   if (state == 4)
      textShadow("GAME OVER", 29, 48, 2, 8);
   else
   {
      textShadow("WELL DONE!", 25, 46, 2, 11);
      int secs = playFrames / 30;
      int x = printNumber(secs / 60, 46, 62, 1, 7);
      text(":", x, 62, 1, 6);
      printNumber(secs % 60, x + 4, 62, 2, 7);
   }
   text("SCORE", 34, 72, 1, 6);
   printNumber(score, 58, 72, 5, 10);
   if (stateTime > 30 && (frame / 15) % 2 == 0)
      text("PRESS O", 50, 80, 1, 7);
}

// outside the cart: a dark case, the buttons under it (these draw in the
// whole 160x160 picture, so they write led16 directly)
void caseFill(int x, int y, int w, int h, int c)
{
   uint16_t col = pal[c];
   for (int j = 0; j < h; j++)
   {
      int row = (y + j) * STRIDE + x;
      for (int i = 0; i < w; i++)
         led16[row + i] = col;
   }
}

// a touch button, lit while held: an arrow, or O and X as on the cart's pad
void drawButton(int x, int y, int w, int h, int b, int icon)
{
   int edge = 13;
   int face = 1;
   if (btn(b) == 1)
   {
      edge = 7;
      face = 12;
   }
   caseFill(x + 1, y, w - 2, h, edge);
   caseFill(x, y + 1, w, h - 2, edge);
   caseFill(x + 1, y + 1, w - 2, h - 2, face);
   int cx = x + w / 2;
   int cy = y + h / 2;
   for (int i = 0; i < 6; i++)
   {
      if (icon == 0)
         caseFill(cx - 3 + i, cy - i, 1, 2 * i + 1, 7);
      if (icon == 1)
         caseFill(cx + 3 - i, cy - i, 1, 2 * i + 1, 7);
      if (icon == 2)
         caseFill(cx - i, cy - 3 + i, 2 * i + 1, 1, 7);
      if (icon == 3)
         caseFill(cx - i, cy + 3 - i, 2 * i + 1, 1, 7);
   }
   if (icon == 4)
   {
      caseFill(cx - 3, cy - 5, 7, 2, 7);
      caseFill(cx - 3, cy + 4, 7, 2, 7);
      caseFill(cx - 5, cy - 3, 2, 7, 7);
      caseFill(cx + 4, cy - 3, 2, 7, 7);
   }
   if (icon == 5)
      for (int i = 0; i < 9; i++)
      {
         caseFill(cx - 4 + i, cy - 4 + i, 2, 2, 7);
         caseFill(cx + 3 - i, cy - 4 + i, 2, 2, 7);
      }
}

void drawCase()
{
   caseFill(0, 0, GX, 128, 0);
   caseFill(GX + 128, 0, 160 - GX - 128, 128, 0);
   caseFill(0, PADY, 160, 160 - PADY, 0);
}

// the six buttons (the sketch tests the touches against the same places)
void drawPad()
{
   drawButton(2, PADY + 1, 24, 30, B_LEFT, 0);
   drawButton(28, PADY + 1, 24, 30, B_RIGHT, 1);
   drawButton(54, PADY + 1, 24, 30, B_UP, 2);
   drawButton(80, PADY + 1, 24, 30, B_DOWN, 3);
   drawButton(108, PADY + 1, 24, 30, B_O, 4);
   drawButton(134, PADY + 1, 24, 30, B_X, 5);
}

void draw()
{
   drawCase();
   if (state == 0)
      drawTitle();
   else
   {
      drawRoom();
      drawThings();
      drawHud();
      if (state == 3)
         textShadow("CLEARED!", 34, 56, 2, 11);
      if (state == 4 || state == 5)
         drawEnd();
   }
}

int main()
{
   setPixelsize(3);           // 160x160, each 3x3 pixels
   makePalette();
   state = 0;
   held = 0;
   before = 0;
   uint32_t last = millis();
   while (true)
   {
      // fixed steps, as the cart's _update at 30 a second
      while (millis() - last >= STEP_MS)
      {
         last = last + STEP_MS;
         before = held;
         held = touchHeld;
         update();
      }
      draw();
      drawPad();
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

  uint32_t t0 = millis();
  static ScriptExecutable exec = parseScript(sporeHop);
  if (!exec.isExeExists()) {
    showCompileError();
    return;
  }
  printf("sporehop: %u bytes of code, compiled in %u ms\n", (unsigned)exec.exe.binary_size, (unsigned)(millis() - t0));

  // the touch screen, read on core 0
  xTaskCreatePinnedToCore(touchTask, "touch", 4096, NULL, 3, NULL, 0);

  exec.execute("main");
}

void loop() {
  delay(1000);
}
