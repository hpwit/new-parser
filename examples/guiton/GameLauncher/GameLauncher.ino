// A menu of compiled games on the Guition ESP32-4848S040 (a 4-inch 480x480
// touch screen), read from the FFat partition: touch a game, it loads and
// runs; its QUIT button comes back to the menu. Under the games, - and +
// set the backlight (full again at every start). Nothing is compiled on the
// board: each game is a binary made by parseScriptToBinary(), so it starts
// at once.
//
// The data folder next to this sketch holds the guiton games: tetris.bin,
// mazechase.bin, neonracer.bin, prismbreak.bin, xenowing.bin,
// chromaquest.bin, vault13h.bin and ironforge.bin, compiled from the scripts
// of the examples next to this one (XenoWing's and ChromaQuest's art are in
// this sketch too; Ironforge's monsters are in ironforge.wad, on the
// partition next to the games). To put them on the board's FFat partition,
// see the README's "On the Guition ESP32-4848S040".
//
// Each game is shown as in its own sketch, with the same touch pad:
//   tetris      240x240 at pixel size 2, its own buttons drawn by the
//               script; touch the TETRIS title to come back
//   vault13h    its 320x240 view 1.5 times larger (480x360), a pad of 120 rows
//   ironforge   240x240 at pixel size 2, its own pad drawn by the script;
//               hold the status bar a second to come back
//   the others  400x240, 1.2 times larger (480x288), a pad of 192 rows
// (LauncherPad.h is the guiton games' TouchPad.h, switched per game.) A .bin
// the launcher doesn't know gets the arrows, START and QUIT.
//
// How a game stops: every game calls readKey() once per frame. When QUIT was
// touched, readKey() never returns: the game's task waits there, in a
// function of this sketch that holds nothing, and loop() kills the task and
// frees the game. (Killing it anywhere else, inside the panel driver say,
// could leave the screen half way.) Ironforge reads no key: it waits at the
// start of show16(), before the panel is touched.
//
// Needs the modified LovyanGFX Bus_RGB driver (double frame buffer with
// show(), _frame_buffer, pixel_size, offset) and FastLED (CHSV).
// Guition4848S040.h is the panel configuration.
//
// Board: ESP32S3 Dev Module, PSRAM "OPI PSRAM", Flash Size 16MB,
// Partition Scheme "16M Flash (3MB APP/9.9MB FATFS)".
#include <new>
#include <FS.h>
#include <FFat.h>
#include "Guition4848S040.h"
#include "FastLED.h"
#include "script_executable.h"
#include "binding.h"
// XenoWing's art: the game reads it through six bound pointers
#include "xenowing_assets.h"
// ChromaQuest's worlds and sprites, read through three bound pointers
#include "chroma_assets.h"

LGFX lcd;
static uint16_t *gfxP = (uint16_t *)gfx, *skyP = (uint16_t *)sky, *farP = (uint16_t *)farRidge;
static uint16_t *forestP = (uint16_t *)forest, *nearP = (uint16_t *)nearRidge, *frontP = (uint16_t *)frontPlants;
static uint16_t *chromaGfxP = (uint16_t *)chromaGfx, *worldColorP = (uint16_t *)worldColor;
static uint8_t *worldLumP = (uint8_t *)worldLum;
uint16_t *frame_buffer;
// where the game draws (led16): the frame buffer itself for Tetris, else a
// 400x240 picture of its own (480 wide, in PSRAM) that show16() scales
uint16_t *gameBuffer;
static uint16_t *ownBuffer;
static uint32_t _sizex = 480;
static uint32_t _sizey = 480;

#include "LauncherPad.h"

// how the game being played is shown: SHOW_PIXELS, it draws into the frame
// buffer at its own pixel size (Tetris, Ironforge)
enum { SHOW_MENU, SHOW_PIXELS, SHOW_SCALED, SHOW_VIEW };
static int showMode = SHOW_MENU;

// --- each game's buttons (coordinates in its pad, 480 wide) ---

static const PadButton mazeChasePad[] = {
  {85, 4, 80, 58, 'z', PAD_TAP, "^"},
  {4, 64, 80, 64, 'q', PAD_TAP, "<"},
  {166, 64, 80, 64, 'd', PAD_TAP, ">"},
  {85, 130, 80, 58, 's', PAD_TAP, "v"},
  {290, 64, 186, 124, ' ', PAD_TAP, "START"},
  {376, 4, 100, 52, 27, PAD_TAP, "QUIT"},
};
static const PadButton neonRacerPad[] = {
  {4, 40, 140, 148, 'q', PAD_REPEAT, "<"},
  {150, 40, 140, 148, 'd', PAD_REPEAT, ">"},
  {300, 62, 176, 60, 's', PAD_REPEAT, "BRAKE"},
  {300, 128, 176, 60, ' ', PAD_TAP, "START"},
  {376, 4, 100, 52, 27, PAD_TAP, "QUIT"},
};
static const PadButton prismBreakPad[] = {
  {4, 40, 140, 148, 'q', PAD_HOLD_STOP, "<"},
  {150, 40, 140, 148, 'd', PAD_HOLD_STOP, ">"},
  {300, 62, 176, 126, ' ', PAD_TAP, "SERVE"},
  {376, 4, 100, 52, 27, PAD_TAP, "QUIT"},
};
static const PadButton xenoWingPad[] = {
  {85, 4, 80, 58, 'z', PAD_HOLD, "^"},
  {4, 64, 80, 64, 'q', PAD_HOLD, "<"},
  {166, 64, 80, 64, 'd', PAD_HOLD, ">"},
  {85, 130, 80, 58, 's', PAD_HOLD, "v"},
  {290, 64, 186, 124, ' ', PAD_TAP, "BOMB"},
  {376, 4, 100, 52, 27, PAD_TAP, "QUIT"},
};
// ChromaQuest, and any game the launcher doesn't know
static const PadButton holdPad[] = {
  {85, 4, 80, 58, 'z', PAD_HOLD, "^"},
  {4, 64, 80, 64, 'q', PAD_HOLD, "<"},
  {166, 64, 80, 64, 'd', PAD_HOLD, ">"},
  {85, 130, 80, 58, 's', PAD_HOLD, "v"},
  {290, 64, 186, 124, ' ', PAD_TAP, "START"},
  {376, 4, 100, 52, 27, PAD_TAP, "QUIT"},
};
static const PadButton vaultPad[] = {
  {4, 8, 70, 104, 'q', PAD_HOLD, "<"},
  {78, 8, 70, 48, 'z', PAD_HOLD, "^"},
  {78, 64, 70, 48, 's', PAD_HOLD, "v"},
  {152, 8, 70, 104, 'd', PAD_HOLD, ">"},
  {232, 8, 40, 48, '1', PAD_TAP, "1"},
  {276, 8, 40, 48, '2', PAD_TAP, "2"},
  {320, 8, 40, 48, '3', PAD_TAP, "3"},
  {232, 64, 128, 48, 27, PAD_TAP, "QUIT"},
  {364, 8, 112, 104, ' ', PAD_TAP, "FIRE"},
};
// Tetris draws its own buttons (examples/guiton/Tetris, at 240x240, here in
// screen pixels); their order gives the bits of touchHeld the script reads:
// left 1, right 2, down 4, turn 8, drop 16. The TETRIS title is QUIT.
static const PadButton tetrisPad[] = {
  {260, 364, 68, 108, 'q', PAD_REPEAT, ""},
  {404, 364, 68, 108, 'd', PAD_REPEAT, ""},
  {332, 364, 68, 108, 's', PAD_REPEAT, ""},
  {260, 248, 140, 108, 'z', PAD_TAP, ""},
  {404, 248, 68, 108, ' ', PAD_TAP, ""},
  {260, 0, 220, 32, 27, PAD_TAP, ""},
};
#define COUNT(a) (int)(sizeof(a) / sizeof(a[0]))

// Ironforge draws its pad (examples/guiton/Ironforge, at 240x240) and reads
// touchHeld: bits 0 turn left, 1 turn right, 2 forward, 3 back, 4 FIRE,
// 5 USE, 6 GUN. Bit 7 is the launcher's: the status bar, held a second, QUIT.
#define IRONFORGE_QUIT (1u << 7)
static uint32_t ironforgeBits(int x, int y) {
  x /= 2;
  y /= 2;
  if (y >= 144 && y < 176)
    return IRONFORGE_QUIT;
  if (y < 176)
    return 0;
  if (x >= 164)
    return 1u << 4;
  if (x >= 104)
    return y < 207 ? 1u << 5 : 1u << 6;
  // the cross: the side of its middle the finger is on; between two arms,
  // both (forward and turning)
  int dx = x - 52, dy = y - 208;
  if (abs(dx) < 5 && abs(dy) < 5)
    return 0;
  uint32_t b = 0;
  if (abs(dx) * 5 > abs(dy) * 2)
    b |= dx < 0 ? 1u << 0 : 1u << 1;
  if (abs(dy) * 5 > abs(dx) * 2)
    b |= dy < 0 ? 1u << 2 : 1u << 3;
  return b;
}

// HSV -> RGB565, byte swapped like LovyanGFX's frame buffer
static uint16_t hsv16(uint8_t h, uint8_t s, uint8_t v) {
  CRGB d = CHSV(h, s, v);
  uint16_t j = lcd.color565(d.red, d.green, d.blue);
  return ((j & 0xff) << 8) | ((j & 0xFF00) >> 8);
}

// a 400x240 game, 1.2 times larger, into the frame buffer's top 480x288:
// each five pixels become six (the first twice), each five rows six
static void scaleGame(uint16_t *fb) {
  for (int dy = 0; dy < 288; dy++) {
    uint16_t *dst = fb + dy * 480;
    if (dy % 6 == 1) {
      memcpy(dst, dst - 480, 480 * 2);
      continue;
    }
    const uint16_t *src = ownBuffer + (dy * 5 / 6) * 480;
    for (int sx = 0; sx < 400; sx += 5) {
      dst[0] = src[sx];
      dst[1] = src[sx];
      dst[2] = src[sx + 1];
      dst[3] = src[sx + 2];
      dst[4] = src[sx + 3];
      dst[5] = src[sx + 4];
      dst += 6;
    }
  }
}

// Vault13H's 320x240 view (from x 40), 1.5 times larger, into the top
// 480x360: each two pixels become three (a, a, b), each two rows three
static void scaleView(uint16_t *fb) {
  for (int dy = 0; dy < 360; dy++) {
    uint16_t *dst = fb + dy * 480;
    if (dy % 3 == 1) {
      memcpy(dst, dst - 480, 480 * 2);
      continue;
    }
    const uint16_t *src = ownBuffer + (dy * 2 / 3) * 480 + 40;
    for (int sx = 0; sx < 320; sx += 2) {
      uint16_t a = src[sx], b = src[sx + 1];
      dst[0] = a;
      dst[1] = a;
      dst[2] = b;
      dst += 3;
    }
  }
}

static volatile bool gameParked = false;
static bool holdToQuit = false;   // Ironforge: QUIT is the status bar, held
static uint32_t quitSince = 0;

void show16() {
  if (holdToQuit) {
    if (!(padHeld & IRONFORGE_QUIT))
      quitSince = 0;
    else if (quitSince == 0)
      quitSince = millis() | 1;
    else if (millis() - quitSince >= 1000) {
      gameParked = true;
      while (true)
        vTaskDelay(1000);
    }
  }
  if (showMode == SHOW_SCALED)
    scaleGame(frame_buffer);
  if (showMode == SHOW_VIEW)
    scaleView(frame_buffer);
  padShow(frame_buffer);
  lcd._bus_instance.show();
  frame_buffer = (uint16_t *)lcd._bus_instance._frame_buffer;
  if (showMode == SHOW_PIXELS || showMode == SHOW_MENU)
    gameBuffer = frame_buffer;
  lcd._panel_instance.resetWriteBuffer();
}

void clearscreen() {
  memset(frame_buffer, 0, 480 * 480 * 2);
  memset(ownBuffer, 0, 480 * 240 * 2);
  padDrawn[0] = padDrawn[1] = 0xFFFFFFFF;
}

// Tetris and Ironforge ask for pixel size 2 and get it; the others ask for 2 (400x240 on
// the 800x480 panel they were written for) and are scaled by show16() instead
void setPixelsize(int h) {
  if (showMode != SHOW_PIXELS)
    h = 1;
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

float scriptSin(float a) {
  return sinf(a);
}

float scriptHypot(float x, float y) {
  return hypotf(x, y);
}

float scriptAtan2(float y, float x) {
  return atan2f(y, x);
}

// --- Ironforge's WAD, /ironforge.wad: read into PSRAM when the game first
// starts, and kept (as in examples/guiton/Ironforge) ---
static uint8_t *wadData = NULL;
static int wadLoaded = 0;

static void loadWad(const char *path) {
  if (wadData)
    return;
  File f = FFat.open(path, FILE_READ);
  if (!f) {
    printf("no %s on the FFat partition: Ironforge uses its own monsters\n", path);
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

// the picture named prefix + frame letter + angle digit (HUSKA1)
static int wadSprite(char *prefix, int frame, int angle) {
  char name[9];
  snprintf(name, sizeof(name), "%s%c%d", prefix, 'A' + frame, angle);
  return wadFind(name);
}

#define KEY_ESCAPE 27

// the game that runs, built in place: a ScriptExecutable can't be copied or
// moved, and it has to be freed when the game stops
alignas(ScriptExecutable) static uint8_t gameSlot[sizeof(ScriptExecutable)];
static ScriptExecutable *game = NULL;

// the games' readKey(): on QUIT (Escape), the game's task stops here for good
int readKey() {
  int c = popKey();
  if (c == KEY_ESCAPE) {
    gameParked = true;
    while (true)
      vTaskDelay(1000);
  }
  return c;
}

// the games on the partition
#define MAX_GAMES 8
static char gameFile[MAX_GAMES][32];
static uint32_t gameSize[MAX_GAMES];
static int gameCount = 0;
static char message[40] = "";

static void listGames() {
  gameCount = 0;
  File root = FFat.open("/");
  File f = root.openNextFile();
  while (f && gameCount < MAX_GAMES) {
    const char *name = f.name();
    size_t n = strlen(name);
    if (!f.isDirectory() && n > 4 && n < sizeof(gameFile[0]) && strcasecmp(name + n - 4, ".bin") == 0) {
      strcpy(gameFile[gameCount], name);
      gameSize[gameCount] = f.size();
      gameCount++;
    }
    f = root.openNextFile();
  }
  // in alphabetical order
  for (int i = 0; i < gameCount; i++)
    for (int j = i + 1; j < gameCount; j++)
      if (strcasecmp(gameFile[j], gameFile[i]) < 0) {
        char t[32];
        strcpy(t, gameFile[i]);
        strcpy(gameFile[i], gameFile[j]);
        strcpy(gameFile[j], t);
        uint32_t s = gameSize[i];
        gameSize[i] = gameSize[j];
        gameSize[j] = s;
      }
}

// --- the menu, 480x480, a row per game to touch ---

#define ROW_Y 76
#define ROW_H 40
#define ROW_STEP 44
// the light: - and + either side of a bar, under the games
#define LIGHT_Y 430
#define LIGHT_H 44
#define TOUCH_DARKER 100
#define TOUCH_LIGHTER 101

// the backlight chosen in the menu (full at every start); a game can change
// it with setBrightness(), the menu puts it back
static int brightness = 255;
// the levels - and + go through: each about 10% under the one before, as
// the eye sees light, down to 48 (at 31 the backlight driver goes black)
static const uint8_t lightLevels[] = {48, 54, 60, 67, 75, 84, 94, 105, 117, 131, 146, 163, 183, 204, 228, 255};
#define LIGHT_STEPS (int)(sizeof(lightLevels) / sizeof(lightLevels[0]))
static int lightStep = LIGHT_STEPS - 1;

static void menuFill(int x, int y, int w, int h, uint16_t c) {
  for (int j = y; j < y + h; j++)
    for (int i = x; i < x + w; i++)
      frame_buffer[j * 480 + i] = c;
}

// a word in capitals (lower case is shown as capitals), in the games' font
static void menuText(int x, int y, const char *s, int sc, int r, int g, int b) {
  char up[40];
  int n = 0;
  for (; *s && n < 39; s++)
    up[n++] = toupper(*s);
  up[n] = 0;
  padText(frame_buffer, x, y, up, sc, r, g, b);
}

static void drawMenu(uint32_t t, int pressed) {
  for (int y = 0; y < 480; y++)
    menuFill(0, y, 480, 1, hsv16(170 + y / 16, 255, 20 + y / 8));
  const char *title = "GAME LAUNCHER";
  for (int i = 0; title[i]; i++) {
    char one[2] = {title[i], 0};
    CRGB c = CHSV(i * 20 + t / 20, 220, 255);
    menuText(66 + i * 27, 20, one, 3, c.red, c.green, c.blue);
  }
  if (gameCount == 0) {
    menuText(48, 200, "NO BIN FILE ON THE FFAT PARTITION", 1, 255, 120, 120);
    menuText(84, 224, "SEE THE README GAMELAUNCHER", 1, 200, 200, 200);
  }
  for (int i = 0; i < gameCount; i++) {
    int y = ROW_Y + i * ROW_STEP;
    bool on = i == pressed;
    uint16_t edge = on ? hsv16(0, 0, 255) : hsv16(160, 90, 200);
    uint16_t face = on ? hsv16(160, 200, 255) : hsv16(165, 200, 70);
    menuFill(22, y, 436, ROW_H, edge);
    menuFill(20, y + 2, 440, ROW_H - 4, edge);
    menuFill(23, y + 3, 434, ROW_H - 6, face);
    char line[40];
    snprintf(line, sizeof(line), "%.*s", (int)strlen(gameFile[i]) - 4, gameFile[i]);
    menuText(40, y + 12, line, 2, 255, 255, 255);
    snprintf(line, sizeof(line), "%u", (unsigned)gameSize[i]);
    menuText(372, y + 16, line, 1, 255, 210, 120);
  }
  // the light: - [bar] +
  for (int k = 0; k < 2; k++) {
    int x = k == 0 ? 20 : 400;
    bool on = pressed == (k == 0 ? TOUCH_DARKER : TOUCH_LIGHTER);
    menuFill(x, LIGHT_Y, 60, LIGHT_H, on ? hsv16(0, 0, 255) : hsv16(160, 90, 200));
    menuFill(x + 3, LIGHT_Y + 3, 54, LIGHT_H - 6, on ? hsv16(160, 200, 255) : hsv16(165, 200, 70));
    menuFill(x + 18, LIGHT_Y + LIGHT_H / 2 - 2, 24, 4, hsv16(0, 0, 255));
    if (k == 1)
      menuFill(x + 28, LIGHT_Y + LIGHT_H / 2 - 12, 4, 24, hsv16(0, 0, 255));
  }
  menuFill(96, LIGHT_Y + 14, 288, 16, hsv16(165, 200, 50));
  menuFill(96, LIGHT_Y + 14, 288 * (lightStep + 1) / LIGHT_STEPS, 16, hsv16(40, 200, 255));
  menuText(218, LIGHT_Y + 18, "LIGHT", 1, 255, 255, 255);
  if (message[0])
    menuText(20, 58, message, 1, 255, 90, 90);
}

// which row a touch is on (or the light's - or +), or -1
static int rowAt(int x, int y) {
  if (y >= LIGHT_Y && y < LIGHT_Y + LIGHT_H) {
    if (x >= 20 && x < 80)
      return TOUCH_DARKER;
    if (x >= 400 && x < 460)
      return TOUCH_LIGHTER;
    return -1;
  }
  if (x < 20 || x >= 460 || y < ROW_Y)
    return -1;
  int i = (y - ROW_Y) / ROW_STEP;
  if (i >= gameCount || (y - ROW_Y) % ROW_STEP >= ROW_H)
    return -1;
  return i;
}

// how a game is shown and played, from its file's name
static void setUpFor(const char *file) {
  if (strncasecmp(file, "tetris", 6) == 0) {
    showMode = SHOW_PIXELS;
    gameBuffer = frame_buffer;
    usePad(tetrisPad, COUNT(tetrisPad), 0, false, 220, 70);
    return;
  }
  if (strncasecmp(file, "ironforge", 9) == 0) {
    showMode = SHOW_PIXELS;
    gameBuffer = frame_buffer;
    loadWad("/ironforge.wad");
    usePad(NULL, 0, 0, false, 150, 40);
    padBitsAt = ironforgeBits;
    holdToQuit = true;
    return;
  }
  gameBuffer = ownBuffer;
  if (strncasecmp(file, "vault13h", 8) == 0) {
    showMode = SHOW_VIEW;
    usePad(vaultPad, COUNT(vaultPad), 360, true, 150, 40);
    return;
  }
  showMode = SHOW_SCALED;
  if (strncasecmp(file, "mazechase", 9) == 0)
    usePad(mazeChasePad, COUNT(mazeChasePad), 288, true, 150, 40);
  else if (strncasecmp(file, "neonracer", 9) == 0)
    usePad(neonRacerPad, COUNT(neonRacerPad), 288, true, 150, 40);
  else if (strncasecmp(file, "prismbreak", 10) == 0)
    usePad(prismBreakPad, COUNT(prismBreakPad), 288, true, 150, 40);
  else if (strncasecmp(file, "xenowing", 8) == 0)
    usePad(xenoWingPad, COUNT(xenoWingPad), 288, true, 150, 40);
  else
    usePad(holdPad, COUNT(holdPad), 288, true, 150, 40);
}

static void startGame(int i) {
  char path[40];
  snprintf(path, sizeof(path), "/%s", gameFile[i]);
  File f = FFat.open(path, FILE_READ);
  if (!f) {
    snprintf(message, sizeof(message), "CANNOT OPEN %s", gameFile[i]);
    return;
  }
  size_t size = f.size();
  uint8_t *bin = (uint8_t *)malloc(size);
  size_t got = bin ? f.read(bin, size) : 0;
  f.close();
  if (got != size) {
    free(bin);
    snprintf(message, sizeof(message), "CANNOT READ %s", gameFile[i]);
    return;
  }
  uint32_t t0 = millis();
  game = new (gameSlot) ScriptExecutable(createExecutableFromBuffer(bin, size));
  free(bin);
  if (!game->isExeExists()) {
    game->~ScriptExecutable();
    game = NULL;
    snprintf(message, sizeof(message), "%s DOES NOT LOAD", gameFile[i]);
    return;
  }
  printf("%s: loaded in %u ms, %u bytes of code\n", gameFile[i], (unsigned)(millis() - t0),
         (unsigned)game->exe.binary_size);
  message[0] = 0;
  setUpFor(gameFile[i]);
  while (popKey() != 0)
    ;
  gameParked = false;
  game->executeAsTask("main", 16384, 1, 1, "game");
}

static void stopGame() {
  noPad();
  holdToQuit = false;
  quitSince = 0;
  game->kill();
  game->~ScriptExecutable();
  game = NULL;
  gameParked = false;
  while (popKey() != 0)
    ;
  showMode = SHOW_MENU;
  setPixelsize(1);
  PAD_Y = 0;  // the menu draws its words with the pad's font, from the top
  lcd.setBrightness(brightness);  // the game may have changed it
  printf("back to the menu\n");
}

// lcd.init() on core 0 so Bus_RGB's interrupts stay off core 1, where the games draw
static void lcdInitTask(void *done) {
  lcd.init();
  // full light at every start; - and + change it until the next one
  lightStep = LIGHT_STEPS - 1;
  brightness = lightLevels[lightStep];
  lcd.setBrightness(brightness);
  xTaskNotifyGive((TaskHandle_t)done);
  vTaskDelete(NULL);
}

void setup() {
  Serial.begin(115200);
  xTaskCreatePinnedToCore(lcdInitTask, "lcdInit", 4096, xTaskGetCurrentTaskHandle(), 5, NULL, 0);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  frame_buffer = (uint16_t *)lcd._bus_instance._frame_buffer;
  ownBuffer = (uint16_t *)heap_caps_calloc(480 * 240, 2, MALLOC_CAP_SPIRAM);
  gameBuffer = frame_buffer;

  // what every game uses: the games are compiled against these names
  bindVariable((char *)"uint32_t", (char *)"width", NULL, (void *)&_sizex);
  bindVariable((char *)"uint32_t", (char *)"height", NULL, (void *)&_sizey);
  // a pointer, not an array: it changes with the frame buffers and the game
  bindVariable((char *)"uint16_t *", (char *)"led16", NULL, (void *)&gameBuffer);
  bindFunction((char *)"uint16_t", (char *)"hsv2", (char *)"int,int,int", (void *)hsv16);
  bindFunction((char *)"void", (char *)"show16", NULL, (void *)show16);
  bindFunction((char *)"void", (char *)"setPixelsize", (char *)"int", (void *)setPixelsize);
  bindFunction((char *)"uint32_t", (char *)"millis", NULL, (void *)scriptMillis);
  bindFunction((char *)"uint32_t", (char *)"rand", (char *)"uint32_t", (void *)scriptRand);
  bindFunction((char *)"int", (char *)"readKey", NULL, (void *)readKey);
  bindFunction((char *)"void", (char *)"setBrightness", (char *)"int", (void *)scriptSetBrightness);
  // Tetris: the buttons held, to draw them lit
  bindVariable((char *)"uint32_t", (char *)"touchHeld", NULL, (void *)&padHeld);
  // Vault13H
  bindFunction((char *)"float", (char *)"sin", (char *)"float", (void *)scriptSin);
  bindFunction((char *)"float", (char *)"hypot", (char *)"float,float", (void *)scriptHypot);
  // Ironforge: atan2, and its monsters' WAD
  bindFunction((char *)"float", (char *)"atan2", (char *)"float,float", (void *)scriptAtan2);
  bindVariable((char *)"uint8_t *", (char *)"wad", NULL, (void *)&wadData);
  bindVariable((char *)"int", (char *)"wadLoaded", NULL, (void *)&wadLoaded);
  bindFunction((char *)"int", (char *)"wadSprite", (char *)"char*,int,int", (void *)wadSprite);
  bindFunction((char *)"int", (char *)"wadFind", (char *)"char*", (void *)wadFind);
  // XenoWing's sprites and the five layers of its alien world (in flash)
  bindVariable((char *)"uint16_t *", (char *)"gfx", NULL, (void *)&gfxP);
  bindVariable((char *)"uint16_t *", (char *)"sky", NULL, (void *)&skyP);
  bindVariable((char *)"uint16_t *", (char *)"farRidge", NULL, (void *)&farP);
  bindVariable((char *)"uint16_t *", (char *)"forest", NULL, (void *)&forestP);
  bindVariable((char *)"uint16_t *", (char *)"nearRidge", NULL, (void *)&nearP);
  bindVariable((char *)"uint16_t *", (char *)"frontPlants", NULL, (void *)&frontP);
  // ChromaQuest's sprites and its three worlds, in colour and in brightness
  bindVariable((char *)"uint16_t *", (char *)"chromaGfx", NULL, (void *)&chromaGfxP);
  bindVariable((char *)"uint16_t *", (char *)"worldColor", NULL, (void *)&worldColorP);
  bindVariable((char *)"uint8_t *", (char *)"worldLum", NULL, (void *)&worldLumP);

  // false: a partition that doesn't mount is left alone, never formatted
  if (!FFat.begin(false))
    printf("FFat does not mount: put the data folder on the partition (see the README)\n");
  else
    listGames();
  printf("%d games, light %d\n", gameCount, brightness);
  showMode = SHOW_MENU;
  setPixelsize(1);
  PAD_Y = 0;

  // the touch screen, read on core 0 while a game plays
  xTaskCreatePinnedToCore(padTouchTask, "touch", 4096, NULL, 3, NULL, 0);
}

// the menu: a row lights while touched, and its game starts when the finger
// lifts on it; - and + step the light through lightLevels
static int pressedRow = -1;

void loop() {
  if (game != NULL) {
    if (gameParked) {
      stopGame();
      pressedRow = -2;  // the finger that touched QUIT must lift first
    } else
      delay(50);
    return;
  }
  lgfx::touch_point_t tp;
  bool down = lcd.getTouchRaw(&tp, 1) > 0;
  if (down) {
    if (pressedRow != -2) {
      int r = rowAt(tp.x, tp.y);
      pressedRow = r;
    }
  } else {
    if (pressedRow == TOUCH_DARKER || pressedRow == TOUCH_LIGHTER) {
      lightStep += pressedRow == TOUCH_LIGHTER ? 1 : -1;
      lightStep = lightStep < 0 ? 0 : lightStep >= LIGHT_STEPS ? LIGHT_STEPS - 1 : lightStep;
      brightness = lightLevels[lightStep];
      lcd.setBrightness(brightness);
      printf("light %d\n", brightness);
    } else if (pressedRow >= 0) {
      int r = pressedRow;
      pressedRow = -1;
      startGame(r);
      if (game != NULL)
        return;
    }
    pressedRow = -1;
  }
  drawMenu(millis(), pressedRow);
  show16();
  delay(30);
}
