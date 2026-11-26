// Board check on an ESP32-S3 (QEMU's esp32s3 machine cannot run the
// script loader, and lacks ee.vldbc without .ip), printing START/RESULT: a
// script whose __ASM__ functions use the S3's PIE instructions (ee.*),
// each checked against the same operation done one element at a time.
// Covers the encodings (also checked against GNU as on the host), the
// q registers under FreeRTOS (the task's first ee.* instruction enables
// the coprocessor), and 16-byte aligned loads/stores with .ip.
#include "script_executable.h"
#include "binding.h"

static int bad = 0;

char script[] = R"EOF(
s_int buf[96];

// x rounded up to a multiple of 16 bytes: ee.vld.128/ee.vst.128 need it
__ASM__ s_int *align16(s_int *x)
{
"entry a1,32"
"addi a2,a2,15"
"movi a3,-16"
"and a2,a2,a3"
"retw.n"
}

// d[i] = a[i] + b[i], saturated, n a multiple of 8, all 16-byte aligned
__ASM__ void vadds16(s_int *d, s_int *a, s_int *b, int n)
{
"entry a1,32"
"srli a5,a5,3"
"beqz a5,vadds_end"
"vadds_loop:"
"ee.vld.128.ip q0,a3,16"
"ee.vld.128.ip q1,a4,16"
"ee.vadds.s16 q2,q0,q1"
"ee.vst.128.ip q2,a2,16"
"addi a5,a5,-1"
"bnez a5,vadds_loop"
"vadds_end:"
"retw.n"
}

// d[i] = max(d[i], a[i])
__ASM__ void vmax16(s_int *d, s_int *a, int n)
{
"entry a1,32"
"srli a4,a4,3"
"beqz a4,vmax_end"
"mov a6,a2"
"vmax_loop:"
"ee.vld.128.ip q0,a2,16"
"ee.vld.128.ip q1,a3,16"
"ee.vmax.s16 q2,q0,q1"
"ee.vst.128.ip q2,a6,16"
"addi a4,a4,-1"
"bnez a4,vmax_loop"
"vmax_end:"
"retw.n"
}

// d[0..n-1] = v (a broadcast load from v's own address), n a multiple of 8
__ASM__ void vfill16(s_int *d, s_int *v, int n)
{
"entry a1,32"
"ee.vldbc.16 q0,a3"
"srli a4,a4,3"
"beqz a4,vfill_end"
"vfill_loop:"
"ee.vst.128.ip q0,a2,16"
"addi a4,a4,-1"
"bnez a4,vfill_loop"
"vfill_end:"
"retw.n"
}

// lane k (0..3) of the 32-bit lanes of the 16 bytes at p: ee.movi.32.a
__ASM__ int lane32(s_int *p, int k)
{
"entry a1,32"
"ee.zero.q q3"
"ee.vld.128.ip q3,a2,0"
"beqi a3,1,lane_1"
"beqi a3,2,lane_2"
"beqi a3,3,lane_3"
"ee.movi.32.a q3,a2,0"
"retw.n"
"lane_1:"
"ee.movi.32.a q3,a2,1"
"retw.n"
"lane_2:"
"ee.movi.32.a q3,a2,2"
"retw.n"
"lane_3:"
"ee.movi.32.a q3,a2,3"
"retw.n"
}

int sat(int v)
{
   if (v > 32767)
      return 32767;
   if (v < -32768)
      return -32768;
   return v;
}

void main()
{
   s_int *p = align16(buf);
   s_int *a = p;
   s_int *b = p + 24;
   s_int *d = p + 48;
   for (int i = 0; i < 24; i++)
   {
      a[i] = i * 1500 - 9000;
      b[i] = 30000 - i * 2700;
   }
   vadds16(d, a, b, 24);
   for (int i = 0; i < 24; i++)
      if (d[i] != sat(a[i] + b[i]))
      {
         printfln("vadds16 %d: %d, expected %d", i, d[i], sat(a[i] + b[i]));
         bad++;
      }
   vmax16(a, b, 24);
   for (int i = 0; i < 24; i++)
   {
      int was = i * 1500 - 9000;
      int m = was;
      if (b[i] > m)
         m = b[i];
      if (a[i] != m)
      {
         printfln("vmax16 %d: %d, expected %d", i, a[i], m);
         bad++;
      }
   }
   b[0] = -1234;
   vfill16(d, b, 16);
   for (int i = 0; i < 16; i++)
      if (d[i] != -1234)
      {
         printfln("vfill16 %d: %d", i, d[i]);
         bad++;
      }
   for (int i = 0; i < 8; i++)
      d[i] = i + 1;
   for (int k = 0; k < 4; k++)
   {
      int want = (2 * k + 1) + (2 * k + 2) * 65536;
      if (lane32(d, k) != want)
      {
         printfln("lane32 %d: %d, expected %d", k, lane32(d, k), want);
         bad++;
      }
   }
   // where the saturating add clamps below: -32768, or -32767 as QEMU does
   for (int i = 0; i < 8; i++)
   {
      a[i] = -30000 - i;
      b[i] = -30000;
   }
   vadds16(d, a, b, 8);
   printfln("saturation: -30000 + -30000 = %d, 30000 + 30000 = %d", d[0], sat(60000));
   a[0] = 30000;
   b[0] = 30000;
   vadds16(d, a, b, 8);
   if (d[0] != 32767)
   {
      printfln("vadds16 positive saturation: %d", d[0]);
      bad++;
   }
   printfln("checked: %d wrong", bad);
}
)EOF";

void setup()
{
   Serial.begin(115200);
   delay(500);
   printf("START\n");
   bindVariable((char *)"int", (char *)"bad", NULL, (void *)&bad);
   ScriptExecutable exec = parseScript(script);
   if (!exec.isExeExists())
   {
      showCompileError();
      printf("RESULT FAIL\n");
      return;
   }
   exec.execute("main");
   printf("RESULT %s\n", bad == 0 ? "PASS" : "FAIL");
}

void loop()
{
   delay(1000);
}
