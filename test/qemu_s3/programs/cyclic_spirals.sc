external void emit(int v);
#define width 64
#define height 48
// two grids of states 0..15, used in turn: cur -> nxt, then nxt -> cur
uint8_t cur[height, width];
uint8_t nxt[height, width];
// how bright each cell glows: 15 when it just changed, then one less a frame
uint8_t glow[height, width];
// the colour of each cell: its state + 16 * its glow, an index into pal
uint8_t idx[height, width];
uint16_t pal[256];
uint32_t seed = 12345;

// a cell moves to the next state (15 wraps to 0) when one of its four
// neighbours is already there
void cycleCurToNxt(int j)
{
   for (int i = 1; i < width - 1; i++)
   {
      uint8_t c = cur[j, i];
      uint8_t n = (c + 1) & 15;
      uint8_t hit = (cur[j, i - 1] == n) | (cur[j, i + 1] == n) | (cur[j - 1, i] == n) | (cur[j + 1, i] == n);
      nxt[j, i] = (c + hit) & 15;
   }
}

void cycleNxtToCur(int j)
{
   for (int i = 1; i < width - 1; i++)
   {
      uint8_t c = nxt[j, i];
      uint8_t n = (c + 1) & 15;
      uint8_t hit = (nxt[j, i - 1] == n) | (nxt[j, i + 1] == n) | (nxt[j - 1, i] == n) | (nxt[j + 1, i] == n);
      cur[j, i] = (c + hit) & 15;
   }
}

// 15 where the cell changed (0 - 1 is all ones), else the glow fades by one
void glowRow(int j)
{
   for (int i = 0; i < width; i++)
   {
      uint8_t changed = nxt[j, i] != cur[j, i];
      glow[j, i] = ((0 - changed) & 15) | (glow[j, i] - (glow[j, i] > 0));
   }
}

// the palette index of the new state: state + 16 * glow
void indexFromNxt(int j)
{
   for (int i = 0; i < width; i++)
   {
      uint8_t g2 = glow[j, i] + glow[j, i];
      uint8_t g4 = g2 + g2;
      uint8_t g8 = g4 + g4;
      idx[j, i] = nxt[j, i] + g8 + g8;
   }
}

void indexFromCur(int j)
{
   for (int i = 0; i < width; i++)
   {
      uint8_t g2 = glow[j, i] + glow[j, i];
      uint8_t g4 = g2 + g2;
      uint8_t g8 = g4 + g4;
      idx[j, i] = cur[j, i] + g8 + g8;
   }
}

int random16()
{
   seed = seed * 1103515245 + 12345;
   return (seed >> 16) & 15;
}

void seedGrid()
{
   for (int h = 0; h < width * height; h++)
   {
      cur[h] = random16();
      nxt[h] = cur[h];
      glow[h] = 0;
   }
}

// one frame: a step one way or the other, then the glow and the colours
void frame(int f)
{
   if ((f & 1) == 0)
   {
      for (int j = 1; j < height - 1; j++)
         cycleCurToNxt(j);
      for (int j = 0; j < height; j++)
      {
         glowRow(j);
         indexFromNxt(j);
      }
   }
   else
   {
      for (int j = 1; j < height - 1; j++)
         cycleNxtToCur(j);
      // glowRow compares nxt (old) with cur (new): the same test
      for (int j = 0; j < height; j++)
      {
         glowRow(j);
         indexFromCur(j);
      }
   }
}
int main()
{
   seedGrid();
   for (int f = 0; f < 60; f++)
   {
      frame(f);
      if (f % 10 == 9)
      {
         int sum = 0;
         for (int h = 0; h < width * height; h++)
            sum = sum + idx[h] * (h % 89 + 1);
         emit(sum);
      }
   }
}
