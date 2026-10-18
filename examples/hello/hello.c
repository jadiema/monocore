// Minimal MONOCORE-1 program: text, a sprite moved with the joystick, a beep on button 1.
#include "monocore.h"

// graphics and palettes converted by sdk/tools/png2chr.py, pulled in by data.S
extern const uint8_t font_chr[], font_pal[], robot_chr[], robot_pal[];

static volatile uint32_t vblanks;

void irq_handler(void) {
  if (IRQ_STATUS & IRQ_VBLANK) { IRQ_STATUS = IRQ_VBLANK; vblanks++; }
}

static void wait_vblank(void) {
  uint32_t v = vblanks;
  while (vblanks == v) wfi();
}

static void print(int x, int y, const char *s) {
  volatile uint32_t *map = TILEMAP(0) + y * 64 + x;   // 64x64 map, see LC_MAPW(1) | LC_MAPH(1)
  while (*s) *map++ = TILE(*s++ - 32, 0);             // font tile n = ASCII code - 32, palette 0
}

int main(void) {
  // palettes: 0 = font, 1 = robot ; background colour = palette entry 2*16
  dma_copy(PALETTE, font_pal, 32);
  dma_copy(PALETTE + 16, robot_pal, 32);
  PALETTE[32] = RGB555(30, 40, 90);
  BG_COLOR = 32;

  // layer 0: text, 8x8 tiles, 4 bpp
  LAYER_CTRL(0) = LC_MAPW(1) | LC_MAPH(1) | LC_PRIO(1);
  LAYER_CHR(0) = (uint32_t)font_chr;
  print(14, 8, "BONJOUR !");
  print(6, 26, "JOYSTICK : BOUGER   B1 : BIP");

  // sprites: graphics base, one 2x2-cell sprite
  SPR_CHR = (uint32_t)robot_chr;
  SPR_COUNT = 1;
  int x = 152, y = 112;

  DISP_CTRL = DISP_ON | DISP_LAYER(0) | DISP_SPRITES;
  IRQ_ENABLE = IRQ_VBLANK;
  irq_global_enable();

  for (;;) {
    wait_vblank();
    uint32_t in = IN_P1;
    if (in & IN_LEFT) x -= 2;
    if (in & IN_RIGHT) x += 2;
    if (in & IN_UP) y -= 2;
    if (in & IN_DOWN) y += 2;

    volatile sprite_t *s = &SPRITES[0];
    s->x = x; s->y = y; s->tile = 0; s->pal = 1;
    s->attr = SA_ON | SA_PRIO(2); s->w = 2; s->h = 2;
    s->zoomx = 0x200; s->zoomy = 0x200;                 // displayed at twice its size

    // button 1: square wave at 880 Hz on PSG channel 0
    PSG[0].wave = PSG_SQUARE; PSG[0].duty = 128; PSG[0].freq = 880 * 16;
    PSG[0].vol_l = PSG[0].vol_r = (in & IN_B(1)) ? 120 : 0;
  }
}
