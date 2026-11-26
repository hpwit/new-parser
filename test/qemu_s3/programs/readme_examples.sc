// The loops of the README's "ESP32-S3: vectorized loops" chapter, each
// vectorized, against the same loops in C.
external void emit(int v);
#define width 100
#define rows 4

uint8_t heat[rows, width];
uint8_t front[width];
uint8_t back[width];
uint8_t level[rows, width];
uint8_t mask[rows, width];
uint8_t trail[width];
uint8_t layerA[width];
uint8_t layerB[width];
uint8_t sum[width];

void clearRow(int y)
{
   for (int x = 0; x < width; x++)
   {
      heat[y, x] = 0;
   }
}

void copyLine()
{
   for (int x = 0; x < width; x++)
   {
      back[x] = front[x];
   }
}

void threshold(int y)
{
   for (int x = 0; x < width; x++)
   {
      mask[y, x] = level[y, x] > 128;
   }
}

void fade()
{
   for (int x = 0; x < width; x++)
   {
      trail[x] = trail[x] - (trail[x] > 0);
   }
}

void addLayers()
{
   for (int x = 0; x < width; x++)
   {
      sum[x] = layerA[x] + layerB[x];
   }
}

int main()
{
   for (int h = 0; h < rows * width; h++)
   {
      heat[h] = h % 251;
      level[h] = (h * 37) % 256;
      mask[h] = 7;
   }
   for (int x = 0; x < width; x++)
   {
      front[x] = (x * 13) % 256;
      trail[x] = x % 3;
      layerA[x] = (x * 29) % 256;
      layerB[x] = (x * 71 + 5) % 256;
   }
   clearRow(2);
   copyLine();
   threshold(1);
   fade();
   addLayers();
   for (int h = 0; h < rows * width; h++)
   {
      emit(heat[h] + mask[h] * 256);
   }
   for (int x = 0; x < width; x++)
   {
      emit(back[x] + trail[x] * 256 + sum[x] * 65536);
   }
}
