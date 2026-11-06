// MultiEffectController's plasma, with time stepped by the frame instead
// of read from millis(): float values kept across calls (each division
// is a call to ___div, then sin() and hypot()). A float restore after a
// call whose save had been dropped -- the save was renamed to the
// register its value was copied from -- left one term stale. Compared
// frame for frame with reference.c's floatcalls().
#define width 128
#define height 96
external CRGB leds[height, width];
external CRGB hsv(int h, int s, int v);
external float sin(float a);
external float hypot(float x, float y);
external void show();

float lerp(float a, float b, float t)
{
   return a + (b - a) * t;
}

float plasmaValue(int x, int y, uint32_t t)
{
   float fx = x;
   float fy = y;
   float ft = t;

   float v1 = sin((fx / 8.0) + (ft / 40.0));
   float v2 = sin((fy / 6.0) - (ft / 55.0));
   float v3 = sin(((fx + fy) / 10.0) + (ft / 30.0));

   float cx = fx - (width / 2);
   float cy = fy - (height / 2);
   float dist = hypot(cx, cy);
   float v4 = sin((dist / 4.0) - (ft / 20.0));

   return (v1 + v2 + v3 + v4) / 4.0;
}

void main()
{
   uint32_t t = 0;
   while (t < 80)
   {
      for (int y = 0; y < height; y++)
      {
         for (int x = 0; x < width; x++)
         {
            float v = plasmaValue(x, y, t);
            int hue = (int)(lerp(0, 255, (v + 1.0) / 2.0));
            leds[y, x] = hsv(hue, 255, 200);
         }
      }
      show();
      t = t + 40;
   }
}
