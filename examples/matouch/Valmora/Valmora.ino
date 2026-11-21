// Valmora -- The Sundered Crown, on the ESP32-S3-Touch-LCD-7 (800x480),
// played with a USB keyboard: a top-down adventure in the style of the
// early role-playing games. A world map, three towns to talk in, four
// dungeons to light with a torch, three shards of a broken crown to find and
// a sorcerer to end. See the README's "Valmora".
//
// Every picture (32x32 tiles, each with 16 colours of the 4096), the maps,
// the people and their words come from a WAD file on the FFat partition,
// /valmora.wad (data/valmora.wad, made by tools/make_wad.py). The sketch
// reads it into PSRAM and binds it for the script (wad, wadLoaded,
// wadFind). The script is tools/valmora.sc, put in here by
// tools/make_sketch.py; the matouch GameLauncher and SolarOS run the same
// script (valmora.bin).
//
// Keys
//   arrows or Z Q S D   walk; walk into someone to talk, into a monster to fight
//   M                   Mend: heal (4 magic)
//   F, then a way       Spark: a bolt of fire (3 magic)
//   P                   drink a potion
//   Space               wait a turn
//   Enter               next page, buy, choose; Escape: leave
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

uint32_t scriptRand(uint32_t n) {
  return n ? esp_random() % n : 0;
}

// --- the WAD: read from the FFat partition into PSRAM ---
static uint8_t *wadData = NULL;
static int wadLoaded = 0;

static void loadWad(const char *path) {
  if (!FFat.begin(false)) {
    printf("FFat does not mount: put data/valmora.wad on it (see the README)\n");
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

char valmora[] = R"EOF(// VALMORA - The Sundered Crown. A top-down adventure in the style of the
// early role-playing games: a world map, towns to talk in, dungeons to
// light with a torch, three shards to find and a sorcerer to end.
//
// Runs on the ESP32-S3-Touch-LCD-7 (800x480): examples/matouch/Valmora,
// the matouch GameLauncher (valmora.bin), and SolarOS (asm run valmora.bin).
// valmora.wad, next to it, holds every picture (32x32 tiles, 16 colours
// each out of 4096), the maps, the people and their words:
// examples/matouch/Valmora/tools/make_wad.py builds it, and the same
// folder's valmora.sc is this script.
//
// Keys
//   arrows or Z Q S D   walk; walk into someone to talk, into a monster to fight
//   M                   Mend: heal (4 magic)
//   F, then a way       Spark: a bolt of fire (3 magic)
//   P                   drink a potion
//   Space               wait a turn
//   Enter               next page; Escape leaves

// --- generated by valmora-tools/make_wad.py: do not edit by hand ---
#define T_GRASS0 0
#define T_GRASS1 1
#define T_GRASS2 2
#define T_MEADOW 3
#define T_BRUSH 4
#define T_FOREST 5
#define T_FOREST2 6
#define T_PINES 7
#define T_HILLS 8
#define T_MOUNTAIN 9
#define T_MOUNTAIN2 10
#define T_PEAK 11
#define T_SAND 12
#define T_SHALLOW 13
#define T_DEEP 17
#define T_SWAMP 21
#define T_LAVA 22
#define T_ASH 26
#define T_SHORE1 27
#define T_SHORE2 28
#define T_SHORE3 29
#define T_SHORE4 30
#define T_SHORE5 31
#define T_SHORE6 32
#define T_SHORE7 33
#define T_SHORE8 34
#define T_SHORE9 35
#define T_SHORE10 36
#define T_SHORE11 37
#define T_SHORE12 38
#define T_SHORE13 39
#define T_SHORE14 40
#define T_SHORE15 41
#define T_ROAD0 42
#define T_ROAD1 43
#define T_ROAD2 44
#define T_ROAD3 45
#define T_ROAD4 46
#define T_ROAD5 47
#define T_ROAD6 48
#define T_ROAD7 49
#define T_ROAD8 50
#define T_ROAD9 51
#define T_ROAD10 52
#define T_ROAD11 53
#define T_ROAD12 54
#define T_ROAD13 55
#define T_ROAD14 56
#define T_ROAD15 57
#define T_BRIDGEH 58
#define T_BRIDGEV 59
#define T_VILLAGE 60
#define T_CASTLE 61
#define T_PORT 62
#define T_CAVE 63
#define T_CRYPT 64
#define T_MINE 65
#define T_SPIRE 66
#define T_CAUSEWAY 67
#define T_GATE 68
#define T_GATEOPEN 69
#define T_COBBLE 70
#define T_COBBLE2 71
#define T_WOODFLOOR 72
#define T_STONEFLOOR 73
#define T_CARPET 74
#define T_BRICK 75
#define T_BRICKWIN 76
#define T_BRICKDOOR 77
#define T_STONEWALL 78
#define T_STONEDOOR 79
#define T_TORCH 80
#define T_WOODWALL 82
#define T_WOODWIN 83
#define T_WOODDOOR 84
#define T_COUNTER 85
#define T_TABLE 86
#define T_BED 87
#define T_BARREL 88
#define T_CRATE 89
#define T_SHELF 90
#define T_ANVIL 91
#define T_THRONE 92
#define T_WELL 93
#define T_FOUNTAIN 94
#define T_FLOWERBED 96
#define T_HEDGE 97
#define T_FENCE 98
#define T_TREE 99
#define T_SIGN_WEAPON 100
#define T_SIGN_ARMOR 101
#define T_SIGN_POTION 102
#define T_SIGN_INN 103
#define T_SIGN_HEAL 104
#define T_PILLAR 105
#define T_CAVEFLOOR 106
#define T_CAVEWALL 107
#define T_CRYPTFLOOR 108
#define T_CRYPTWALL 109
#define T_MINEFLOOR 110
#define T_MINEWALL 111
#define T_SPIREFLOOR 112
#define T_SPIREWALL 113
#define T_STAIRS_DOWN 114
#define T_STAIRS_UP 115
#define T_CHEST 116
#define T_CHEST_OPEN 117
#define T_BONES 118
#define T_WEB 119
#define T_RUBBLE 120
#define T_ALTAR 121
#define T_ALTAR_EMPTY 122
#define T_SPR_HERO 123
#define T_SPR_GUARD 125
#define T_SPR_MERCHANT 127
#define T_SPR_MAN 129
#define T_SPR_WOMAN 131
#define T_SPR_KING 133
#define T_SPR_HEALER 135
#define T_SPR_INNKEEPER 137
#define T_SPR_SAGE 139
#define T_SPR_SMITH 141
#define T_SPR_CHILD 143
#define T_SPR_RAT 145
#define T_SPR_BAT 147
#define T_SPR_SLIME 149
#define T_SPR_WOLF 151
#define T_SPR_GOBLIN 153
#define T_SPR_SKELETON 155
#define T_SPR_ORC 157
#define T_SPR_SPIDER 159
#define T_SPR_GHOUL 161
#define T_SPR_WRAITH 163
#define T_SPR_TROLL 165
#define T_SPR_DRAKE 167
#define T_SPR_GOLEM 169
#define T_SPR_DROWNED 171
#define T_SPR_WYRM 173
#define T_SPR_MORVATH 175
#define T_SPR_BANDIT 177
#define T_SPR_BLUESLIME 179
#define T_SPR_GHOST 181
#define T_SPR_HERO_UP 183
#define T_SPR_HERO_LEFT 185
#define T_SPR_HERO_RIGHT 187
#define T_VOID 189
#define T_PILLAR_D 190
#define NTILES 191
#define PAL_WORDS 6112
#define M_RAT 0
#define M_BAT 1
#define M_SLIME 2
#define M_WOLF 3
#define M_GOBLIN 4
#define M_BANDIT 5
#define M_SKELETON 6
#define M_SPIDER 7
#define M_ORC 8
#define M_GHOUL 9
#define M_BLUESLIME 10
#define M_WRAITH 11
#define M_TROLL 12
#define M_DRAKE 13
#define M_GHOST 14
#define M_GOLEM 15
#define M_DROWNED 16
#define M_WYRM 17
#define M_MORVATH 18
#define NMONSTERS 19
int zoneMon[30] = {0, 0, 0, 0, 0, 0, 3, 4, 5, 8, 3, 7, 4, 8, 12, 4, 3, 8, 12, 0, 2, 10, 9, 14, 0, 11, 13, 12, 0, 0};
int zoneLvl[30] = {99, 99, 99, 99, 99, 1, 1, 2, 3, 5, 1, 2, 2, 4, 7, 1, 2, 3, 6, 99, 1, 2, 4, 6, 99, 1, 1, 1, 99, 99};
#define TX_INTRO 4632
#define TX_VICTORY 4948
#define GATE_X 44
#define GATE_Y 18
#define HOME_X 24
#define HOME_Y 60
// --- end of generated ---

#define TS 32
#define VIEWH 480
#define PANELX 608
#define SUBS 2
// a short wait after each tile drawn (and every 8 rows of a fill): the
// writes come in small chunks, so the panel's refresh on SolarOS, which
// copies its lines from PSRAM, is never starved for long. 0: no wait
#define BREATH 300
#define KEY 8
#define MAXE 64
#define MAXO 64
#define LOGN 10
#define LOGW 48
#define LOG_SIZE 480
#define DONE_SIZE 768
#define DAY 300
#define NIGHT 220

// ------------------------------------------------------------ data

uint8_t tprop[256];
uint8_t tanim[256];
uint8_t tflag[256];
int fontOff;
int textOff;
int npcOff;
int dlgOff;
int monOff;
int itemOff;
int logoOff;
int mapOff[12];
int dlgStart[64];

uint8_t mapT[8000];
uint8_t seen[8000];
int mapId;
int mapW;
int mapH;
int mapKind;
int mapFloor;
int mapLight;
int mapName;
int mapExitX;
int mapExitY;
int mapOut;
int objBase;
int nObj;
int oType[MAXO];
int oX[MAXO];
int oY[MAXO];
int oA[MAXO];
int oB[MAXO];
int oC[MAXO];
uint8_t done[DONE_SIZE];

int eUsed[MAXE];
int eType[MAXE];     // 1 person, 2 monster
int eX[MAXE];
int eY[MAXE];
int ePX[MAXE];
int ePY[MAXE];
int eSpr[MAXE];
int eHP[MAXE];
int eMax[MAXE];
int eKind[MAXE];     // monster kind or person index
int eObj[MAXE];
int eHX[MAXE];
int eHY[MAXE];
int eHit[MAXE];

int plX;
int plY;
int plPX;
int plPY;
int hp;
int maxHp;
int mp;
int maxMp;
int str;
int def;
int level;
int xp;
int gold;
int potions;
int weapon;
int armour;
int shards;
int bossDead;
int gateOpen;
int turn;
int worldTurn;
int won;
int quit;
int faceX;
int faceY;
int plHit;

int camLeft;
int camTop;
// Valmora Low: the WAD's tiles are 16x16 (and its font 8 rows): everything
// is laid out in 800x480 coordinates and drawn at half of them, 400x240;
// org: where that picture starts on the panel (centred at pixel size 1)
int half = 0;
int tileSz = 32;
int orgX = 0;
int orgY = 0;
int pixelSize = 1;
int viewW = 608;     // the map view: 608 beside the panel, 800 on the title
int showHero = 1;
int animTick;
uint32_t lastAnim;
int panelDirty;
uint16_t ink[4];

uint8_t lineBuf[120];
int lineLen;
uint8_t logBuf[LOG_SIZE];
uint16_t logCol[LOGN];

uint16_t C_BG;
uint16_t C_BG2;
uint16_t C_EDGE;
uint16_t C_TEXT;
uint16_t C_DIM;
uint16_t C_GOLD;
uint16_t C_RED;
uint16_t C_GREEN;
uint16_t C_CYAN;
uint16_t C_BOX;
uint16_t C_BOXEDGE;
uint16_t C_HP;
uint16_t C_MP;
uint16_t C_XP;
uint16_t C_BAR;
uint16_t C_BLACK;

// ------------------------------------------------------------ little helpers

int rd16(int o)
{
   return wad[o] | (wad[o + 1] << 8);
}

int iabs(int v)
{
   if (v < 0)
      return -v;
   return v;
}

int imin(int a, int b)
{
   if (a < b)
      return a;
   return b;
}

int imax(int a, int b)
{
   if (a > b)
      return a;
   return b;
}

uint16_t col(int r, int g, int b)
{
   int v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
   return ((v & 255) << 8) | (v >> 8);
}

// a mix of two panel colours, t out of 3
uint16_t mix3(uint16_t a, uint16_t b, int t)
{
   int va = ((a & 255) << 8) | (a >> 8);
   int vb = ((b & 255) << 8) | (b >> 8);
   int r = ((va >> 11) * (3 - t) + (vb >> 11) * t) / 3;
   int g = (((va >> 5) & 63) * (3 - t) + ((vb >> 5) & 63) * t) / 3;
   int bl = ((va & 31) * (3 - t) + (vb & 31) * t) / 3;
   int v = (r << 11) | (g << 5) | bl;
   return ((v & 255) << 8) | (v >> 8);
}

// the same colour at 45%
uint16_t dimmed(uint16_t c)
{
   int v = ((c & 255) << 8) | (c >> 8);
   int r = (v >> 11) * 45 / 100;
   int g = ((v >> 5) & 63) * 45 / 100;
   int b = (v & 31) * 45 / 100;
   v = (r << 11) | (g << 5) | b;
   int s = ((v & 255) << 8) | (v >> 8);
   if (s == KEY)
      s = 0;
   return s;
}

// busy for a moment without touching PSRAM (locals only)
int breathe(int n)
{
   int a = 0;
   for (int i = 0; i < n; i++)
      a = a + i;
   return a;
}

void fillRect(int x, int y, int w, int h, uint16_t c)
{
   if (half == 1)
   {
      int x1 = (x + w) >> 1;
      int y1 = (y + h) >> 1;
      x = x >> 1;
      y = y >> 1;
      w = x1 - x;
      h = y1 - y;
      if (w <= 0)
         w = 1;
      if (h <= 0)
         h = 1;
   }
   for (int j = 0; j < h; j++)
   {
      int o = (y + j + orgY) * stride + x + orgX;
      for (int i = 0; i < w; i++)
         display[o + i] = c;
      if ((j & 7) == 7)
         breathe(BREATH);
   }
}

void frameRect(int x, int y, int w, int h, uint16_t c)
{
   fillRect(x, y, w, 2, c);
   fillRect(x, y + h - 2, w, 2, c);
   fillRect(x, y, 2, h, c);
   fillRect(x + w - 2, y, 2, h, c);
}

// ------------------------------------------------------------ text

void setInk(uint16_t fg, uint16_t bg)
{
   ink[0] = bg;
   ink[1] = mix3(bg, fg, 1);
   ink[2] = mix3(bg, fg, 2);
   ink[3] = fg;
}

// a character's width in 800x480 coordinates (Valmora Low's font is half)
int glyphW(int c)
{
   if (c < 32 || c > 126)
      return 0;
   return wad[fontOff + 3 + (c - 32) * 3] << half;
}

int drawChar(int c, int x, int y)
{
   if (c < 32 || c > 126)
      return x;
   int g = fontOff + 3 + (c - 32) * 3;
   int w = wad[g];
   int o = fontOff + rd16(g + 1);
   int bpr = (w + 3) >> 2;
   int rows = wad[fontOff];
   int gx = (x >> half) + orgX;
   int gy = (y >> half) + orgY;
   for (int r = 0; r < rows; r++)
   {
      int d = (gy + r) * stride + gx;
      for (int i = 0; i < w; i++)
      {
         int v = (wad[o + (i >> 2)] >> (6 - ((i & 3) << 1))) & 3;
         if (v != 0)
            display[d + i] = ink[v];
      }
      o = o + bpr;
   }
   return x + (w << half);
}

int drawText(char *s, int x, int y)
{
   int i = 0;
   int ch = s[i];
   while (ch != 0)
   {
      x = drawChar(ch, x, y);
      i++;
      ch = s[i];
   }
   return x;
}

int textW(char *s)
{
   int i = 0;
   int w = 0;
   int ch = s[i];
   while (ch != 0)
   {
      w = w + glyphW(ch);
      i++;
      ch = s[i];
   }
   return w;
}

// text from the WAD (an offset in TEXT), to the end of its line
int drawWad(int off, int x, int y)
{
   int o = textOff + off;
   while (wad[o] != 0 && wad[o] != 10 && wad[o] != 12)
   {
      x = drawChar(wad[o], x, y);
      o++;
   }
   return x;
}

int wadW(int off)
{
   int o = textOff + off;
   int w = 0;
   while (wad[o] != 0 && wad[o] != 10 && wad[o] != 12)
   {
      w = w + glyphW(wad[o]);
      o++;
   }
   return w;
}

int digits[12];

int drawNum(int v, int x, int y)
{
   if (v < 0)
   {
      x = drawChar(45, x, y);
      v = -v;
   }
   int n = 0;
   if (v == 0)
   {
      digits[0] = 0;
      n = 1;
   }
   while (v > 0)
   {
      digits[n] = v % 10;
      v = v / 10;
      n++;
   }
   for (int i = n - 1; i >= 0; i--)
      x = drawChar(48 + digits[i], x, y);
   return x;
}

int numW(int v)
{
   int w = glyphW(48);
   while (v >= 10)
   {
      w = w + glyphW(48);
      v = v / 10;
   }
   return w;
}

// a page of WAD text: lines until \f; returns where the next page starts, or -1
int drawPage(int off, int x, int y)
{
   int o = textOff + off;
   int cx = x;
   while (wad[o] != 0 && wad[o] != 12)
   {
      if (wad[o] == 10)
      {
         cx = x;
         y = y + 18;
      }
      else
         cx = drawChar(wad[o], cx, y);
      o++;
   }
   if (wad[o] == 12)
      return o + 1 - textOff;
   return -1;
}

// ------------------------------------------------------------ the message log

void lineReset()
{
   lineLen = 0;
   lineBuf[0] = 0;
}

void lineStr(char *s)
{
   int i = 0;
   int ch = s[i];
   while (ch != 0 && lineLen < 110)
   {
      lineBuf[lineLen] = ch;
      lineLen++;
      i++;
      ch = s[i];
   }
   lineBuf[lineLen] = 0;
}

void lineWad(int off)
{
   int o = textOff + off;
   while (wad[o] != 0 && lineLen < 110)
   {
      lineBuf[lineLen] = wad[o];
      lineLen++;
      o++;
   }
   lineBuf[lineLen] = 0;
}

void lineNum(int v)
{
   int n = 0;
   if (v == 0)
   {
      digits[0] = 0;
      n = 1;
   }
   while (v > 0)
   {
      digits[n] = v % 10;
      v = v / 10;
      n++;
   }
   for (int i = n - 1; i >= 0; i--)
   {
      lineBuf[lineLen] = 48 + digits[i];
      lineLen++;
   }
   lineBuf[lineLen] = 0;
}

void logLine(int start, int stop, uint16_t c)
{
   for (int i = 0; i < LOGN - 1; i++)
   {
      for (int k = 0; k < LOGW; k++)
         logBuf[i * LOGW + k] = logBuf[(i + 1) * LOGW + k];
      logCol[i] = logCol[i + 1];
   }
   int n = 0;
   for (int k = start; k < stop && n < LOGW - 1; k++)
   {
      logBuf[(LOGN - 1) * LOGW + n] = lineBuf[k];
      n++;
   }
   logBuf[(LOGN - 1) * LOGW + n] = 0;
   logCol[LOGN - 1] = c;
}

// the line built with lineStr()... goes stop the log, wrapped stop the panel
void logPush(uint16_t c)
{
   if (lineBuf[0] >= 97 && lineBuf[0] <= 122)
      lineBuf[0] = lineBuf[0] - 32;
   int start = 0;
   while (start < lineLen)
   {
      int w = 0;
      int k = start;
      int cut = -1;
      while (k < lineLen && w + glyphW(lineBuf[k]) <= 176)
      {
         w = w + glyphW(lineBuf[k]);
         if (lineBuf[k] == 32)
            cut = k;
         k++;
      }
      if (k >= lineLen)
      {
         logLine(start, lineLen, c);
         start = lineLen;
      }
      else
      {
         if (cut <= start)
            cut = k;
         logLine(start, cut, c);
         start = cut;
         while (start < lineLen && lineBuf[start] == 32)
            start++;
      }
   }
   panelDirty = 2;
}

void say(char *s, uint16_t c)
{
   lineReset();
   lineStr(s);
   logPush(c);
}

void clearLog()
{
   for (int i = 0; i < LOGN * LOGW; i++)
      logBuf[i] = 0;
}

// ------------------------------------------------------------ tiles

uint16_t pal[16];          // the logo's colours
// each tile's 16 colours at full light, then dimmed; its pixels stay in the
// WAD, 4 bits each: drawing reads a quarter of what a decoded copy would,
// which leaves the PSRAM bus (and its cache) to the panel's refresh
uint16_t tpal[PAL_WORDS];
int tpix[256];

void decodeTiles()
{
   int o = wadFind("TILES");
   int n = rd16(o);
   o = o + 2;
   tileSz = wad[o + 1];
   if (tileSz == 0)
      tileSz = 32;
   int bytes = 34 + tileSz * tileSz / 2;
   for (int t = 0; t < n; t++)
   {
      tflag[t] = wad[o];
      for (int i = 0; i < 16; i++)
      {
         uint16_t c = rd16(o + 2 + i * 2);
         tpal[t * 32 + i] = c;
         tpal[t * 32 + 16 + i] = dimmed(c);
      }
      tpix[t] = o + 34;
      o = o + bytes;
   }
   int pr = wadFind("TPROP");
   int an = wadFind("TANIM");
   for (int t = 0; t < n; t++)
   {
      tprop[t] = wad[pr + t];
      tanim[t] = wad[an + t];
   }
}

// tile t at (sx, sy) on the map view, cut at its edges; dim 1 for the
// dimmed colours. key 1: colour 0 is left out (creatures, overlays).
// dark 1: every other pixel black (remembered, out of the light)
void blitTile(int t, int dim, int sx, int sy, int key, int dark)
{
   // on the panel: at half of the 800x480 coordinates in Valmora Low
   int tp = tileSz;
   int limW = viewW >> half;
   int limH = VIEWH >> half;
   sx = sx >> half;
   sy = sy >> half;
   int x0 = 0;
   int x1 = tp;
   int y0 = 0;
   int y1 = tp;
   if (sx < 0)
      x0 = -sx;
   if (sx + tp > limW)
      x1 = limW - sx;
   if (sy < 0)
      y0 = -sy;
   if (sy + tp > limH)
      y1 = limH - sy;
   if (x0 >= x1 || y0 >= y1)
      return;
   int pb = t * 32 + dim * 16;
   int rowBytes = tp >> 1;
   for (int y = y0; y < y1; y++)
   {
      int d = (sy + y + orgY) * stride + sx + orgX;
      int p = tpix[t] + y * rowBytes;
      if (x0 == 0 && x1 == tp && key == 0 && dark == 0)
      {
         // the whole row: two pixels a byte
         for (int i = 0; i < rowBytes; i++)
         {
            int b = wad[p + i];
            display[d] = tpal[pb + (b >> 4)];
            display[d + 1] = tpal[pb + (b & 15)];
            d = d + 2;
         }
      }
      else
      {
         for (int x = x0; x < x1; x++)
         {
            int b = wad[p + (x >> 1)];
            if ((x & 1) == 0)
               b = b >> 4;
            else
               b = b & 15;
            if (dark == 1 && ((x + y) & 1) == 1)
               display[d + x] = 0;
            else if (key == 0 || b != 0)
               display[d + x] = tpal[pb + b];
         }
      }
   }
   breathe(BREATH);
}

// a whole tile at twice its size, anywhere on screen, colour 0 left out
void blit2x(int t, int sx, int sy)
{
   int pb = t * 32;
   int tp = tileSz;
   int rowBytes = tp >> 1;
   sx = (sx >> half) + orgX;
   sy = (sy >> half) + orgY;
   for (int y = 0; y < tp * 2; y++)
   {
      int d = (sy + y) * stride + sx;
      int p = tpix[t] + (y >> 1) * rowBytes;
      for (int x = 0; x < tp * 2; x++)
      {
         int b = wad[p + (x >> 2)];
         if (((x >> 1) & 1) == 0)
            b = b >> 4;
         else
            b = b & 15;
         if (b != 0 || tflag[t] == 0)
            display[d + x] = tpal[pb + b];
      }
   }
}

int frameOf(int t, int x, int y)
{
   int f = tanim[t];
   if (f > 1)
      return t + (animTick + ((x * 3 + y * 5) & 3)) % f;
   return t;
}

// ------------------------------------------------------------ the map

int cell(int x, int y)
{
   if (x < 0 || y < 0 || x >= mapW || y >= mapH)
      return mapOut;
   return mapT[y * mapW + x];
}

int blocked(int x, int y)
{
   if (x < 0 || y < 0 || x >= mapW || y >= mapH)
   {
      if (mapKind == 1)
         return 0;
      return 1;
   }
   int t = mapT[y * mapW + x];
   if (t == T_GATE && gateOpen == 1)
      return 0;
   return tprop[t] & 1;
}

int entityAt(int x, int y)
{
   for (int i = 0; i < MAXE; i++)
      if (eUsed[i] == 1 && eX[i] == x && eY[i] == y)
         return i;
   return -1;
}

int objAt(int x, int y)
{
   for (int i = 0; i < nObj; i++)
      if (oX[i] == x && oY[i] == y)
         return i;
   return -1;
}

int isNight()
{
   if (mapKind != 0)
      return 0;
   if (worldTurn % DAY >= NIGHT)
      return 1;
   return 0;
}

// 2 full light, 1 dim, 0 remembered, -1 unseen
int lightAt(int x, int y)
{
   int dx = x - plX;
   int dy = y - plY;
   int d2 = dx * dx + dy * dy;
   if (mapKind == 2)
   {
      int r = mapLight;
      if (d2 <= (r - 2) * (r - 2))
         return 2;
      if (d2 <= r * r + r)
         return 1;
      if (x >= 0 && y >= 0 && x < mapW && y < mapH && seen[y * mapW + x] == 1)
         return 0;
      return -1;
   }
   if (isNight() == 1)
   {
      if (d2 <= 10)
         return 2;
      int t = cell(x, y);
      if ((tprop[t] & 8) != 0 || t == T_VILLAGE || t == T_CASTLE || t == T_PORT)
         return 2;
      return 1;
   }
   return 2;
}

void markSeen()
{
   if (mapKind != 2)
      return;
   int r = mapLight;
   for (int y = plY - r; y <= plY + r; y++)
      for (int x = plX - r; x <= plX + r; x++)
         if (x >= 0 && y >= 0 && x < mapW && y < mapH)
         {
            int dx = x - plX;
            int dy = y - plY;
            if (dx * dx + dy * dy <= r * r + r)
               seen[y * mapW + x] = 1;
         }
}

void drawCell(int x, int y, int sx, int sy)
{
   int t = cell(x, y);
   int lv = lightAt(x, y);
   if (lv < 0 || t == T_VOID)
   {
      fillRect(imax(sx, 0), imax(sy, 0), imin(sx + TS, viewW) - imax(sx, 0), imin(sy + TS, VIEWH) - imax(sy, 0),
               C_BLACK);
      return;
   }
   if (t == T_GATE && gateOpen == 1)
      t = T_GATEOPEN;
   int dim = 0;
   if (lv < 2)
      dim = 1;
   int dark = 0;
   if (lv == 0)
      dark = 1;
   if (tflag[t] == 1)
   {
      blitTile(mapFloor, dim, sx, sy, 0, dark);
      if (lv > 0)
         blitTile(t, dim, sx, sy, 1, 0);
      return;
   }
   blitTile(frameOf(t, x, y), dim, sx, sy, 0, dark);
}


void drawBar(int x, int y, int w, int v, int m, uint16_t c)
{
   fillRect(x, y, w, 8, C_BAR);
   if (m > 0 && v > 0)
      fillRect(x + 1, y + 1, (w - 2) * imin(v, m) / m, 6, c);
}

// the hero turned the way of the last step (or bump, or spark)
int heroSprite()
{
   if (faceY < 0)
      return T_SPR_HERO_UP;
   if (faceX < 0)
      return T_SPR_HERO_LEFT;
   if (faceX > 0)
      return T_SPR_HERO_RIGHT;
   return T_SPR_HERO;
}

// the view, the moves of this turn px pixels along (0..TS)
void drawView(int px)
{
   int cx = plPX * TS + (plX - plPX) * px;
   int cy = plPY * TS + (plY - plPY) * px;
   camLeft = cx - 9 * TS;
   camTop = cy - 7 * TS;
   int tx0 = camLeft >> 5;
   int ty0 = camTop >> 5;
   int ox = tx0 * TS - camLeft;
   int oy = ty0 * TS - camTop;
   int cols = viewW / TS + 2;
   for (int j = 0; j < 16; j++)
   {
      for (int i = 0; i < cols; i++)
         drawCell(tx0 + i, ty0 + j, ox + i * TS, oy + j * TS);
      // a breath after every row: on SolarOS the panel's refresh copies its
      // lines from PSRAM through the same cache the tiles are read through,
      // and falls behind (the picture shakes) when a redraw hogs it
      delay(1);
   }
   int fr = (animTick >> 1) & 1;
   for (int i = 0; i < MAXE; i++)
      if (eUsed[i] == 1)
      {
         int lv = lightAt(eX[i], eY[i]);
         if (lv < 1)
            continue;
         int sx = ePX[i] * TS + (eX[i] - ePX[i]) * px - camLeft;
         int sy = ePY[i] * TS + (eY[i] - ePY[i]) * px - camTop;
         if (sx <= -TS || sy <= -TS || sx >= viewW || sy >= VIEWH)
            continue;
         int dim = 0;
         if (lv == 1 || eHit[i] > 0)
            dim = 1;
         blitTile(eSpr[i] + fr, dim, sx, sy, 1, 0);
         if (eType[i] == 2 && eHP[i] < eMax[i] && sy >= 4 && sx >= 2 && sx + 30 <= viewW)
            drawBar(sx + 2, sy - 4, 28, eHP[i], eMax[i], C_RED);
      }
   if (showHero == 0)
      return;
   int pdim = 0;
   if (plHit > 0)
      pdim = 1;
   blitTile(heroSprite() + fr, pdim, 9 * TS, 7 * TS, 1, 0);
}

// ------------------------------------------------------------ the panel

void drawShard(int x, int y, int have, uint16_t c)
{
   for (int r = 0; r < 14; r++)
   {
      int w = r;
      if (r > 7)
         w = 14 - r;
      uint16_t cc = C_BAR;
      if (have == 1)
         cc = c;
      fillRect(x + 7 - w, y + r, w * 2 + 1, 1, cc);
   }
}

// experience to reach the level after lv
int xpFor(int lv)
{
   if (lv <= 0)
      return 0;
   return lv * lv * 20 + lv * 10;
}

void drawPanel()
{
   fillRect(PANELX, 0, 192, 480, C_BG);
   fillRect(PANELX, 0, 3, 480, C_EDGE);
   fillRect(616, 8, 68, 68, C_BG2);
   frameRect(616, 8, 68, 68, C_EDGE);
   blit2x(T_SPR_HERO, 618, 10);
   setInk(C_GOLD, C_BG);
   drawNum(level, drawText("Level ", 696, 14), 14);
   setInk(C_DIM, C_BG);
   drawNum(xp, drawText("XP ", 696, 34), 34);
   drawBar(696, 56, 92, xp - xpFor(level - 1), xpFor(level) - xpFor(level - 1), C_XP);
   setInk(C_TEXT, C_BG);
   drawText("Health", 620, 84);
   int x = drawNum(hp, 700, 84);
   x = drawChar(47, x, 84);
   drawNum(maxHp, x, 84);
   drawBar(620, 102, 168, hp, maxHp, C_HP);
   drawText("Magic", 620, 114);
   x = drawNum(mp, 700, 114);
   x = drawChar(47, x, 114);
   drawNum(maxMp, x, 114);
   drawBar(620, 132, 168, mp, maxMp, C_MP);
   setInk(C_GOLD, C_BG);
   drawText("Gold", 620, 146);
   drawNum(gold, 700, 146);
   setInk(C_TEXT, C_BG);
   drawText("Potions", 620, 164);
   drawNum(potions, 700, 164);
   drawWad(rd16(itemOff + weapon * 5), 620, 182);
   drawWad(rd16(itemOff + (5 + armour) * 5), 620, 200);
   setInk(C_DIM, C_BG);
   drawText("Shards", 620, 222);
   drawShard(690, 223, shards & 1, col(120, 230, 90));
   drawShard(712, 223, (shards >> 1) & 1, col(80, 200, 255));
   drawShard(734, 223, (shards >> 2) & 1, col(255, 140, 40));
   fillRect(616, 246, 176, 2, C_EDGE);
   setInk(C_GOLD, C_BG);
   x = drawWad(mapName, 620, 252);
   if (mapKind == 0)
   {
      setInk(C_DIM, C_BG);
      if (isNight() == 1)
         drawText("night", 750, 252);
      else
         drawText("day", 760, 252);
   }
   for (int i = 0; i < LOGN; i++)
   {
      setInk(logCol[i], C_BG);
      int cx = 620;
      int k = 0;
      while (logBuf[i * LOGW + k] != 0)
      {
         cx = drawChar(logBuf[i * LOGW + k], cx, 274 + i * 16);
         k++;
      }
   }
   fillRect(616, 436, 176, 1, C_EDGE);
   setInk(C_DIM, C_BG);
   drawText("M mend  F spark", 614, 440);
   drawText("P potion Space wait", 614, 458);
}

// ------------------------------------------------------------ frames

void showFrame(int sub)
{
   drawView(sub * TS / SUBS);
   if (panelDirty > 0)
   {
      drawPanel();
      panelDirty--;
   }
}

void tick()
{
   if (millis() - lastAnim >= 220)
   {
      lastAnim = millis();
      animTick++;
   }
}

void present()
{
   updateDisplay();
}

// the next key: Enter as 13 whether the keyboard gives a carriage return or
// a new line (SolarOS passes the keyboard's own character)
int getKey()
{
   int k = readKey();
   if (k == 10)
      k = 13;
   return k;
}

int drawnTick = -1;

// 1 when the picture must be drawn again: forced, the animation moved on,
// or the panel is behind; else a 10 ms wait, so the other tasks run and the
// panel's refresh keeps its share of the PSRAM bus
int frameDue(int force)
{
   tick();
   if (force == 1 || animTick != drawnTick || panelDirty > 0)
   {
      drawnTick = animTick;
      return 1;
   }
   delay(10);
   return 0;
}

// a box over the map: drawn when it opens and after each key, the world
// behind it still; else a 10 ms wait
int boxDue(int force)
{
   if (force == 1)
      return 1;
   delay(10);
   return 0;
}

// the moves of this turn, in SUBS steps
void animate()
{
   int moved = 0;
   if (plX != plPX || plY != plPY)
      moved = 1;
   for (int i = 0; i < MAXE; i++)
      if (eUsed[i] == 1 && (eX[i] != ePX[i] || eY[i] != ePY[i]))
         moved = 1;
   if (moved == 1)
      for (int s = 1; s < SUBS; s++)
      {
         tick();
         showFrame(s);
         present();
      }
   plPX = plX;
   plPY = plY;
   for (int i = 0; i < MAXE; i++)
   {
      ePX[i] = eX[i];
      ePY[i] = eY[i];
   }
   markSeen();
}

// ------------------------------------------------------------ boxes

void box(int x, int y, int w, int h)
{
   fillRect(x, y, w, h, C_BOX);
   frameRect(x, y, w, h, C_BOXEDGE);
   frameRect(x + 3, y + 3, w - 6, h - 6, C_EDGE);
}

// a box over the map view with a speaker, one page of text
void talkBox(int spr, int name, int page, int more)
{
   box(12, 302, 584, 170);
   if (spr >= 0)
   {
      fillRect(24, 316, 68, 68, C_BG2);
      frameRect(24, 316, 68, 68, C_EDGE);
      blit2x(spr, 26, 318);
   }
   setInk(C_GOLD, C_BOX);
   if (name >= 0)
      drawWad(name, 108, 312);
   setInk(C_TEXT, C_BOX);
   drawPage(page, 108, 334);
   setInk(C_DIM, C_BOX);
   if (more == 1)
      drawText("Enter: more", 496, 448);
   else
      drawText("Enter", 540, 448);
}

// every page of a text, Enter between them
void showPages(int spr, int name, int off)
{
   int page = off;
   while (page >= 0)
   {
      int next = -1;
      int o = textOff + page;
      while (wad[o] != 0 && wad[o] != 12)
         o++;
      if (wad[o] == 12)
         next = o + 1 - textOff;
      int more = 0;
      if (next >= 0)
         more = 1;
      int redraw = 1;
      int k = 0;
      while (k != 13 && k != 32 && k != 27)
      {
         if (boxDue(redraw) == 1)
         {
            showFrame(SUBS);
            talkBox(spr, name, page, more);
            present();
            redraw = 0;
         }
         k = getKey();
         if (k != 0)
            redraw = 1;
      }
      if (k == 27)
         return;
      page = next;
   }
}

// one line in the talk box, Enter or any key closes it
void notice(int spr, int name, char *s)
{
   int redraw = 1;
   int k = 0;
   while (k == 0 || k >= 128)
   {
      if (boxDue(redraw) == 1)
      {
         showFrame(SUBS);
         box(12, 302, 584, 170);
         if (spr >= 0)
         {
            fillRect(24, 316, 68, 68, C_BG2);
            frameRect(24, 316, 68, 68, C_EDGE);
            blit2x(spr, 26, 318);
         }
         setInk(C_GOLD, C_BOX);
         if (name >= 0)
            drawWad(name, 108, 312);
         setInk(C_TEXT, C_BOX);
         drawText(s, 108, 334);
         present();
         redraw = 0;
      }
      k = getKey();
      if (k != 0)
         redraw = 1;
   }
}

// a question; price > 0 is shown under it
int askYes(int spr, int name, char *s, int price)
{
   int redraw = 1;
   int k = 0;
   while (k != 121 && k != 110 && k != 27 && k != 13)
   {
      if (boxDue(redraw) == 1)
      {
         showFrame(SUBS);
         box(12, 302, 584, 170);
         if (spr >= 0)
         {
            fillRect(24, 316, 68, 68, C_BG2);
            frameRect(24, 316, 68, 68, C_EDGE);
            blit2x(spr, 26, 318);
         }
         setInk(C_GOLD, C_BOX);
         if (name >= 0)
            drawWad(name, 108, 312);
         setInk(C_TEXT, C_BOX);
         drawText(s, 108, 334);
         if (price > 0)
         {
            setInk(C_GOLD, C_BOX);
            int x = drawNum(price, 108, 360);
            drawText(" gold", x, 360);
         }
         setInk(C_DIM, C_BOX);
         drawText("Y yes    N no", 108, 440);
         present();
         redraw = 0;
      }
      k = getKey();
      if (k != 0)
         redraw = 1;
   }
   if (k == 121 || k == 13)
      return 1;
   return 0;
}

// ------------------------------------------------------------ the hero

void levelUp()
{
   while (xp >= xpFor(level))
   {
      level++;
      maxHp = maxHp + 9;
      maxMp = maxMp + 4;
      str = str + 2;
      def = def + 1;
      hp = maxHp;
      mp = maxMp;
      lineReset();
      lineStr("You reach level ");
      lineNum(level);
      lineStr("!");
      logPush(C_GOLD);
   }
}

int monAt(int k)
{
   return monOff + 1 + k * 16;
}

void heal(int n, char *what)
{
   int before = hp;
   hp = imin(maxHp, hp + n);
   lineReset();
   lineStr(what);
   lineStr(": +");
   lineNum(hp - before);
   lineStr(" health.");
   logPush(C_GREEN);
}

void killEntity(int i)
{
   int m = monAt(eKind[i]);
   int gx = rd16(m + 5);
   int gg = rd16(m + 7);
   lineReset();
   lineStr("You defeat ");
   lineWad(rd16(m + 9));
   lineStr(". +");
   lineNum(gx);
   lineStr(" XP");
   if (gg > 0)
   {
      int g = gg / 2 + rand(gg / 2 + 1);
      gold = gold + g;
      lineStr(", ");
      lineNum(g);
      lineStr(" gold");
   }
   lineStr(".");
   logPush(C_GOLD);
   xp = xp + gx;
   eUsed[i] = 0;
   if (eObj[i] >= 0 && oType[eObj[i]] == 4)
   {
      done[mapId * MAXO + eObj[i]] = 1;
      bossDead = bossDead | (1 << oB[eObj[i]]);
      if (oB[eObj[i]] == 3)
         won = 1;
      else
         say("A shard glows on the altar.", C_CYAN);
   }
   levelUp();
}

void hurtMonster(int i, int dmg)
{
   eHP[i] = eHP[i] - dmg;
   eHit[i] = 2;
   if (eHP[i] <= 0)
      killEntity(i);
}

void attack(int i)
{
   int m = monAt(eKind[i]);
   int mdef = wad[m + 4];
   int chance = 82 + level * 2 - mdef * 2;
   if (rand(100) >= chance)
   {
      lineReset();
      lineStr("You miss ");
      lineWad(rd16(m + 9));
      lineStr(".");
      logPush(C_DIM);
      return;
   }
   int pw = wad[itemOff + weapon * 5 + 2];
   int dmg = pw + str / 2 + rand(str / 2 + 3) - mdef;
   int crit = 0;
   if (rand(10) == 0)
   {
      dmg = dmg * 2;
      crit = 1;
   }
   if (dmg < 1)
      dmg = 1;
   lineReset();
   if (crit == 1)
      lineStr("A mighty blow! ");
   lineStr("You hit ");
   lineWad(rd16(m + 9));
   lineStr(" for ");
   lineNum(dmg);
   lineStr(".");
   logPush(C_TEXT);
   hurtMonster(i, dmg);
}

void monsterAttack(int i)
{
   int m = monAt(eKind[i]);
   int matk = wad[m + 3];
   int ap = wad[itemOff + (5 + armour) * 5 + 2];
   int chance = 70 + matk - (def + ap) * 2;
   if (chance > 95)
      chance = 95;
   if (chance < 30)
      chance = 30;
   if (rand(100) >= chance)
   {
      lineReset();
      lineWad(rd16(m + 9));
      lineStr(" misses.");
      logPush(C_DIM);
      return;
   }
   int dmg = matk / 2 + rand(matk / 2 + 1) - def / 2 - ap;
   if (dmg < 1)
      dmg = 1;
   hp = hp - dmg;
   plHit = 2;
   lineReset();
   lineWad(rd16(m + 9));
   lineStr(" hits you for ");
   lineNum(dmg);
   lineStr(".");
   logPush(C_RED);
}

// ------------------------------------------------------------ entities

int addEntity(int type, int x, int y, int spr)
{
   for (int i = 0; i < MAXE; i++)
      if (eUsed[i] == 0)
      {
         eUsed[i] = 1;
         eType[i] = type;
         eX[i] = x;
         eY[i] = y;
         ePX[i] = x;
         ePY[i] = y;
         eHX[i] = x;
         eHY[i] = y;
         eSpr[i] = spr;
         eObj[i] = -1;
         eHit[i] = 0;
         return i;
      }
   return -1;
}

int addMonster(int k, int x, int y)
{
   int m = monAt(k);
   int i = addEntity(2, x, y, wad[m]);
   if (i >= 0)
   {
      eKind[i] = k;
      eHP[i] = rd16(m + 1);
      eMax[i] = eHP[i];
   }
   return i;
}

int monsterCount()
{
   int n = 0;
   for (int i = 0; i < MAXE; i++)
      if (eUsed[i] == 1 && eType[i] == 2)
         n++;
   return n;
}

int isFree(int x, int y)
{
   if (blocked(x, y) == 1)
      return 0;
   if (x < 0 || y < 0 || x >= mapW || y >= mapH)
      return 0;
   if (x == plX && y == plY)
      return 0;
   if (entityAt(x, y) >= 0)
      return 0;
   return 1;
}

// where a creature may step: no doors to places for monsters
int canStep(int i, int x, int y)
{
   if (isFree(x, y) == 0)
      return 0;
   if (mapKind == 0)
   {
      int t = cell(x, y);
      if (objAt(x, y) >= 0 || (tprop[t] & 4) != 0 || t == T_CAUSEWAY)
         return 0;
   }
   if (mapKind == 2 && objAt(x, y) >= 0 && oType[objAt(x, y)] == 3)
      return 0;
   if (eType[i] == 1 && (iabs(x - eHX[i]) > 3 || iabs(y - eHY[i]) > 3))
      return 0;
   return 1;
}

void stepToward(int i)
{
   int dx = plX - eX[i];
   int dy = plY - eY[i];
   int sx = 0;
   int sy = 0;
   if (dx > 0)
      sx = 1;
   if (dx < 0)
      sx = -1;
   if (dy > 0)
      sy = 1;
   if (dy < 0)
      sy = -1;
   if (iabs(dx) >= iabs(dy))
   {
      if (sx != 0 && canStep(i, eX[i] + sx, eY[i]) == 1)
         eX[i] = eX[i] + sx;
      else if (sy != 0 && canStep(i, eX[i], eY[i] + sy) == 1)
         eY[i] = eY[i] + sy;
   }
   else
   {
      if (sy != 0 && canStep(i, eX[i], eY[i] + sy) == 1)
         eY[i] = eY[i] + sy;
      else if (sx != 0 && canStep(i, eX[i] + sx, eY[i]) == 1)
         eX[i] = eX[i] + sx;
   }
}

void wander(int i)
{
   int d = rand(4);
   int x = eX[i];
   int y = eY[i];
   if (d == 0)
      x++;
   if (d == 1)
      x--;
   if (d == 2)
      y++;
   if (d == 3)
      y--;
   if (canStep(i, x, y) == 1)
   {
      eX[i] = x;
      eY[i] = y;
   }
}

void spawnWorld()
{
   if (mapKind != 0 || monsterCount() >= 5 || rand(9) != 0)
      return;
   int x = plX + rand(23) - 11;
   int y = plY + rand(17) - 8;
   if (iabs(x - plX) < 7 && iabs(y - plY) < 6)
      return;
   if (x < 1 || y < 1 || x >= mapW - 1 || y >= mapH - 1)
      return;
   int t = cell(x, y);
   int z = tprop[t] >> 4;
   if (z == 0 || isFree(x, y) == 0 || objAt(x, y) >= 0)
      return;
   int pick = rand(5);
   int tries = 0;
   while (tries < 6 && zoneLvl[z * 5 + pick] > level)
   {
      pick = rand(5);
      tries++;
   }
   if (zoneLvl[z * 5 + pick] > level)
      pick = 0;
   int k = zoneMon[z * 5 + pick];
   if (isNight() == 1 && rand(3) == 0 && level >= 2)
      k = zoneMon[z * 5 + rand(2) + 1];
   addMonster(k, x, y);
}

void monstersAct()
{
   for (int i = 0; i < MAXE; i++)
   {
      if (eUsed[i] == 0)
         continue;
      if (eHit[i] > 0)
         eHit[i] = eHit[i] - 1;
      int dx = iabs(plX - eX[i]);
      int dy = iabs(plY - eY[i]);
      if (eType[i] == 1)
      {
         if (rand(4) == 0 && dx + dy > 1)
         {
            int p = npcOff + 1 + eKind[i] * 12;
            if (wad[p + 5] == 1)
               wander(i);
         }
         continue;
      }
      if (mapKind == 0 && (dx > 15 || dy > 12))
      {
         eUsed[i] = 0;
         continue;
      }
      if (dx + dy == 1)
      {
         if (hp > 0)
            monsterAttack(i);
         continue;
      }
      int sight = 8;
      if (mapKind == 2)
         sight = mapLight + 3;
      if (dx <= sight && dy <= sight)
         stepToward(i);
      else if (rand(3) == 0 && (eObj[i] < 0 || oType[eObj[i]] != 4))
         wander(i);
   }
}

// ------------------------------------------------------------ maps

void loadMap(int id, int x, int y)
{
   int o = mapOff[id];
   mapId = id;
   mapW = wad[o];
   mapH = wad[o + 1];
   mapKind = wad[o + 2];
   mapFloor = wad[o + 3];
   mapLight = wad[o + 4];
   mapName = rd16(o + 5);
   mapExitX = wad[o + 8];
   mapExitY = wad[o + 9];
   nObj = wad[o + 12];
   int n = mapW * mapH;
   for (int i = 0; i < n; i++)
   {
      mapT[i] = wad[o + 16 + i];
      seen[i] = 0;
   }
   mapOut = T_VOID;
   if (mapKind == 0)
      mapOut = T_DEEP;
   if (mapKind == 1)
      mapOut = T_GRASS0;
   int ob = o + 16 + n;
   for (int i = 0; i < MAXE; i++)
      eUsed[i] = 0;
   for (int i = 0; i < nObj; i++)
   {
      oType[i] = wad[ob + i * 6];
      oX[i] = wad[ob + i * 6 + 1];
      oY[i] = wad[ob + i * 6 + 2];
      oA[i] = wad[ob + i * 6 + 3];
      oB[i] = wad[ob + i * 6 + 4];
      oC[i] = wad[ob + i * 6 + 5];
      int at = oY[i] * mapW + oX[i];
      if (oType[i] == 2 && done[id * MAXO + i] == 1)
         mapT[at] = T_CHEST_OPEN;
      if (oType[i] == 5 && ((shards >> oB[i]) & 1) == 1)
         mapT[at] = T_ALTAR_EMPTY;
      if (oType[i] == 1 || (oType[i] == 4 && done[id * MAXO + i] == 0))
      {
         int e = addMonster(oA[i], oX[i], oY[i]);
         if (e >= 0)
            eObj[e] = i;
      }
   }
   int np = wad[npcOff];
   for (int i = 0; i < np; i++)
   {
      int p = npcOff + 1 + i * 12;
      if (wad[p] == id)
      {
         int pe = addEntity(1, wad[p + 1], wad[p + 2], wad[p + 3]);
         if (pe >= 0)
            eKind[pe] = i;
      }
   }
   plX = x;
   plY = y;
   plPX = x;
   plPY = y;
   markSeen();
   panelDirty = 2;
}

void enterPlace(int target)
{
   int o = mapOff[target];
   int x = wad[o + 10];
   int y = wad[o + 11];
   loadMap(target, x, y);
   lineReset();
   lineStr("You enter ");
   lineWad(mapName);
   lineStr(".");
   logPush(C_CYAN);
}

void toWorld(int x, int y)
{
   loadMap(0, x, y);
   say("You are back on the road.", C_CYAN);
}

// ------------------------------------------------------------ people

int dialogText(int d)
{
   int o = dlgStart[d];
   int n = wad[o];
   int best = rd16(o + 2);
   int have = (shards & 1) + ((shards >> 1) & 1) + ((shards >> 2) & 1);
   for (int i = 0; i < n; i++)
      if (wad[o + 1 + i * 3] <= have)
         best = rd16(o + 2 + i * 3);
   return best;
}

int shardCount()
{
   return (shards & 1) + ((shards >> 1) & 1) + ((shards >> 2) & 1);
}

// buying from a list of weapons (kind 0) or armours (kind 1), tiers lo..hi
void shop(int spr, int name, int kind, int lo, int hi)
{
   while (true)
   {
      int redraw = 1;
      int k = 0;
      while (k == 0 || k >= 128)
      {
         if (boxDue(redraw) == 1)
         {
            showFrame(SUBS);
            box(12, 302, 584, 170);
            fillRect(24, 316, 68, 68, C_BG2);
            frameRect(24, 316, 68, 68, C_EDGE);
            blit2x(spr, 26, 318);
            setInk(C_GOLD, C_BOX);
            drawWad(name, 108, 312);
            drawText("Gold:", 440, 312);
            drawNum(gold, 490, 312);
            for (int t = lo; t <= hi; t++)
            {
               int it = itemOff + (kind * 5 + t) * 5;
               int y = 336 + (t - lo) * 20;
               int own = weapon;
               if (kind == 1)
                  own = armour;
               uint16_t c = C_TEXT;
               if (own >= t)
                  c = C_DIM;
               setInk(c, C_BOX);
               drawNum(t - lo + 1, 108, y);
               drawWad(rd16(it), 132, y);
               drawText("+", 290, y);
               drawNum(wad[it + 2], 302, y);
               drawNum(rd16(it + 3), 360, y);
               drawText("gold", 368 + numW(rd16(it + 3)), y);
               if (own == t)
                  drawText("(yours)", 450, y);
            }
            setInk(C_DIM, C_BOX);
            drawText("Number: buy    Enter: leave", 108, 448);
            present();
            redraw = 0;
         }
         k = getKey();
         if (k != 0)
            redraw = 1;
      }
      if (k == 27 || k == 13)
         return;
      int tier = k - 49 + lo;
      if (tier >= lo && tier <= hi)
      {
         int item = itemOff + (kind * 5 + tier) * 5;
         int mine = weapon;
         if (kind == 1)
            mine = armour;
         if (mine >= tier)
            say("You already have as good.", C_DIM);
         else if (gold < rd16(item + 3))
            say("Not enough gold.", C_RED);
         else
         {
            gold = gold - rd16(item + 3);
            if (kind == 0)
               weapon = tier;
            else
               armour = tier;
            lineReset();
            lineStr("You buy the ");
            lineWad(rd16(item));
            lineStr(".");
            logPush(C_GOLD);
         }
         panelDirty = 2;
      }
   }
}

void talkTo(int e)
{
   int p = npcOff + 1 + eKind[e] * 12;
   int spr = wad[p + 3];
   int role = wad[p + 4];
   int name = rd16(p + 6);
   int d = wad[p + 8];
   int p1 = wad[p + 9];
   int p2 = wad[p + 10];
   int had = gateOpen;
   showPages(spr, name, dialogText(d));
   if (role == 1)
      shop(spr, name, 0, p1, p2);
   if (role == 2)
      shop(spr, name, 1, p1, p2);
   if (role == 3)
   {
      while (true)
      {
         if (askYes(spr, name, "Buy a potion?", p1) == 0)
            break;
         if (gold < p1)
         {
            say("Not enough gold.", C_RED);
            break;
         }
         if (potions >= 9)
         {
            say("You can't carry more.", C_DIM);
            break;
         }
         gold = gold - p1;
         potions++;
         say("You buy a potion.", C_GOLD);
         panelDirty = 2;
      }
   }
   if (role == 4)
   {
      if (askYes(spr, name, "A room for the night?", p1) == 1)
      {
         if (gold < p1)
            say("Not enough gold.", C_RED);
         else
         {
            gold = gold - p1;
            hp = maxHp;
            mp = maxMp;
            worldTurn = (worldTurn / DAY + 1) * DAY;
            say("You sleep well, and wake at dawn.", C_GREEN);
         }
      }
   }
   if (role == 5)
   {
      hp = maxHp;
      say("Your wounds close.", C_GREEN);
   }
   if (role == 6 && shardCount() == 3 && had == 0)
   {
      gateOpen = 1;
      say("Far away, the causeway gate opens.", C_CYAN);
   }
   panelDirty = 2;
}

// ------------------------------------------------------------ turns

void die()
{
   notice(T_SPR_HERO, -1, "You fall... and wake in Brindle, poorer.");
   hp = maxHp;
   mp = maxMp;
   gold = gold / 2;
   loadMap(0, HOME_X, HOME_Y + 1);
   say("Sister Ione found you on the road.", C_GREEN);
}

void endTurn()
{
   turn++;
   if (mapKind == 0)
      worldTurn++;
   if (turn % 6 == 0 && mp < maxMp)
      mp++;
   if (turn % 9 == 0 && hp < maxHp && mapKind != 2)
      hp++;
   if (plHit > 0)
      plHit--;
   monstersAct();
   spawnWorld();
   panelDirty = 2;
   animate();
   if (hp <= 0)
      die();
}

void takeObj(int i)
{
   int at = oY[i] * mapW + oX[i];
   if (oType[i] == 2 && done[mapId * MAXO + i] == 0)
   {
      done[mapId * MAXO + i] = 1;
      mapT[at] = T_CHEST_OPEN;
      lineReset();
      if (oA[i] == 0)
      {
         gold = gold + oB[i];
         lineStr("The chest holds ");
         lineNum(oB[i]);
         lineStr(" gold.");
      }
      if (oA[i] == 1)
      {
         potions = imin(9, potions + oB[i]);
         lineStr("The chest holds ");
         lineNum(oB[i]);
         lineStr(" potions.");
      }
      if (oA[i] == 2 || oA[i] == 3)
      {
         int kind = oA[i] - 2;
         int it = itemOff + (kind * 5 + oB[i]) * 5;
         lineStr("You find ");
         lineWad(rd16(it));
         if ((kind == 0 && weapon < oB[i]) || (kind == 1 && armour < oB[i]))
         {
            lineStr(", and take it.");
            if (kind == 0)
               weapon = oB[i];
            else
               armour = oB[i];
         }
         else
            lineStr(": no better than yours.");
      }
      logPush(C_GOLD);
      panelDirty = 2;
   }
   if (oType[i] == 5)
   {
      int b = oB[i];
      if (((shards >> b) & 1) == 1)
         return;
      if (((bossDead >> b) & 1) == 0)
      {
         say("A shard burns on the altar, guarded.", C_CYAN);
         return;
      }
      shards = shards | (1 << b);
      mapT[at] = T_ALTAR_EMPTY;
      say("You take a shard of the Sunstone Crown!", C_GOLD);
      panelDirty = 2;
      if (shardCount() == 3)
         say("Bring the three shards to the King.", C_CYAN);
   }
}

void tryMove(int dx, int dy)
{
   faceX = dx;
   faceY = dy;
   int x = plX + dx;
   int y = plY + dy;
   if (mapKind == 1 && (x < 0 || y < 0 || x >= mapW || y >= mapH))
   {
      toWorld(mapExitX, mapExitY);
      return;
   }
   int e = entityAt(x, y);
   if (e >= 0)
   {
      if (eType[e] == 1)
         talkTo(e);
      else
      {
         attack(e);
         endTurn();
      }
      return;
   }
   if (cell(x, y) == T_COUNTER)
   {
      int e2 = entityAt(x + dx, y + dy);
      if (e2 >= 0 && eType[e2] == 1)
      {
         talkTo(e2);
         return;
      }
   }
   int ob = objAt(x, y);
   if (ob >= 0 && (oType[ob] == 2 || oType[ob] == 5))
   {
      takeObj(ob);
      endTurn();
      return;
   }
   if (blocked(x, y) == 1)
   {
      if (cell(x, y) == T_GATE)
         say("The gate is shut fast by old magic.", C_CYAN);
      return;
   }
   plX = x;
   plY = y;
   if (ob >= 0 && oType[ob] == 6)
   {
      animate();
      enterPlace(oA[ob]);
      return;
   }
   if (ob >= 0 && oType[ob] == 3)
   {
      animate();
      if (oA[ob] == 0)
         toWorld(oB[ob], oC[ob]);
      else
      {
         loadMap(oA[ob], oB[ob], oC[ob]);
         lineReset();
         lineWad(mapName);
         logPush(C_CYAN);
      }
      return;
   }
   if ((tprop[cell(x, y)] & 2) != 0)
   {
      hp = hp - 5;
      say("The ground burns you!", C_RED);
   }
   endTurn();
}

void mend()
{
   if (mp < 4)
   {
      say("Not enough magic.", C_DIM);
      return;
   }
   mp = mp - 4;
   heal(12 + level * 5, "Mend");
   endTurn();
}

void drinkPotion()
{
   if (potions == 0)
   {
      say("You have no potions.", C_DIM);
      return;
   }
   potions--;
   heal(35, "Potion");
   endTurn();
}

// a bolt flying from the hero: a glow drawn over the map
void glow(int sx, int sy)
{
   int r = 9 >> half;
   int limW = viewW >> half;
   int limH = VIEWH >> half;
   sx = sx >> half;
   sy = sy >> half;
   for (int j = -r; j <= r; j++)
      for (int i = -r; i <= r; i++)
      {
         int d = i * i + j * j;
         int x = sx + i;
         int y = sy + j;
         if (d <= r * r && x >= 0 && y >= 0 && x < limW && y < limH)
         {
            uint16_t c = col(255, 120, 30);
            if (d < (30 >> (half * 2)))
               c = col(255, 240, 140);
            else if (d < (55 >> (half * 2)))
               c = col(255, 190, 60);
            display[(y + orgY) * stride + x + orgX] = c;
         }
      }
}

void spark()
{
   if (mp < 3)
   {
      say("Not enough magic.", C_DIM);
      return;
   }
   say("Spark: which way?", C_CYAN);
   int redraw = 1;
   int k = 0;
   while (k != 122 && k != 115 && k != 113 && k != 100 && k != 27)
   {
      if (boxDue(redraw) == 1)
      {
         showFrame(SUBS);
         present();
         redraw = 0;
      }
      k = getKey();
      if (k != 0)
         redraw = 1;
   }
   if (k == 27)
      return;
   int dx = 0;
   int dy = 0;
   if (k == 122)
      dy = -1;
   if (k == 115)
      dy = 1;
   if (k == 113)
      dx = -1;
   if (k == 100)
      dx = 1;
   mp = mp - 3;
   panelDirty = 2;
   faceX = dx;
   faceY = dy;
   int x = plX;
   int y = plY;
   for (int n = 0; n < 8; n++)
   {
      x = x + dx;
      y = y + dy;
      if (blocked(x, y) == 1 && (tprop[cell(x, y)] & 4) == 0)
         break;
      for (int s = 0; s < 3; s++)
      {
         tick();
         showFrame(SUBS);
         int px = (x - dx) * TS + dx * TS * (s + 1) / 3 - camLeft + 16;
         int py = (y - dy) * TS + dy * TS * (s + 1) / 3 - camTop + 16;
         glow(px, py);
         present();
      }
      int e = entityAt(x, y);
      if (e >= 0 && eType[e] == 2)
      {
         int dmg = 10 + level * 3 + rand(7);
         int m = monAt(eKind[e]);
         lineReset();
         lineStr("The spark burns ");
         lineWad(rd16(m + 9));
         lineStr(" for ");
         lineNum(dmg);
         lineStr(".");
         logPush(C_CYAN);
         hurtMonster(e, dmg);
         break;
      }
   }
   endTurn();
}

// ------------------------------------------------------------ title

void drawLogo(int cx, int y)
{
   int w = rd16(logoOff);
   int h = rd16(logoOff + 2);
   for (int i = 0; i < 16; i++)
      pal[i] = rd16(logoOff + 4 + i * 2);
   int p = logoOff + 36;
   int x0 = cx - w / 2;
   if (half == 1)
   {
      // every other pixel of every other row: the logo at half its size
      for (int j = 0; j < h; j = j + 2)
      {
         int d = ((y + j) / 2 + orgY) * stride + x0 / 2 + orgX;
         int r = p + j * (w / 2);
         for (int i = 0; i < w; i = i + 2)
         {
            int b = wad[r + i / 2] >> 4;
            if (b != 0)
               display[d + i / 2] = pal[b];
         }
      }
      return;
   }
   for (int j = 0; j < h; j++)
   {
      int d = (y + j) * stride + x0;
      for (int i = 0; i < w; i = i + 2)
      {
         int b = wad[p];
         p++;
         if ((b >> 4) != 0)
            display[d + i] = pal[b >> 4];
         if ((b & 15) != 0)
            display[d + i + 1] = pal[b & 15];
      }
   }
}

void centred(char *s, int y, uint16_t c, uint16_t bg)
{
   setInk(c, bg);
   drawText(s, (800 - textW(s)) / 2, y);
}

// the world behind the name; 1 to play, 0 to leave
int title()
{
   loadMap(0, 30, 40);
   viewW = 800;
   showHero = 0;
   int t0 = millis();
   int shown = -1;
   while (true)
   {
      tick();
      // the world behind the name: drifting a pixel every 40 ms at pixel
      // size 2, with its water moving and the line below blinking; at
      // pixel size 1 (where the panel's refresh needs the PSRAM bus most)
      // nothing moves at all: drawn once, then only waited on
      int blink = 0;
      int t = 0;
      int now = 0;
      if (pixelSize == 2)
      {
         blink = ((millis() - t0) / 600) % 2;
         t = (millis() - t0) / 40;
         now = t * 64 + blink * 32 + (animTick & 31);
      }
      else
         animTick = 0;
      if (now == shown)
      {
         delay(10);
         int k0 = getKey();
         if (k0 == 13 || k0 == 32 || k0 == 27)
         {
            viewW = 608;
            showHero = 1;
            panelDirty = 2;
            if (k0 == 27)
               return 0;
            return 1;
         }
         continue;
      }
      shown = now;
      plX = 30 + (t / TS) % 40;
      plY = 40;
      plPX = plX;
      plPY = plY;
      if (pixelSize == 2)
         plPX = plX - 1;
      drawView(t % TS);
      // the band behind the name
      for (int y = 120 >> half; y < 330 >> half; y++)
      {
         int d = (y + orgY) * stride + orgX;
         for (int x = 0; x < 800 >> half; x = x + 1)
            if (((x + y) & 1) == 0)
               display[d + x] = C_BLACK;
      }
      drawLogo(400, 140);
      centred("The Sundered Crown", 262, C_GOLD, C_BLACK);
      if (blink == 0)
         centred("Press Enter to begin", 292, C_TEXT, C_BLACK);
      centred("Escape to leave", 312, C_DIM, C_BLACK);
      present();
      int k = getKey();
      if (k == 13 || k == 32 || k == 27)
      {
         viewW = 608;
         showHero = 1;
         panelDirty = 2;
      }
      if (k == 13 || k == 32)
         return 1;
      if (k == 27)
         return 0;
   }
   return 0;
}

void story(int off)
{
   int page = off;
   while (page >= 0)
   {
      int next = -1;
      int o = textOff + page;
      while (wad[o] != 0 && wad[o] != 12)
         o++;
      if (wad[o] == 12)
         next = o + 1 - textOff;
      int k = 0;
      int drawn = 0;
      while (k != 13 && k != 32 && k != 27)
      {
         // the page is still: drawn into both buffers, then only waited on
         if (drawn < 2)
         {
            fillRect(0, 0, 800, 480, C_BLACK);
            drawLogo(400, 40);
            box(100, 180, 600, 200);
            setInk(C_TEXT, C_BOX);
            drawPage(page, 120, 200);
            setInk(C_DIM, C_BOX);
            drawText("Enter", 640, 352);
            present();
            drawn++;
         }
         else
            delay(10);
         k = getKey();
      }
      if (k == 27)
         return;
      page = next;
   }
}

void newGame()
{
   hp = 32;
   maxHp = 32;
   mp = 10;
   maxMp = 10;
   str = 5;
   def = 1;
   level = 1;
   xp = 0;
   gold = 30;
   potions = 2;
   weapon = 0;
   armour = 0;
   shards = 0;
   bossDead = 0;
   gateOpen = 0;
   turn = 0;
   worldTurn = 40;
   won = 0;
   faceX = 0;
   faceY = 1;
   for (int i = 0; i < 12 * MAXO; i++)
      done[i] = 0;
   clearLog();
   int o = mapOff[1];
   loadMap(1, wad[o + 10], wad[o + 11] - 1);
   say("Welcome to Valmora.", C_GOLD);
   say("Walk into people to talk. Elder Corwin waits by the well.", C_TEXT);
}

// ------------------------------------------------------------ main

void setup()
{
   C_BG = col(24, 20, 32);
   C_BG2 = col(40, 34, 52);
   C_EDGE = col(90, 74, 110);
   C_TEXT = col(232, 226, 210);
   C_DIM = col(150, 140, 150);
   C_GOLD = col(255, 210, 90);
   C_RED = col(255, 96, 80);
   C_GREEN = col(120, 230, 110);
   C_CYAN = col(110, 210, 255);
   C_BOX = col(16, 14, 30);
   C_BOXEDGE = col(200, 160, 70);
   C_HP = col(220, 50, 50);
   C_MP = col(60, 120, 240);
   C_XP = col(230, 190, 60);
   C_BAR = col(50, 44, 60);
   C_BLACK = 0;
   fontOff = wadFind("FONT");
   textOff = wadFind("TEXT");
   npcOff = wadFind("NPCS");
   dlgOff = wadFind("DLG");
   monOff = wadFind("MONST");
   itemOff = wadFind("ITEMS");
   logoOff = wadFind("LOGO");
   mapOff[0] = wadFind("MAP00");
   mapOff[1] = wadFind("MAP01");
   mapOff[2] = wadFind("MAP02");
   mapOff[3] = wadFind("MAP03");
   mapOff[4] = wadFind("MAP04");
   mapOff[5] = wadFind("MAP05");
   mapOff[6] = wadFind("MAP06");
   mapOff[7] = wadFind("MAP07");
   mapOff[8] = wadFind("MAP08");
   mapOff[9] = wadFind("MAP09");
   mapOff[10] = wadFind("MAP10");
   mapOff[11] = wadFind("MAP11");
   int n = wad[dlgOff];
   int o = dlgOff + 1;
   for (int i = 0; i < n; i++)
   {
      dlgStart[i] = o;
      o = o + 1 + wad[o] * 3;
   }
   // 16x16 tiles: Valmora Low, drawn at 400x240, with its own pixel font
   // and brighter greys (the small letters need the contrast)
   int tl = wadFind("TILES");
   if (wad[tl + 3] == 16)
   {
      half = 1;
      C_TEXT = col(250, 246, 236);
      C_DIM = col(196, 190, 204);
   }
}

// the screen, then the tiles
void start()
{
   setPixelsize(pixelSize);
   // Valmora Low at pixel size 1: its 400x240 in the middle of the panel
   if (half == 1 && pixelSize == 1)
   {
      orgX = 200;
      orgY = 120;
   }
   fillRect(0, 0, 800, 480, C_BLACK);
   setInk(C_TEXT, C_BLACK);
   drawText("Painting Valmora...", 330, 230);
   present();
   decodeTiles();
}

int main()
{
   if (wadLoaded == 0)
   {
      printfln("valmora.wad must be next to the script");
      return 1;
   }
   enableGraphicMode();
   setup();
   // --- the pixel size (make_wad.py --low changes these lines) ---
   pixelSize = 1;
   // --- end of the pixel size ---
   start();
   int held = 0;
   uint32_t lastStep = 0;
   while (true)
   {
      if (title() == 0)
         return 0;
      story(TX_INTRO);
      newGame();
      quit = 0;
      while (quit == 0)
      {
         tick();
         int k = getKey();
         if (k >= 128)
         {
            if (k - 128 == held)
               held = 0;
            k = 0;
         }
         if (k == 0 && held != 0 && keyHeld(held) == 1 && millis() - lastStep > 140)
            k = held;
         if (k == 122 || k == 119)
         {
            held = k;
            lastStep = millis();
            tryMove(0, -1);
         }
         else if (k == 115)
         {
            held = k;
            lastStep = millis();
            tryMove(0, 1);
         }
         else if (k == 113 || k == 97)
         {
            held = k;
            lastStep = millis();
            tryMove(-1, 0);
         }
         else if (k == 100)
         {
            held = k;
            lastStep = millis();
            tryMove(1, 0);
         }
         else if (k == 32)
            endTurn();
         else if (k == 109)
            mend();
         else if (k == 102)
            spark();
         else if (k == 112)
            drinkPotion();
         else if (k == 27)
         {
            if (askYes(-1, -1, "Leave this adventure?", 0) == 1)
               quit = 1;
         }
         if (won == 1)
         {
            animate();
            story(TX_VICTORY);
            quit = 1;
         }
         // a new picture only when something changed, else a short wait
         int force = 0;
         if (k != 0)
            force = 1;
         if (frameDue(force) == 1)
         {
            showFrame(SUBS);
            present();
         }
      }
   }
   return 0;
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

  // the names of SolarOS's script runtime, so that one valmora.bin runs
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
  loadWad("/valmora.wad");
  bindVariable((char *)"uint8_t *", (char *)"wad", NULL, (void *)&wadData);
  bindVariable((char *)"int", (char *)"wadLoaded", NULL, (void *)&wadLoaded);
  bindFunction((char *)"int", (char *)"wadFind", (char *)"char*", (void *)wadFind);

  // 32 KB of code: in internal RAM when a block that big is free, else PSRAM
  setScriptCodeMemory(SCRIPT_CODE_AUTO);
  uint32_t t0 = millis();
  static ScriptExecutable exec = parseScript(valmora);
  if (!exec.isExeExists()) {
    showCompileError();
    return;
  }
  printf("valmora: %u bytes of code, compiled in %u ms\n", (unsigned)exec.exe.binary_size, (unsigned)(millis() - t0));

  hid_keyboard_print_char = print_char;
  hid_keyboard_release_char = release_char;
  app_main_keyboard();

  exec.execute("main");
  printf("valmora: the game has ended\n");
}

void loop() {
  delay(1000);
}
