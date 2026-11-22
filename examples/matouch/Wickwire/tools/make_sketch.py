"""Writes ../Wickwire.ino: the sketch below with wickwire.sc inside it.

    python make_sketch.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))

HEAD = r'''// Wickwire and the Haunted Keep, on the ESP32-S3-Touch-LCD-7: a 3D platform
// game, all software rendered by the script at 400x240 (shown 2x on the
// 800x480 panel), played with a USB keyboard. Wick, a clockwork fox with a
// candle in his chest, climbs a haunted keep: the courtyard, the grand
// staircase, the ghost gallery, the rooftops, the moonlets (gravity pulls
// him round each one) and the bell tower. See README_GAMES.md, "Wickwire".
//
// The textures, the levels and the story come from /wickwire.wad on the
// FFat partition (data/wickwire.wad, made by tools/make_wad.py). The sketch
// reads it into PSRAM and binds it for the script (wad, wadLoaded, wadFind).
// The script is tools/wickwire.sc, put in here by tools/make_sketch.py; it
// is compiled on the board (a few minutes: its progress goes to the Serial
// Monitor). The GameLauncher and SolarOS run the same game precompiled
// (wickwire.bin).
//
// Keys
//   up / down (Z S, W)      run forward, step back
//   left / right (Q D, A)   turn (the camera follows behind)
//   Space (K)               jump; again in the air: a second jump
//   F (J)                   spin: breaks crates, kicks bombs, beats ghosts
//   Enter                   pause; Escape leaves; P shows the frame time
//   M                       full screen or a window in the middle (pixel size 2 or 1)
//
// Needs the modified LovyanGFX Bus_RGB driver (double frame buffer with
// show(), _frame_buffer, pixel_size, offset) and the ESP32_USB_Host_HID
// library with the key hooks (see the README's "A USB keyboard driving a
// script"). Matouch7.h is the panel configuration.
//
// Board: ESP32S3 Dev Module, PSRAM "OPI PSRAM", Flash Size 16MB, Partition
// Scheme "16M Flash (3MB APP/9.9MB FATFS)".
#include <FS.h>
#include <FFat.h>
#include "Matouch7.h"
#include "keyboard.h"
#include "script_executable.h"
#include "asm_execute.h"
#include "binding.h"

// The compiler recurses through the script's expressions and long
// `else if` chains (Wickwire's has a 19-way one): on its own 8 KB stack,
// the loop task (setup() runs on it) overflows while it compiles. 32 KB.
SET_LOOP_TASK_STACK_SIZE(32 * 1024);

LGFX lcd;
// where the script draws: a pointer, not an array, since updateDisplay()
// swaps the frame buffers
uint16_t *frame_buffer;
static uint32_t stride = 800;

void updateDisplay() {
  lcd._bus_instance.show();
  frame_buffer = (uint16_t *)lcd._bus_instance._frame_buffer;
  lcd._panel_instance.resetWriteBuffer();
}

void enableGraphicMode() {
}

void setPixelsize(int h) {
  memset(frame_buffer, 0, 800 * 480 * 2);
  updateDisplay();
  memset(frame_buffer, 0, 800 * 480 * 2);
  lcd._bus_instance.offset = 0;
  lcd._bus_instance.pixel_size = h;
}

uint32_t scriptMillis() {
  return millis();
}

// as SolarOS's delay(): the task sleeps, at least one tick, so the other
// tasks (the idle task, the keyboard) run
void scriptDelay(uint32_t ms) {
  TickType_t t = pdMS_TO_TICKS(ms);
  vTaskDelay(t > 0 ? t : 1);
}

static int scriptArgc = 0;

int scriptArg(int i) {
  (void)i;
  return 0;
}

float scriptSin(float a) {
  return sinf(a);
}

float scriptHypot(float a, float b) {
  return hypotf(a, b);
}

float scriptAtan2(float y, float x) {
  return atan2f(y, x);
}

uint32_t scriptRand(uint32_t n) {
  return n ? esp_random() % n : 0;
}

// --- the WAD: read from the FFat partition into PSRAM ---
static uint8_t *wadData = NULL;
static int wadLoaded = 0;

static void loadWad(const char *path) {
  if (!FFat.begin(false)) {
    printf("FFat does not mount: put data/wickwire.wad on it (see the README)\n");
    return;
  }
  File f = FFat.open(path, FILE_READ);
  if (!f) {
    printf("no %s on the FFat partition\n", path);
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

// --- the keys: readKey() gives each press (the arrows as Z Q S D, a
// released arrow as its letter + 128), keyHeld(c) whether one is down ---
#define KEY_QUEUE 16
static volatile uint8_t keyQueue[KEY_QUEUE];
static volatile uint8_t keyHead = 0, keyTail = 0;
static volatile uint8_t keyDown[128];

static void pushKey(uint8_t c) {
  uint8_t next = (keyHead + 1) % KEY_QUEUE;
  if (next != keyTail) {
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

int keyHeld(int c) {
  return keyDown[c & 127];
}

static uint8_t keyLetter(key_return k) {
  if (k.key_code == HID_KEY_UP)
    return 'z';
  if (k.key_code == HID_KEY_LEFT)
    return 'q';
  if (k.key_code == HID_KEY_DOWN)
    return 's';
  if (k.key_code == HID_KEY_RIGHT)
    return 'd';
  if (k.key_code == HID_KEY_ESC)
    return 27;
  if (k.key_code == HID_KEY_ENTER)
    return 13;
  return tolower(k.key_char) & 127;
}

void print_char(key_return k) {
  uint8_t c = keyLetter(k);
  if (c != 0) {
    keyDown[c] = 1;
    pushKey(c);
  }
}

void release_char(key_return k) {
  uint8_t c = keyLetter(k);
  keyDown[c] = 0;
  if (c == 'z' || c == 'q' || c == 's' || c == 'd')
    pushKey(c | 0x80);
}

// --- the compile's progress, on the Serial Monitor: the script is compiled
// here, on the board, and its 117 KB take a while. setCompileProgress()
// calls showProgress() as the compiler works (at most once a percent);
// it prints a line every 10% and whenever the stage changes. ---
static void showProgress(int percent, const char *stage, void *ctx) {
  static int shownTens = -1;
  static const char *shownStage = NULL;
  (void)ctx;
  if (percent / 10 == shownTens && stage == shownStage)
    return;
  shownTens = percent / 10;
  shownStage = stage;
  char bar[21];
  for (int i = 0; i < 20; i++)
    bar[i] = i < percent / 5 ? '#' : '.';
  bar[20] = 0;
  printf("compiling wickwire [%s] %3d%%  %s\n", bar, percent, stage);
}

char wickwire[] = R"EOF('''

TAIL = r''')EOF";

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

  // the names of SolarOS's script runtime, so that one wickwire.bin runs
  // here, in the GameLauncher and on SolarOS
  bindVariable((char *)"uint16_t *", (char *)"display", NULL, (void *)&frame_buffer);
  bindVariable((char *)"uint32_t", (char *)"stride", NULL, (void *)&stride);
  bindFunction((char *)"void", (char *)"updateDisplay", NULL, (void *)updateDisplay);
  bindFunction((char *)"void", (char *)"enableGraphicMode", NULL, (void *)enableGraphicMode);
  bindFunction((char *)"void", (char *)"setPixelsize", (char *)"int", (void *)setPixelsize);
  bindFunction((char *)"uint32_t", (char *)"millis", NULL, (void *)scriptMillis);
  bindFunction((char *)"void", (char *)"delay", (char *)"uint32_t", (void *)scriptDelay);
  bindFunction((char *)"uint32_t", (char *)"rand", (char *)"uint32_t", (void *)scriptRand);
  bindFunction((char *)"int", (char *)"readKey", NULL, (void *)readKey);
  bindFunction((char *)"int", (char *)"keyHeld", (char *)"int", (void *)keyHeld);
  loadWad("/wickwire.wad");
  bindVariable((char *)"uint8_t *", (char *)"wad", NULL, (void *)&wadData);
  bindVariable((char *)"int", (char *)"wadLoaded", NULL, (void *)&wadLoaded);
  bindFunction((char *)"int", (char *)"wadFind", (char *)"char*", (void *)wadFind);
  // SolarOS's command line: none here (the game starts full screen; M
  // switches to a window in the middle of the panel)
  bindVariable((char *)"int", (char *)"argc", NULL, (void *)&scriptArgc);
  bindFunction((char *)"int", (char *)"arg", (char *)"int", (void *)scriptArg);
  // the 3D maths
  bindFunction((char *)"float", (char *)"sin", (char *)"float", (void *)scriptSin);
  bindFunction((char *)"float", (char *)"hypot", (char *)"float,float", (void *)scriptHypot);
  bindFunction((char *)"float", (char *)"atan2", (char *)"float,float", (void *)scriptAtan2);

  // about 70 KB of code: in internal RAM when a block that big is free, else PSRAM
  setScriptCodeMemory(SCRIPT_CODE_AUTO);
  uint32_t t0 = millis();
  setCompileProgress(showProgress, NULL);
  static ScriptExecutable exec = parseScript(wickwire);
  setCompileProgress(NULL, NULL);
  if (!exec.isExeExists()) {
    showCompileError();
    return;
  }
  printf("wickwire: %u bytes of code, compiled in %u ms\n", (unsigned)exec.exe.binary_size, (unsigned)(millis() - t0));

  hid_keyboard_print_char = print_char;
  hid_keyboard_release_char = release_char;
  app_main_keyboard();

  exec.execute("main");
  printf("wickwire: the game has ended\n");
}

void loop() {
  delay(1000);
}
'''


def main():
    script = open(os.path.join(HERE, 'wickwire.sc')).read()
    assert ')EOF' not in script
    out = os.path.join(HERE, '..', 'Wickwire.ino')
    open(out, 'w').write(HEAD + script + TAIL)
    print('%s: %d bytes' % (os.path.normpath(out), os.path.getsize(out)))


if __name__ == '__main__':
    main()
