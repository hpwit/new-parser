// v1's sc_examples/dsiplayicon.sc, unchanged: three pixel-art sprites
// (a ghost, Mario, a cherry) moving over a scrolling rainbow on a 128x96
// led matrix. v1 itself refused this script: displaypic() takes
// `char * pic` and `CRGB *palette`, and gets uint8_t and CRGB arrays,
// which v2 accepts as C does.
//
// The script draws into leds[] and calls sync() after each frame, as in
// v1's runtime. Here sync() stands for your display's show(): it prints a
// checksum of the frame every 25 frames, which the README compares with
// the frames rendered on an emulated ESP32. Replace it with your led
// driver (FastLED.show(), a panel's DMA buffer...) to see the animation.
#include <math.h>
#include "script_executable.h"
#include "binding.h"

#define LED_WIDTH 128
#define LED_HEIGHT 96

struct CRGB
{
   uint8_t red, green, blue;
};

static CRGB leds[LED_WIDTH * LED_HEIGHT];

CRGB scriptHsv(int h, int s, int v)
{
   CRGB c;
   if (s <= 0)
   {
      c.red = c.green = c.blue = (uint8_t)v;
      return c;
   }
   uint8_t region = (uint8_t)h / 43;
   uint8_t remainder = ((uint8_t)h - (region * 43)) * 6;
   uint8_t p = (uint8_t)((v * (255 - s)) >> 8);
   uint8_t q = (uint8_t)((v * (255 - ((s * remainder) >> 8))) >> 8);
   uint8_t t = (uint8_t)((v * (255 - ((s * (255 - remainder)) >> 8))) >> 8);
   switch (region)
   {
   case 0:  c.red = v; c.green = t; c.blue = p; break;
   case 1:  c.red = q; c.green = v; c.blue = p; break;
   case 2:  c.red = p; c.green = v; c.blue = t; break;
   case 3:  c.red = p; c.green = q; c.blue = v; break;
   case 4:  c.red = t; c.green = p; c.blue = v; break;
   default: c.red = v; c.green = p; c.blue = q; break;
   }
   return c;
}

float scriptSin(float a)
{
   return sinf(a);
}

// v1's sync(): the frame is ready
static int frame = 0;
void scriptSync()
{
   frame++;
   if (frame == 1 || frame % 25 == 0)
   {
      uint32_t hash = 2166136261u; // FNV-1a of the frame's bytes
      const uint8_t *b = (const uint8_t *)leds;
      for (int i = 0; i < (int)sizeof(leds); i++)
         hash = (hash ^ b[i]) * 16777619u;
      printf("frame %d: checksum %08lx\n", frame, (unsigned long)hash);
   }
}

char script[] = R"EOF(
uint8_t ghostp[72]={  
  1,9,9,9,1,9,9,9,1,
  1,1,9,1,1,1,9,1,1,
  1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,
  1,3,2,1,1,3,2,1,1,
  1,2,2,1,1,2,2,1,9,
  9,1,1,1,1,1,1,1,9,
  9,9,1,1,1,1,1,9,9};


CRGB palghost[4] = {CRGB(0,0,0), CRGB(255,0,0), CRGB(255,255,255), CRGB(0,0,255)};
CRGB palghost2[4] = {CRGB(0,0,0), CRGB(255,0,255), CRGB(255,255,255), CRGB(0,0,255)};

uint8_t mario[238]={
   100,3,3,100,100,100,100,100,100,100,100,100,100,100,100,100,100,
   100,3,3,3,2,2,2,2,2,2,100,100,100,100,100,100,100, 
   100,100,3,3,2,2,2,2,2,2,2,2,2,2,3,3,100,
   100,100,100,100,2,2,2,2,4,2,2,2,2,2,3,3,100,
    100,5,5,1,1,1,1,2,2,2,2,4,2,2,3,3,100, 
  5,5,1,1,1,1,1,2,1,1,1,2,100,100,3,3,100, 
    5,5,1,1,1,1,2,1,1,1,2,1,1,100,100,3,100,
  100,100,100,100,100,5,5,5,5,5,5,5,1,1,100,100,100,
  100,100,100,3,3,5,5,5,5,6,6,6,6,1,100,100,100,
  100,100,100,3,5,3,3,5,5,5,6,5,5,5,1,100,100,
  100,100,100,3,5,3,5,5,5,6,5,5,5,1,1,100,100,
  100,100,100,100,3,3,3,5,5,6,5,100,1,1,1,100,100, 
  100,100,100,100,1,1,1,1,1,1,1,1,1,5,5,100,100,
  100,100,100,100,100,1,1,1,1,1,100,100,5,5,5,100,100            
};
uint8_t cerise[196] = {
  100, 100, 100, 4, 4, 4, 4, 100, 100, 100, 100, 100, 100, 100,
  100, 100, 4, 1, 1, 1, 1, 4, 100, 100, 100, 100, 100, 100,
  100, 4, 1, 1, 2, 2, 1, 1, 4, 4, 4, 4, 100, 100,
  100, 4, 1, 1, 1, 1, 2, 1, 4, 1, 1, 1, 4, 100,
  100, 4, 1, 1, 1, 1, 1, 1, 4, 2, 2, 1, 1, 4,
  100, 4, 1, 1, 1, 1, 1, 1, 4, 1, 1, 2, 1, 4,
  100, 100, 4, 1, 1, 1, 1, 4, 1, 1, 1, 1, 1, 4,
  100, 100, 100, 4, 4, 3, 4, 1, 1, 1, 1, 1, 1, 4,
  100, 100, 100, 4, 3, 4, 100, 0, 1, 1, 1, 1, 4, 100,
  100, 100, 4, 3, 4, 4, 4, 3, 4, 4, 4, 4, 100, 100,
  100, 4, 4, 3, 4, 3, 3, 4, 100, 100, 100, 100, 100, 100,
  4, 3, 3, 3, 3, 4, 4, 100, 100, 100, 100, 100, 100, 100,
  4, 3, 3, 4, 4, 100, 100, 100, 100, 100, 100, 100, 100, 100,
  100, 4, 4, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100
};


CRGB palm[7]={CRGB(255,0,0),CRGB(255,0,0), CRGB(0,0,255),CRGB(98,65,7),CRGB(255,255,0),CRGB(238,151,64),CRGB(0,0,0)};
CRGB palec[5] = {CRGB(0,0,0), CRGB(255,0,0), CRGB(255,255,255), CRGB(0,255,0),CRGB(0,0,0)};

void displaypic(int x, int y,char * pic,CRGB *palette, int h, int w)
{
//printf("x:%d y:%d\n",x,y);

for (int i=0;i<w;i++)
   {
      for(int j=0;j<h;j++)
      {
            if(pic[i+j*w]<9)
            {
               leds[(i+x)%128+((j+y)%96)*128]=palette[pic[i+j*w]];
            }
      }    
   }
}

void main()
{




for(int m=0;m<30000;m++)
{
   
for(int i=0;i<128;i++)
{
    for(int j=0;j<96;j++)
    {
        leds[i+j*128]=hsv(i+j+m,255,100);
    }
}

float pos=40*sin(3.14*(m%100)/50)+40;
float posy=40*sin(3.14*(m%100)/50+1.58)+40;
displaypic(posy,0 ,ghostp,palghost, 8,9);
displaypic(m,15,mario,palm,14,17);
displaypic(pos,posy,cerise,palec,14,14);

sync();
//print2(g,m);
}
}
)EOF";

void setup()
{
   Serial.begin(115200);
   delay(1000);

   bindVariable((char *)"CRGB", (char *)"leds", (char *)"[12288]", (void *)leds);
   bindFunction((char *)"CRGB", (char *)"hsv", (char *)"int,int,int", (void *)scriptHsv);
   bindFunction((char *)"float", (char *)"sin", (char *)"float", (void *)scriptSin);
   bindFunction((char *)"void", (char *)"sync", NULL, (void *)scriptSync);

   static ScriptExecutable exec = parseScript(script);
   if (!exec.isExeExists())
   {
      showCompileError();
      return;
   }
   uint32_t t = millis();
   exec.execute("main"); // 30000 frames
   printf("done: %d frames in %lu ms\n", frame, (unsigned long)(millis() - t));
}

void loop()
{
   delay(1000);
}
