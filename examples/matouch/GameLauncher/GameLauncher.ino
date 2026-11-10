// A menu of compiled games, read from the FFat partition: pick one with the
// keyboard, it loads and runs; Escape comes back to the menu. Nothing is
// compiled on the board: each game is a binary made by parseScriptToBinary()
// (see CreateTetrisBinary), so it starts at once.
//
// The data folder next to this sketch holds the games of the matouch examples:
// tetris.bin, mazechase.bin, neonracer.bin, prismbreak.bin, xenowing.bin and
// chromaquest.bin (XenoWing's and ChromaQuest's art are in this sketch too,
// xenowing_assets.h and chroma_assets.h). Any other .bin
// compiled with the same bindings (below) shows in the menu too.
// To put them on the board's FFat partition, see the README's "GameLauncher:
// games from the FFat partition".
//
// Keys: Z/S or the arrows to choose, Space or Enter to play, 1-9 to play
// that game at once; in a game, Escape comes back here. The arrows also play
// in the games: they arrive as Z Q S D.
//
// How a game stops: every game calls readKey() once per frame. When Escape
// was pressed, readKey() never returns: the game's task waits there, in a
// function of this sketch that holds nothing, and loop() kills the task and
// frees the game. (Killing it anywhere else, inside show16() say, could leave
// the screen driver half way.)
//
// Needs the modified LovyanGFX Bus_RGB driver (double frame buffer with show(),
// _frame_buffer, pixel_size, offset), FastLED (CHSV) and the ESP32_USB_Host_HID
// library with the key hook (see the README's "A USB keyboard driving a
// script"). Matouch7.h is the panel configuration (Waveshare ESP32-S3-Touch-LCD
// 800x480 pinout).
//
// For the ESP32-S3-Touch-LCD-7: a 7-inch 800x480 RGB touch screen with an
// ESP32-S3 (16MB flash, 8MB OPI PSRAM). The keyboard goes on its USB host port.
// Board: ESP32S3 Dev Module, PSRAM "OPI PSRAM", Flash Size 16MB,
// Partition Scheme "16M Flash (3MB APP/9.9MB FATFS)".
#include <new>
#include <FS.h>
#include <FFat.h>
#include "Matouch7.h"
#include "FastLED.h"
#include "keyboard.h"
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
static uint32_t _sizex = 800;
static uint32_t _sizey = 480;

// HSV -> RGB565, byte swapped like LovyanGFX's frame buffer
static uint16_t hsv16(uint8_t h, uint8_t s, uint8_t v) {
  CRGB d = CHSV(h, s, v);
  uint16_t j = lcd.color565(d.red, d.green, d.blue);
  return ((j & 0xff) << 8) | ((j & 0xFF00) >> 8);
}

void show16() {
  lcd._bus_instance.show();
  frame_buffer = (uint16_t *)lcd._bus_instance._frame_buffer;
  lcd._panel_instance.resetWriteBuffer();
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

uint32_t scriptMillis() {
  return millis();
}

uint32_t scriptRand(uint32_t n) {
  return esp_random() % n;
}

// the keys, from the keyboard's task to the menu or the game
#define KEY_QUEUE 16
#define KEY_ESCAPE 27
static volatile uint8_t keyQueue[KEY_QUEUE];
static volatile uint8_t keyHead = 0, keyTail = 0;

static void pushKey(uint8_t c) {
  uint8_t next = (keyHead + 1) % KEY_QUEUE;
  if (next != keyTail) {  // full: the key is dropped
    keyQueue[keyHead] = c;
    keyHead = next;
  }
}

static int popKey() {
  if (keyTail == keyHead)
    return 0;
  int c = keyQueue[keyTail];
  keyTail = (keyTail + 1) % KEY_QUEUE;
  return c;
}

void print_char(key_return k) {
  if (k.key_code == HID_KEY_ESC)
    pushKey(KEY_ESCAPE);
  else if (k.key_code == HID_KEY_UP)
    pushKey('z');
  else if (k.key_code == HID_KEY_LEFT)
    pushKey('q');
  else if (k.key_code == HID_KEY_DOWN)
    pushKey('s');
  else if (k.key_code == HID_KEY_RIGHT)
    pushKey('d');
  else if (k.key_char != 0)
    pushKey(k.key_char);
}

// a direction let go, as its letter + 128 (XenoWing moves while a key is
// held; the other games ignore these)
void release_char(key_return k) {
  uint8_t c = tolower(k.key_char);
  if (k.key_code == HID_KEY_UP)
    c = 'z';
  else if (k.key_code == HID_KEY_LEFT)
    c = 'q';
  else if (k.key_code == HID_KEY_DOWN)
    c = 's';
  else if (k.key_code == HID_KEY_RIGHT)
    c = 'd';
  if (c == 'z' || c == 'q' || c == 's' || c == 'd')
    pushKey(c | 0x80);
}

// the game that runs, built in place: a ScriptExecutable can't be copied or
// moved, and it has to be freed when the game stops
alignas(ScriptExecutable) static uint8_t gameSlot[sizeof(ScriptExecutable)];
static ScriptExecutable *game = NULL;
static volatile bool gameParked = false;

// the games' readKey(): on Escape, the game's task stops here for good
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
#define MAX_GAMES 9
static char gameFile[MAX_GAMES][32];
static uint32_t gameSize[MAX_GAMES];
static int gameCount = 0;
static int selected = 0;
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

// --- the menu, drawn into the frame buffer at 400x240 ---

// the games' 8x8 font, 0-9 then A-Z: 8 rows a character, the leftmost pixel
// in the highest bit (ChromaQuest/tools/font8.py)
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
// how each row shades the text's colour, in sixteenths: scaled by rowMul,
// then lifted toward white by rowLift; a dark band, then a bright edge
static const uint8_t rowMul[8] = {16, 16, 16, 7, 16, 15, 12, 9};
static const uint8_t rowLift[8] = {10, 6, 2, 0, 5, 0, 0, 0};

static void fill(int x, int y, int w, int h, uint16_t c) {
  for (int j = y; j < y + h; j++)
    if (j >= 0 && j < 240)
      for (int i = x; i < x + w; i++)
        if (i >= 0 && i < 400)
          frame_buffer[j * 800 + i] = c;
}

// capitals, digits and spaces (lower case is shown as capitals), 9 dots
// apart: a dark shadow down and to the right, then each row in its shade of c
static int text(int x, int y, const char *s, int sc, uint16_t c) {
  uint16_t v = (c << 8) | (c >> 8);
  int cr = (v >> 11) & 31, cg = (v >> 5) & 63, cb = v & 31;
  for (; *s; s++) {
    int ch = toupper(*s);
    int g = -1;
    if (ch >= '0' && ch <= '9')
      g = ch - '0';
    if (ch >= 'A' && ch <= 'Z')
      g = ch - 'A' + 10;
    if (g >= 0)
      for (int pass = 0; pass < 2; pass++)
        for (int r = 0; r < 8; r++) {
          uint16_t rc = 0x4208;  // the shadow: a dark blue
          if (pass == 1) {
            int rr = cr * rowMul[r] >> 4, rg = cg * rowMul[r] >> 4, rb = cb * rowMul[r] >> 4;
            rr += (31 - rr) * rowLift[r] >> 4;
            rg += (63 - rg) * rowLift[r] >> 4;
            rb += (31 - rb) * rowLift[r] >> 4;
            uint16_t w = (rr << 11) | (rg << 5) | rb;
            rc = (w << 8) | (w >> 8);
          }
          int off = pass == 0 ? sc : 0;
          for (int b = 0; b < 8; b++)
            if ((font8[g * 8 + r] >> (7 - b)) & 1)
              fill(x + b * sc + off, y + r * sc + off, sc, sc, rc);
        }
    x += 9 * sc;
  }
  return x;
}

// the same, centred on the screen
static void centred(int y, const char *s, int sc, uint16_t c) {
  text((400 - (int)strlen(s) * 9 * sc) / 2, y, s, sc, c);
}

static void drawMenu(uint32_t t) {
  for (int y = 0; y < 240; y++)
    fill(0, y, 400, 1, hsv16(170 + y / 8, 255, 20 + y / 4));
  const char *title = "GAME LAUNCHER";
  for (int i = 0; title[i]; i++) {
    char one[2] = {title[i], 0};
    text(25 + i * 27, 14, one, 3, hsv16(i * 20 + t / 20, 220, 255));
  }
  if (gameCount == 0) {
    centred(100, "NO BIN FILE ON THE FFAT PARTITION", 1, hsv16(0, 200, 255));
    centred(120, "SEE THE README GAMELAUNCHER", 1, hsv16(0, 0, 200));
  }
  for (int i = 0; i < gameCount; i++) {
    int y = 60 + i * 18;
    bool on = i == selected;
    if (on)
      fill(40, y - 3, 320, 16, hsv16(200 + (t / 10) % 20, 230, 160));
    char line[40];
    snprintf(line, sizeof(line), "%d  %.*s", i + 1, (int)strlen(gameFile[i]) - 4, gameFile[i]);
    text(52, y + 1, line, 1, on ? hsv16(0, 0, 255) : hsv16(150, 60, 220));
    snprintf(line, sizeof(line), "%u", (unsigned)gameSize[i]);
    text(300, y + 1, line, 1, hsv16(45, 150, 220));
  }
  centred(214, "Z S CHOOSE   SPACE PLAY   ESC BACK", 1, hsv16(150, 40, 180));
  if (message[0])
    centred(198, message, 1, hsv16(0, 220, 255));
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
  while (popKey() != 0)
    ;
  gameParked = false;
  game->executeAsTask("main", 16384, 1, 1, "game");
}

static void stopGame() {
  game->kill();
  game->~ScriptExecutable();
  game = NULL;
  gameParked = false;
  while (popKey() != 0)
    ;
  setPixelsize(2);
  printf("back to the menu\n");
}

// lcd.init() on core 0 so Bus_RGB's line copy interrupts stay off core 1, where the games draw
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

  // what every game uses: the games are compiled against these names
  bindVariable((char *)"uint32_t", (char *)"width", NULL, (void *)&_sizex);
  bindVariable((char *)"uint32_t", (char *)"height", NULL, (void *)&_sizey);
  // a pointer, not an array: show16() swaps the frame buffers
  bindVariable((char *)"uint16_t *", (char *)"led16", NULL, (void *)&frame_buffer);
  bindFunction((char *)"uint16_t", (char *)"hsv2", (char *)"int,int,int", (void *)hsv16);
  bindFunction((char *)"void", (char *)"show16", NULL, (void *)show16);
  bindFunction((char *)"void", (char *)"setPixelsize", (char *)"int", (void *)setPixelsize);
  bindFunction((char *)"uint32_t", (char *)"millis", NULL, (void *)scriptMillis);
  bindFunction((char *)"uint32_t", (char *)"rand", (char *)"uint32_t", (void *)scriptRand);
  bindFunction((char *)"int", (char *)"readKey", NULL, (void *)readKey);
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
  printf("%d games\n", gameCount);
  setPixelsize(2);

  hid_keyboard_print_char = print_char;
  hid_keyboard_release_char = release_char;
  app_main_keyboard();
}

void loop() {
  if (game != NULL) {
    if (gameParked)
      stopGame();
    else
      delay(50);
    return;
  }
  int k = popKey();
  while (k != 0) {
    if ((k == 'z' || k == 'Z') && selected > 0)
      selected--;
    if ((k == 's' || k == 'S') && selected < gameCount - 1)
      selected++;
    if (k >= '1' && k < '1' + gameCount)
      selected = k - '1';
    if ((k == ' ' || k == '\n' || (k >= '1' && k <= '9')) && gameCount > 0) {
      startGame(selected);
      if (game != NULL)
        return;
    }
    k = popKey();
  }
  drawMenu(millis());
  show16();
  delay(30);
}
