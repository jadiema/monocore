#include "mc.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
static void red(void)  { mc_color(4095, MC_RGB(255, 0, 0)); }
static void blue(void) { mc_color(4095, MC_RGB(0, 0, 255)); }
static volatile int ticks;
static void tick(void) { ticks++; }
int main(void) {
  mc_init();
  mc_text_layer(0, 3);
  mc_background(4095);
  mc_color(4095, 0);
  mc_raster_add(100, red);
  mc_raster_add(0, blue);
  mc_raster_add(160, blue);
  mc_on_timer(0, 1000, tick);
  mc_text(0, 2, 2, "SDK MC : TEST", 0);
  char *a = malloc(1000), *b = malloc(5000);
  strcpy(a, "bonjour"); strcat(a, " monocore");
  free(a);
  char *c = malloc(500);
  printf("heap %u a=%p c=%p same=%d ", (unsigned)mc_heap_free(), a, c, a == c);
  printf("sin90=%d atan(1,1)=%d sqrt=%u ", (int)mc_sin(256), mc_atan2(1, 1), (unsigned)mc_isqrt(1000000));
  char buf[32]; snprintf(buf, sizeof buf, "[%05d|%-4s|%X]", -42, "ab", 0xBEEF); printf("%s ", buf);
  for (int i = 0; i < 10; i++) mc_frame();
  printf("ticks10f=%d frames=%u", ticks, (unsigned)mc_frames);
  (void)b;
  for (;;) mc_frame();
}
