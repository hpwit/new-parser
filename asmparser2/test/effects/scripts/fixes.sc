// The idioms the October 2026 code-generation fixes were about: an
// else-if chain ending in a plain else, calls on either side of an
// operator in 2D array stores, a CRGB-returning call stored into a 2D
// external array. Compared frame for frame with reference.c's fixes().
#define width 128
#define height 96
external CRGB leds[height, width];
external CRGB hsv(int h, int s, int v);
external uint32_t rand(uint32_t mod);
external void show();
uint8_t heat[height, width];
CRGB palette[256];
void main()
{
   for (int i = 0; i < 256; i++)
   {
      if (i < 64)
         palette[i] = CRGB(i * 4, 0, 0);
      else if (i < 160)
         palette[i] = CRGB(255, (i - 64) * 8 / 3, 0);
      else if (i < 200)
      {
         palette[i] = CRGB(255, 255, (i - 160) * 8 / 3);
      }
      else
         palette[i] = CRGB(i, i, 255);
   }
   for (int y = 0; y < 32; y++)
      for (int x = 0; x < width; x++)
      {
         heat[y, x] = rand(200) + 20;
         leds[y, x] = palette[heat[y, x]];
      }
   for (int y = 32; y < 64; y++)
      for (int x = 0; x < width; x++)
         leds[y, x] = hsv(x + y, 255, 255);
   for (int y = 64; y < 80; y++)
      for (int x = 0; x < width; x++)
      {
         heat[y, x] = rand(255);
         leds[y, x] = palette[heat[y, x]];
      }
   for (int y = 80; y < height; y++)
      for (int x = 0; x < width; x++)
      {
         heat[y, x] = 20 + rand(200);
         leds[y, x] = palette[heat[y, x]];
      }
   show();
}
