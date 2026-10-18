// Video fuzz ROM: every frame, random registers, tilemaps, line tables, palette and sprites.
// All writes are done at the start of the vertical blank and finish before the first visible line
// (the blanking lasts about 76 000 cycles at 50 MHz), so the picture does not depend on CPU timing
// and the RTL must match the simulator exactly.
#include "monocore.h"

static volatile uint32_t vbl;
void irq_handler(void) {
  if (IRQ_STATUS & IRQ_VBLANK) { IRQ_STATUS = IRQ_VBLANK; vbl++; }
}

extern const uint8_t blob[];   // 64 KB of random graphics in ROM (blob.S)
static uint32_t s;
static uint32_t rnd(void) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

// graphics base: CHR RAM, ROM, or an invalid area (WRAM / sound registers) that disables the plane
static uint32_t pick_base(void) {
  uint32_t r = rnd() % 10;
  if (r < 5) return CHRRAM_BASE + ((rnd() % 0x8000) & ~3u);
  if (r < 9) return (uint32_t)blob + ((rnd() % 0x8000) & ~3u);
  return (rnd() & 1) ? WRAM_BASE : APU_BASE;
}

static const uint16_t modes[][2] = {{320, 240}, {256, 224}, {384, 224}, {224, 288}, {384, 192}};

int main(void) {
  s = 0x12345678;
  volatile uint32_t *chr = (volatile uint32_t *)CHRRAM_BASE;
  // CHR RAM: 32 KB of random graphics, then 96 KB of tilemap entries with small tile numbers
  for (int i = 0; i < 0x2000; i++) chr[i] = rnd();
  for (int i = 0x2000; i < 0x8000; i++) chr[i] = rnd() & 0x07FF03FF;
  IRQ_STATUS = 0x1F;
  IRQ_ENABLE = IRQ_VBLANK;
  irq_global_enable();
  DEBUG_OUT = 'G';
  uint32_t f = 0;
  for (;;) {
    uint32_t v = vbl;
    while (vbl == v) wfi();
    f++;
    s = 0x9E3779B9u * (f + 7);
    rnd(); rnd();

    if ((f & 7) == 1) {
      uint32_t m = rnd() % 5;
      SCREEN_W = modes[m][0];
      SCREEN_H = modes[m][1];
    }
    // DIP switches restrict the test: 1 no sprites, 2 no layers, 4 no sprite zoom, 8 no sprite flips,
    // 16 layers from CHR RAM only, 32 sprites from CHR RAM only, 64 4 bpp only, 128 one sprite per line max
    uint32_t dip = DIPSW;
    uint32_t dc = (rnd() & 0x3E) | ((rnd() % 16) ? DISP_ON : 0);
    if (dip & 1) dc &= ~DISP_SPRITES;
    if (dip & 2) dc &= ~(DISP_LAYER(0) | DISP_LAYER(1) | DISP_LAYER(2) | DISP_LAYER(3));
    DISP_CTRL = dc;
    BG_COLOR = rnd();
    for (int l = 0; l < 4; l++) {
      static const uint8_t maps[] = {0x00, 0x04, 0x10, 0x08, 0x20, 0x14, 0x0C, 0x30};   // last two invalid
      uint32_t c = rnd();
      LAYER_CTRL(l) = (c & 0x343) | maps[(c >> 12) % ((c >> 20) % 4 ? 6 : 8)];
      LAYER_CHR(l) = (dip & 16) ? CHRRAM_BASE + ((rnd() % 0x8000) & ~3u) : pick_base();
      if (dip & 64) LAYER_CTRL(l) &= ~LC_8BPP;
      LAYER_SCROLLX(l) = rnd();
      LAYER_SCROLLY(l) = rnd();
    }
    // bulk random data from the CHR RAM (fast block RAM source)
    // (half of the tilemaps per frame, alternately, to stay within the blanking)
    dma_copy((void *)(TILEMAP_BASE + (f & 1) * 0x8000), (const void *)(CHRRAM_BASE + 0x8000 + ((rnd() % 0x8000) & ~3u)), 0x8000);
    dma_copy((void *)LINETAB_BASE, (const void *)(CHRRAM_BASE + ((rnd() % 0x10000) & ~3u)), 0x2000);
    dma_copy((void *)PALETTE_BASE, (const void *)(CHRRAM_BASE + ((rnd() % 0x10000) & ~3u)), 0x2000);

    // sprites: sizes, zooms, flips and positions; some frames pile them on one line
    uint32_t count = rnd() % 257;
    int crowd = (f % 5) == 0;
    int crowd_y = rnd() % 200;
    for (uint32_t i = 0; i < count; i++) {
      volatile sprite_t *sp = &SPRITES[i];
      uint32_t a = rnd(), b = rnd(), c = rnd();
      sp->x = (int16_t)((a % 480) - 64);
      sp->y = crowd ? (int16_t)(crowd_y - (int)(b % 16)) : (int16_t)((b % 360) - 48);
      sp->tile = c & 0x3FF;
      sp->pal = c >> 16;
      sp->attr = (uint8_t)((a >> 9) | ((a >> 20) % 8 ? SA_ON : 0));
      if (dip & 8) sp->attr &= ~(SA_HFLIP | SA_VFLIP);
      if (dip & 64) sp->attr &= ~SA_8BPP;
      uint32_t sz = rnd();
      sp->w = (sz & 0x100) ? (uint8_t)(sz % 41) : (uint8_t)(sz % 5);
      sp->h = (sz & 0x200) ? (uint8_t)((sz >> 12) % 41) : (uint8_t)((sz >> 12) % 5);
      uint32_t z = rnd();
      sp->zoomx = (z & 3) == 0 ? 0 : (z & 3) == 1 ? 0x100 : (uint16_t)(0x30 + (z >> 4) % 0x400);
      sp->zoomy = ((z >> 2) & 3) == 0 ? 0 : ((z >> 2) & 3) == 1 ? 0x100 : (uint16_t)(0x30 + (z >> 16) % 0x400);
      if (dip & 4) { sp->zoomx = 0x100; sp->zoomy = 0x100; }
      if (dip & 128) sp->y = (int16_t)(i * 2);
    }
    SPR_CHR = (dip & 32) ? CHRRAM_BASE + ((rnd() % 0x8000) & ~3u) : pick_base();
    if (dip & 256) {
      // worst case for the render window: 4 layers 16x16 8 bpp and shrunk 8 bpp sprites, all read
      // from the ROM (SDRAM), every sprite crossing the same lines
      DISP_CTRL = DISP_ON | DISP_SPRITES | DISP_LAYER(0) | DISP_LAYER(1) | DISP_LAYER(2) | DISP_LAYER(3);
      for (int l = 0; l < 4; l++) { LAYER_CTRL(l) = LC_TILE16 | LC_8BPP | LC_MAPW(1) | LC_MAPH(1); LAYER_CHR(l) = (uint32_t)blob; }
      for (int i = 0; i < 256; i++) {
        volatile sprite_t *sp = &SPRITES[i];
        sp->x = (int16_t)(i * 3 % 384); sp->y = 100; sp->attr = SA_ON | SA_8BPP; sp->w = 8; sp->h = 2;
        sp->zoomx = 0x40 + (i & 31); sp->zoomy = 0x100; sp->tile = i * 5; sp->pal = i;
      }
      SPR_CHR = (uint32_t)blob;
      SPR_COUNT = 256;
    }
    if (!(dip & 256)) SPR_COUNT = (rnd() % 8) ? count : 300;
    DEBUG_OUT = '.';
  }
}
