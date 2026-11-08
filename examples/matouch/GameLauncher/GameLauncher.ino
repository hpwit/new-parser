// A menu of compiled games, read from the FFat partition: pick one with the
// keyboard, it loads and runs; Escape comes back to the menu. Nothing is
// compiled on the board: each game is a binary made by parseScriptToBinary()
// (see CreateTetrisBinary), so it starts at once.
//
// The data folder next to this sketch holds three: tetris.bin (matouch/Tetris),
// mazechase.bin (matouch/MazeChase) and neonracer.bin (matouch/NeonRacer). Any
// other .bin compiled with the same bindings (below) shows in the menu too.
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

// 3x5 pixel font, 0-9 then A-Z: 5 rows of 3 bits, the top row first
static const uint16_t font[36] = {31599, 11415, 29671, 29647, 23497, 31183, 31215, 29330, 31727, 31695,
                                  11245, 27566, 14627, 27502, 31143, 31140, 14699, 23533, 29847, 4714,
                                  23861, 18727, 24557, 27501, 11114, 27556, 11123, 27565, 14478, 29842,
                                  23407, 23402, 23549, 23213, 23186, 29351};

static void fill(int x, int y, int w, int h, uint16_t c) {
  for (int j = y; j < y + h; j++)
    if (j >= 0 && j < 240)
      for (int i = x; i < x + w; i++)
        if (i >= 0 && i < 400)
          frame_buffer[j * 800 + i] = c;
}

// capitals, digits and spaces (lower case is shown as capitals)
static int text(int x, int y, const char *s, int sc, uint16_t c) {
  for (; *s; s++) {
    int ch = toupper(*s);
    int g = -1;
    if (ch >= '0' && ch <= '9')
      g = ch - '0';
    if (ch >= 'A' && ch <= 'Z')
      g = ch - 'A' + 10;
    if (g >= 0)
      for (int r = 0; r < 5; r++)
        for (int b = 0; b < 3; b++)
          if ((font[g] >> ((4 - r) * 3 + 2 - b)) & 1)
            fill(x + b * sc, y + r * sc, sc, sc, c);
    x += 4 * sc;
  }
  return x;
}

static void drawMenu(uint32_t t) {
  for (int y = 0; y < 240; y++)
    fill(0, y, 400, 1, hsv16(170 + y / 8, 255, 20 + y / 4));
  const char *title = "GAME LAUNCHER";
  for (int i = 0; title[i]; i++) {
    char one[2] = {title[i], 0};
    text(46 + i * 24, 14, one, 5, hsv16(i * 20 + t / 20, 220, 255));
  }
  if (gameCount == 0) {
    text(60, 100, "NO BIN FILE ON THE FFAT PARTITION", 2, hsv16(0, 200, 255));
    text(60, 120, "SEE THE README GAMELAUNCHER", 2, hsv16(0, 0, 200));
  }
  for (int i = 0; i < gameCount; i++) {
    int y = 60 + i * 18;
    bool on = i == selected;
    if (on)
      fill(40, y - 3, 320, 16, hsv16(200 + (t / 10) % 20, 230, 160));
    char line[40];
    snprintf(line, sizeof(line), "%d  %.*s", i + 1, (int)strlen(gameFile[i]) - 4, gameFile[i]);
    text(52, y, line, 2, on ? hsv16(0, 0, 255) : hsv16(150, 60, 220));
    snprintf(line, sizeof(line), "%u", (unsigned)gameSize[i]);
    text(300, y, line, 2, hsv16(45, 150, 220));
  }
  text(40, 212, "Z S CHOOSE   SPACE PLAY   ESC BACK", 2, hsv16(150, 40, 180));
  if (message[0])
    text(40, 196, message, 2, hsv16(0, 220, 255));
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

  // false: a partition that doesn't mount is left alone, never formatted
  if (!FFat.begin(false))
    printf("FFat does not mount: put the data folder on the partition (see the README)\n");
  else
    listGames();
  printf("%d games\n", gameCount);
  setPixelsize(2);

  hid_keyboard_print_char = print_char;
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
