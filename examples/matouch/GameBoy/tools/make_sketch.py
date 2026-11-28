#!/usr/bin/env python3
"""Writes ../GameBoy.ino: the sketch below with gameboy.sc inside it.

    python make_sketch.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))

HEAD = r'''// Game Boy on the ESP32-S3-Touch-LCD-7: an emulator of the first Game Boy
// (DMG) written as a script, compiled on the board. It lists the .gb
// cartridges at the root of the microSD card and of the FFat partition,
// and runs the one you pick: 160x144 at pixel size 3 in the middle of the
// 800x480 panel, played with a USB keyboard. No sound.
// See README_GAMES.md, "Game Boy".
//
// Keys
//   arrows (Z Q S D)          the cross
//   K or Space                A
//   J or B                    B
//   Enter                     Start (and chooses a cartridge in the list)
//   Tab or Backspace          Select
//   Escape                    back to the list
//
// The picture is shown at the script's pixelsize (3: 480x432, centred).
//
// The script is tools/gameboy.sc, put in here by tools/make_sketch.py. It
// uses the names of SolarOS's script runtime (display, updateDisplay,
// setPixelsize, keyHeld...), so the same script runs on SolarOS, plus
// romCount(), romNameChar(i, j), romLoad(i) and rom, bound below.
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
#include <SD_MMC.h>
#include "Matouch7.h"
#include "keyboard.h"
#include "script_executable.h"
#include "asm_execute.h"
#include "binding.h"

// GB_TEST: a cartridge from test_rom.h instead of the card, a clock that
// moves one frame (17 ms) per picture shown so that no frame is skipped,
// and a checksum of the picture every GB_TEST frames (tools/board_check.py;
// at 6453, where pixel size 3 puts it)
#ifdef GB_TEST
#include "test_rom.h"
#endif

// The compiler recurses through the script's expressions: on its own 8 KB
// stack the loop task overflows while it compiles. 32 KB.
SET_LOOP_TASK_STACK_SIZE(32 * 1024);

LGFX lcd;
// where the script draws: a pointer, not an array, since updateDisplay()
// swaps the frame buffers
uint16_t *frame_buffer;
static uint32_t shown = 0, shownAt = 0;

#ifdef GB_TEST
static int testFrames = 0, inGame = 0;
static void testChecksum() {
  uint32_t h = 2166136261u;
  for (int y = 0; y < 144; y++)
    for (int x = 0; x < 160; x++) {
      uint16_t v = frame_buffer[6453 + y * 800 + x];
      h = (h ^ (v & 255)) * 16777619u;
      h = (h ^ (v >> 8)) * 16777619u;
    }
  printf("GBTEST frame %d checksum %08lx\n", testFrames, (unsigned long)h);
}
#endif

void updateDisplay() {
#ifdef GB_TEST
  if (inGame) {
    testFrames++;
    if (testFrames % GB_TEST == 0)
      testChecksum();
  }
#endif
  lcd._bus_instance.show();
  frame_buffer = (uint16_t *)lcd._bus_instance._frame_buffer;
  lcd._panel_instance.resetWriteBuffer();
  // pictures shown a second, on the Serial Monitor every 10 s
  shown++;
  uint32_t now = millis();
  if (now - shownAt >= 10000) {
    if (shownAt != 0)
      printf("gameboy: %.1f pictures a second\n", shown * 1000.0f / (now - shownAt));
    shown = 0;
    shownAt = now;
  }
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
#ifdef GB_TEST
  if (inGame)
    return testFrames * 17;
#endif
  return millis();
}

// as SolarOS's delay(): the task sleeps, at least one tick, so the other
// tasks (the idle task, the keyboard) run
void scriptDelay(uint32_t ms) {
  TickType_t t = pdMS_TO_TICKS(ms);
  vTaskDelay(t > 0 ? t : 1);
}

// --- the cartridges: .gb files at the root of the card and of FFat ---
#define PIN_SD_CMD 11
#define PIN_SD_CLK 12
#define PIN_SD_D0 13
#define MAX_ROMS 64
static char *romPath[MAX_ROMS];
static int roms = 0;
static uint8_t *rom = NULL;

static bool isGb(const char *name) {
  size_t n = strlen(name);
  return n > 3 && strcasecmp(name + n - 3, ".gb") == 0;
}

static void listRoms(fs::FS &fs, const char *prefix) {
  File dir = fs.open("/");
  if (!dir)
    return;
  for (File f = dir.openNextFile(); f && roms < MAX_ROMS; f = dir.openNextFile()) {
    const char *name = f.name();
    if (!f.isDirectory() && isGb(name)) {
      romPath[roms] = (char *)malloc(strlen(prefix) + strlen(name) + 2);
      sprintf(romPath[roms], "%s/%s", prefix, name[0] == '/' ? name + 1 : name);
      roms++;
    }
  }
}

int romCount() {
#ifdef GB_TEST
  return 1;
#endif
  return roms;
}

// the j-th letter of cartridge i's name (without its folder), 0 at its end
int romNameChar(int i, int j) {
#ifdef GB_TEST
  return j < 4 ? "TEST"[j] : 0;
#endif
  if (i < 0 || i >= roms)
    return 0;
  const char *n = strrchr(romPath[i], '/') + 1;
  return j < (int)strlen(n) ? (uint8_t)n[j] : 0;
}

// reads cartridge i into PSRAM: its size, 0 when it can't
int romLoad(int i) {
#ifdef GB_TEST
  inGame = 1;
  rom = (uint8_t *)test_rom;
  return sizeof(test_rom);
#endif
  if (i < 0 || i >= roms)
    return 0;
  free(rom);
  rom = NULL;
  const char *path = romPath[i];
  File f = strncmp(path, "/sd/", 4) == 0 ? SD_MMC.open(path + 3) : FFat.open(path + 5);
  if (!f)
    return 0;
  size_t size = f.size();
  // a 32 KB cartridge at least, room for the largest bank number
  rom = (uint8_t *)ps_malloc(size < 32768 ? 32768 : size);
  if (rom == NULL) {
    f.close();
    return 0;
  }
  size_t got = f.read(rom, size);
  f.close();
  printf("gameboy: %s, %u bytes\n", path, (unsigned)got);
  return got == size ? (int)size : 0;
}

// --- the keys: keyHeld(c) whether one is down (the arrows as Z Q S D) ---
static volatile uint8_t keyDown[128];

int keyHeld(int c) {
#ifdef GB_TEST
  if (!inGame)
    return c == 13 && shown >= 2 && shown < 4;
#endif
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
  if (k.key_code == 0x2A)  // Backspace
    return 8;
  return tolower(k.key_char) & 127;
}

void print_char(key_return k) {
  keyDown[keyLetter(k)] = 1;
}

void release_char(key_return k) {
  keyDown[keyLetter(k)] = 0;
}

char gameboy[] = R"EOF('''

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

  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
  if (SD_MMC.begin("/sdcard", true))  // true: 1 bit mode; a card that fails is never formatted
    listRoms(SD_MMC, "/sd");
  if (FFat.begin(false))
    listRoms(FFat, "/ffat");
  printf("gameboy: %d cartridges\n", roms);

  bindVariable((char *)"uint16_t *", (char *)"display", NULL, (void *)&frame_buffer);
  bindFunction((char *)"void", (char *)"updateDisplay", NULL, (void *)updateDisplay);
  bindFunction((char *)"void", (char *)"enableGraphicMode", NULL, (void *)enableGraphicMode);
  bindFunction((char *)"void", (char *)"setPixelsize", (char *)"int", (void *)setPixelsize);
  bindFunction((char *)"uint32_t", (char *)"millis", NULL, (void *)scriptMillis);
  bindFunction((char *)"void", (char *)"delay", (char *)"uint32_t", (void *)scriptDelay);
  bindFunction((char *)"int", (char *)"keyHeld", (char *)"int", (void *)keyHeld);
  bindFunction((char *)"int", (char *)"romCount", NULL, (void *)romCount);
  bindFunction((char *)"int", (char *)"romNameChar", (char *)"int,int", (void *)romNameChar);
  bindFunction((char *)"int", (char *)"romLoad", (char *)"int", (void *)romLoad);
  bindVariable((char *)"uint8_t *", (char *)"rom", NULL, (void *)&rom);

  // the emulator's inner loop wants internal RAM when there is room
  setScriptCodeMemory(SCRIPT_CODE_AUTO);
  uint32_t t0 = millis();
  static ScriptExecutable exec = parseScript(gameboy);
  if (!exec.isExeExists()) {
    showCompileError();
    return;
  }
  printf("gameboy: %u bytes of code, compiled in %u ms\n", (unsigned)exec.exe.binary_size, (unsigned)(millis() - t0));

  hid_keyboard_print_char = print_char;
  hid_keyboard_release_char = release_char;
  app_main_keyboard();

  exec.execute("main");
  printf("gameboy: the script has ended\n");
}

void loop() {
  delay(1000);
}
'''


def main():
    script = open(os.path.join(HERE, 'gameboy.sc')).read()
    assert ')EOF' not in script
    out = os.path.join(HERE, '..', 'GameBoy.ino')
    open(out, 'w').write(HEAD + script + TAIL)
    print('%s: %d bytes' % (os.path.normpath(out), os.path.getsize(out)))


if __name__ == '__main__':
    main()
