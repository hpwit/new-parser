// The touch controls of the guiton games, on the Guition ESP32-4848S040.
//
// The games were written for 400x240 (an 800x480 panel at pixel size 2).
// Here they run at pixel size 1: the game is shown 1:1 in the top half of the
// 480x480 screen (400x240, centred: GAME_X empty columns on each side), and
// the bottom half (PAD_Y to 479) is a touch pad: big buttons, drawn by the
// sketch, that never cover the game.
//
// A sketch includes this file, then defines its buttons in padButtons[]
// (coordinates inside the pad, 480x240) and their number in padButtonCount. The buttons send the keys the game
// already knows from the USB keyboard, into the same queue readKey() reads:
//   PAD_TAP        the key, once per touch
//   PAD_REPEAT     the key, then again while held (as a keyboard repeats)
//   PAD_HOLD       the key, then key + 128 when let go (the ship moves while
//                  a direction is held, as with the keyboard's release hook)
//   PAD_HOLD_STOP  the key, then 's' when no PAD_HOLD_STOP button is held any
//                  more (PrismBreak's paddle stops when the finger lifts)
// Up to three fingers count, so a direction and a button can be held at once.
//
// The sketch calls padShow() in show16(), before the frame is shown: it
// redraws the pad into the frame buffer when what is held changed since that
// buffer last showed it (there are two buffers), and clears the side columns
// (a game may draw a little past its 400 columns, unseen on the 800x480 panel)
// unless PAD_SIDES is 0.
// Needs lcd (the LGFX) and frame_buffer defined before it is included.
#pragma once

// a sketch can change these before including the file (Vault13H scales its
// view to 480x360 and keeps 120 rows for the pad, with PAD_SIDES 0)
#ifndef GAME_X
#define GAME_X 40
#endif
#ifndef PAD_Y
#define PAD_Y 240
#endif
#ifndef PAD_SIDES
#define PAD_SIDES 1       // clear the columns left and right of the game
#endif
#define PAD_W 480
#define PAD_H (480 - PAD_Y)

enum { PAD_TAP, PAD_REPEAT, PAD_HOLD, PAD_HOLD_STOP };

struct PadButton {
  int16_t x, y, w, h;   // in the pad: 0..479, 0..239
  uint8_t key;
  uint8_t mode;
  const char *label;    // a word, or an arrow: "<" ">" "^" "v"
};

// the sketch's buttons
extern const PadButton padButtons[];
extern const int padButtonCount;

#define PAD_REPEAT_DELAY 150
#define PAD_REPEAT_EVERY 40
#define PAD_MAX_BUTTONS 16

// the keys, from the touch task to the script's
#define KEY_QUEUE 32
static volatile uint8_t keyQueue[KEY_QUEUE];
static volatile uint8_t keyHead = 0, keyTail = 0;

static void pushKey(uint8_t c) {
  uint8_t next = (keyHead + 1) % KEY_QUEUE;
  if (next != keyTail) {  // full: the key is dropped
    keyQueue[keyHead] = c;
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

// the buttons held, a bit each
static volatile uint32_t padHeld = 0;

static int padButtonAt(int x, int y) {
  for (int i = 0; i < padButtonCount; i++) {
    const PadButton &b = padButtons[i];
    if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h)
      return i;
  }
  return -1;
}

static void padTouchTask(void *) {
  uint32_t held = 0;
  uint32_t nextRepeat[PAD_MAX_BUTTONS] = {0};
  while (true) {
    lgfx::touch_point_t tp[3];
    int n = lcd.getTouchRaw(tp, 3);
    uint32_t now = 0;
    for (int k = 0; k < n; k++) {
      int i = padButtonAt(tp[k].x, tp[k].y - PAD_Y);
      if (i >= 0)
        now |= 1u << i;
    }
    uint32_t t = millis();
    for (int i = 0; i < padButtonCount; i++) {
      const PadButton &b = padButtons[i];
      uint32_t bit = 1u << i;
      if ((now & bit) && !(held & bit)) {
        pushKey(b.key);
        nextRepeat[i] = t + PAD_REPEAT_DELAY;
      } else if ((now & bit) && b.mode == PAD_REPEAT && (int32_t)(t - nextRepeat[i]) >= 0) {
        pushKey(b.key);
        nextRepeat[i] = t + PAD_REPEAT_EVERY;
      } else if (!(now & bit) && (held & bit)) {
        if (b.mode == PAD_HOLD)
          pushKey(b.key | 0x80);
        if (b.mode == PAD_HOLD_STOP) {
          bool other = false;
          for (int j = 0; j < padButtonCount; j++)
            if (j != i && (now & (1u << j)) && padButtons[j].mode == PAD_HOLD_STOP)
              other = true;
          if (!other)
            pushKey('s');
        }
      }
    }
    held = now;
    padHeld = held;
    vTaskDelay(pdMS_TO_TICKS(16));
  }
}

// --- drawing the pad ---

// r, g, b (0-255) as the frame buffer wants them: RGB565, byte swapped
static uint16_t padColor(int r, int g, int b) {
  uint16_t v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  return (v << 8) | (v >> 8);
}

static void padFill(uint16_t *fb, int x, int y, int w, int h, uint16_t c) {
  for (int j = y; j < y + h; j++) {
    uint16_t *row = fb + (PAD_Y + j) * 480;
    for (int i = x; i < x + w; i++)
      row[i] = c;
  }
}

// the games' 8x8 font, 0-9 then A-Z (ChromaQuest/tools/font8.py)
static const uint8_t font8[288] = {126, 199, 207, 219, 243, 227, 126, 0, 24, 56, 120, 24, 24, 24, 126, 0,
                                    126, 195, 6, 28, 112, 192, 255, 0, 126, 195, 3, 62, 3, 195, 126, 0,
                                    14, 30, 54, 102, 255, 6, 6, 0, 255, 192, 254, 3, 3, 195, 126, 0,
                                    62, 96, 192, 254, 195, 195, 126, 0, 255, 195, 6, 12, 24, 24, 24, 0,
                                    126, 195, 195, 126, 195, 195, 126, 0, 126, 195, 195, 127, 3, 6, 124, 0,
                                    60, 102, 195, 195, 255, 195, 195, 0, 254, 195, 195, 254, 195, 195, 254, 0,
                                    126, 195, 192, 192, 192, 195, 126, 0, 252, 198, 195, 195, 195, 198, 252, 0,
                                    255, 192, 192, 252, 192, 192, 255, 0, 255, 192, 192, 252, 192, 192, 192, 0,
                                    126, 195, 192, 207, 195, 195, 126, 0, 195, 195, 195, 255, 195, 195, 195, 0,
                                    126, 24, 24, 24, 24, 24, 126, 0, 63, 6, 6, 6, 198, 198, 124, 0,
                                    198, 204, 216, 240, 216, 204, 198, 0, 192, 192, 192, 192, 192, 192, 255, 0,
                                    195, 231, 255, 219, 195, 195, 195, 0, 195, 227, 243, 219, 207, 199, 195, 0,
                                    126, 195, 195, 195, 195, 195, 126, 0, 254, 195, 195, 254, 192, 192, 192, 0,
                                    126, 195, 195, 195, 203, 198, 123, 0, 254, 195, 195, 254, 204, 198, 195, 0,
                                    126, 195, 192, 126, 3, 195, 126, 0, 255, 24, 24, 24, 24, 24, 24, 0,
                                    195, 195, 195, 195, 195, 195, 126, 0, 195, 195, 195, 195, 102, 60, 24, 0,
                                    195, 195, 195, 219, 255, 231, 195, 0, 195, 102, 60, 24, 60, 102, 195, 0,
                                    195, 195, 102, 60, 24, 24, 24, 0, 255, 6, 12, 24, 48, 96, 255, 0};

// a word, each row of a letter a shade of c, over a dark shadow
static void padText(uint16_t *fb, int x, int y, const char *s, int sc, int r0, int g0, int b0) {
  static const uint8_t rowMul[8] = {16, 16, 16, 7, 16, 15, 12, 9};
  static const uint8_t rowLift[8] = {10, 6, 2, 0, 5, 0, 0, 0};
  for (; *s; s++, x += 9 * sc) {
    int g = -1;
    if (*s >= '0' && *s <= '9')
      g = *s - '0';
    if (*s >= 'A' && *s <= 'Z')
      g = *s - 'A' + 10;
    if (g < 0)
      continue;
    for (int pass = 0; pass < 2; pass++)
      for (int r = 0; r < 8; r++) {
        uint16_t c = padColor(10, 8, 20);
        if (pass == 1) {
          int rr = r0 * rowMul[r] >> 4, gg = g0 * rowMul[r] >> 4, bb = b0 * rowMul[r] >> 4;
          c = padColor(rr + ((255 - rr) * rowLift[r] >> 4), gg + ((255 - gg) * rowLift[r] >> 4),
                       bb + ((255 - bb) * rowLift[r] >> 4));
        }
        int off = pass == 0 ? sc : 0;
        for (int b = 0; b < 8; b++)
          if ((font8[g * 8 + r] >> (7 - b)) & 1)
            padFill(fb, x + b * sc + off, y + r * sc + off, sc, sc, c);
      }
  }
}

// an arrow, pointing: '<' '>' '^' 'v', its middle at (cx, cy)
static void padArrow(uint16_t *fb, int cx, int cy, char dir, int size, uint16_t c) {
  for (int i = 0; i < size; i++) {
    int len = 2 * i + 1;
    if (dir == '<')
      padFill(fb, cx - size / 2 + i, cy - i, 1, len, c);
    if (dir == '>')
      padFill(fb, cx + size / 2 - i, cy - i, 1, len, c);
    if (dir == '^')
      padFill(fb, cx - i, cy - size / 2 + i, len, 1, c);
    if (dir == 'v')
      padFill(fb, cx - i, cy + size / 2 - i, len, 1, c);
  }
}

static void drawPad(uint16_t *fb, uint32_t held) {
  // a dark blue background, lighter at the top
  for (int y = 0; y < PAD_H; y++)
    padFill(fb, 0, y, PAD_W, 1, padColor(8, 10, 30 + (PAD_H - y) / 8));
  padFill(fb, 0, 0, PAD_W, 2, padColor(70, 80, 140));
  for (int i = 0; i < padButtonCount; i++) {
    const PadButton &b = padButtons[i];
    bool on = held & (1u << i);
    uint16_t edge = on ? padColor(255, 255, 255) : padColor(150, 160, 210);
    uint16_t face = on ? padColor(60, 110, 255) : padColor(30, 34, 70);
    padFill(fb, b.x + 2, b.y, b.w - 4, b.h, edge);
    padFill(fb, b.x, b.y + 2, b.w, b.h - 4, edge);
    padFill(fb, b.x + 1, b.y + 1, b.w - 2, b.h - 2, edge);
    padFill(fb, b.x + 3, b.y + 3, b.w - 6, b.h - 6, face);
    int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
    const char *l = b.label;
    if (l[1] == 0 && (l[0] == '<' || l[0] == '>' || l[0] == '^' || l[0] == 'v'))
      padArrow(fb, cx, cy, l[0], 22, padColor(255, 255, 255));
    else {
      int sc = (int)strlen(l) * 18 <= b.w - 12 ? 2 : 1;
      padText(fb, cx - ((int)strlen(l) * 9 * sc - sc) / 2, cy - 4 * sc, l, sc, 255, 255, 255);
    }
  }
}

// before a frame is shown: the pad as it is now, the side columns cleared
static uint16_t *padBuffer[2] = {NULL, NULL};
static uint32_t padDrawn[2] = {0xFFFFFFFF, 0xFFFFFFFF};

static void padShow(uint16_t *fb) {
  int k = fb == padBuffer[0] ? 0 : fb == padBuffer[1] ? 1 : -1;
  if (k < 0) {
    k = padBuffer[0] == NULL ? 0 : 1;
    padBuffer[k] = fb;
  }
  uint32_t held = padHeld;
  if (padDrawn[k] != held) {
    drawPad(fb, held);
    padDrawn[k] = held;
  }
#if PAD_SIDES
  for (int y = 0; y < PAD_Y; y++) {
    memset(fb + y * 480, 0, GAME_X * 2);
    memset(fb + y * 480 + GAME_X + 400, 0, (480 - GAME_X - 400) * 2);
  }
#endif
}
