#include <stdio.h>

#include "parser.h"

//#define shot
#ifdef shot
char  script[]=R"EOF(
  #define j 45
  struct yves
  {
    float y;
    yves()
    {

    }
  }
yves balls;

void main()
{
  balls.y=45;
}
  )EOF";
 
#else
char  script[]=R"EOF(
external void show();
external uint32_t rand(uint32_t s);
external CRGB hsv(int h,int s,int v);
external CRGB *leds;
#define max_nb_balls 200 
#define rmax 8 
#define rmin 8 
#define width 128 
#define height 96 
#define panel_width 128 
#define panel_height 96 
#define true 1



struct ball
{
   float vx, vy, xc, yc, r;
   int color;
   void drawBall()
   {
    
      int startx = xc - r;
      int r2 = r * r;
      float r4 = r ^ 4;
      int starty = yc - r;
      int _xc = xc;
      int _yc = yc;
      for (int i = startx; i <= _xc; i++)
         for (int j = starty; j <= _yc; j++)
         {
            int v;
            int distance = (i - xc) ^ 2 + (j - yc) ^ 2;
            if (distance <= r2)
            {
               v = 255 * (1 - distance ^ 2 / (r4));
               CRGB cc = hsv(color, 255, v);
               leds[j*panel_width +i] = cc;
               leds[j*panel_width+(int)(2 * xc - i)] = cc;
               leds[(int)(2 * xc - i) + (int)(2 * yc - j) * panel_width] = cc;
               leds[i + (int)(2 * yc - j) * panel_width] = cc;
            }
         }
         
   }
   void updateBall()
   {
    
      xc += vx;
      yc += vy;
      if (xc >= (width - r - 1))
      {
         xc = width - r - 1;
         vx = -vx;
      }
      if (xc < (r + 1))
      {
         xc = r + 1;
         vx = -vx;
      }
      if (yc >= height - r - 1)
      {
         yc = height - r - 1;
         vy = -vy;
      }
      if (yc < r + 1)
      {
         yc = r + 1;
         vy = -vy;
      }

      drawBall();
      
   }
   ball()
   {
    //below if you comment any line it will work like it was a memory issue
    //needs more investigation
      vx = rand(300) / 255 + 0.7;
      vy = rand(280) / 255 + 0.5;
      r = (rmax - rmin) * (rand(280) / 180) + rmin;
      xc = width / 2 * (rand(280) / 255 + 0.3) + 15;
      yc = height / 2 * (rand(280) / 255 + 0.3) + 15;
      color = rand(255);
      
   }
}
ball dds;
ball BALLs[200];

void main(int num)
{
   if (num > max_nb_balls)
   {
      num = max_nb_balls;
   }
   if (num <= 0)
   {
      num = 1;
   }
 //  printfln("numberof balls:%d",num);
  // resetStat();
   uint32_t h;
   while (true)
   {
      for (int i = 0; i < width; i++)
         for (int j = 0; j < height; j++)
            leds[j*panel_width+ i] = hsv(i + h + j, 255, 180);

      for (int i = 0; i < num; i++)
         BALLs[i].updateBall();
      h++;
      show();
   }
}
)EOF";
#endif



void setup()
{
  Serial.begin(115200);
    Script s;

    s.addContent(script);

    s.init();
Parser p;
p.clean();
uint32_t start_mem=esp_get_free_heap_size();
p.parse(&s,&__allTokens);
uint32_t end_mem=esp_get_free_heap_size();

printf("all maem :%ld %ld %ld\n",start_mem,end_mem,start_mem-end_mem);
if(Error.error)
display_error(&Error);
else
{
 // functions.clear();
  printf("**********$code************\n");
 prettyPrint(&program,0);
  printf("**********$variables************\n");
  prettyPrint(&main_context,0);
  printf("**********$functions************\n");
  prettyPrint(&functions,0);
}

//p.clean();
uint32_t after_clean=esp_get_free_heap_size();
printf("all maem :%ld %ld %ld\n",start_mem,after_clean,start_mem-after_clean);
}

void loop()
{

}
