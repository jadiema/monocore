// Chapter 11: a first program with the low-level "mc" SDK.
// Raster rainbow, a waving title (per-line scrolling), 48 bouncing balls drawn in CHR RAM,
// heap memory and snprintf.
#include "mc.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct { int x, y, vx, vy; } ball_t;      // positions in 1/256 pixel
static ball_t *balls;
#define BALLS 48

static void rainbow(void) {                       // raster effect, called every 8 lines
  int a = mc_raster_line * 4;                     // angle: one turn (1024) over the screen height
  mc_color(4095, MC_RGB(110 + (mc_sin(a) * 90 >> 16), 110 + (mc_sin(a + 341) * 90 >> 16),
                        110 + (mc_sin(a + 683) * 90 >> 16)));
}

static void make_ball(void) {                     // an 8 x 8 disc, 4 bits per pixel, in CHR RAM
  uint8_t cell[32] = {0};
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) {
      int dx = 2 * x - 7, dy = 2 * y - 7, d = dx * dx + dy * dy;
      int colour = d < 12 ? 2 : d < 50 ? 1 : 0;   // 2: highlight, 1: body, 0: transparent
      cell[y * 4 + x / 2] |= (uint8_t)(colour << (4 * (x & 1)));
    }
  mc_chr_upload(0, cell, sizeof cell);
  mc_sprite_sheet(MC_CHR(0));
  mc_color(16 * 16 + 1, MC_RGB(255, 120, 40));    // palette 16: the balls
  mc_color(16 * 16 + 2, MC_RGB(255, 240, 200));
}

int main(void) {
  mc_init();
  mc_text_layer(0, 3);
  mc_layer_linescroll(0, true);                   // each line of the text layer can move
  mc_background(4095);
  for (int y = 0; y < 240; y += 8) mc_raster_add(y, rainbow);
  make_ball();
  balls = calloc(BALLS, sizeof(ball_t));          // memory taken from the heap
  for (int i = 0; i < BALLS; i++)
    balls[i] = (ball_t){(int)(mc_rand() % 312) << 8, (80 + (int)(mc_rand() % 152)) << 8,
                        (int)(mc_rand() % 512) - 256, (int)(mc_rand() % 512) - 256};
  mc_sprites_used(BALLS);
  mc_display(DISP_ON | DISP_LAYER(0) | DISP_SPRITES);
  mc_text(0, 10, 6, "BIENVENUE DANS MC !", 0);
  char line[41];
  for (;;) {
    mc_frame();
    for (int y = 40; y < 64; y++)                 // the title waves: one offset per line
      mc_linescroll(0)[y].dx = (int16_t)(mc_sin((int)mc_frames * 8 + y * 6) * 10 >> 16);
    for (int i = 0; i < BALLS; i++) {
      ball_t *b = &balls[i];
      b->x += b->vx; b->y += b->vy;
      if (b->x < 0 || b->x > (312 << 8)) b->vx = -b->vx;
      if (b->y < (80 << 8) || b->y > (232 << 8)) b->vy = -b->vy;
      int zoom = 0x100 + (mc_sin((int)mc_frames * 6 + i * 21) >> 9);   // 0.5x to 1.5x
      mc_sprite(i, b->x >> 8, b->y >> 8, 0, 1, 1, 16, 4);
      mc_sprite_zoom(i, zoom, zoom);
    }
    snprintf(line, sizeof line, "IMAGE %5u   TAS LIBRE %6u", (unsigned)mc_frames, (unsigned)mc_heap_free());
    mc_text(0, 5, 29, line, 0);
  }
}
