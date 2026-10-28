// GRAVITON: a gravity puzzle for MONOCORE-1.
// Place pods that pull or push, then fire: the shot must reach the purple target.
// All graphics are drawn at start-up: two 320 x 240 bitmaps in CHR RAM (scenery, shot traces),
// sprites for pods, rings, target, shot, cursor and sparks.
#include "mc.h"
#include <string.h>
#include "physics.h"
#include "levels.h"

// ---------------------------------------------------------------- layout
// The game world is 320 x 240 (the physics and the levels); it is drawn twice as fine, on a
// 640 x 480 interlaced picture: text in 16 x 16 tiles (an anti-aliased vector font, font16.h), the two
// bitmaps in 16 x 16 tiles allocated only where something is drawn (most of the screen is black),
// sprites drawn at twice their size.
#define SCR_W 320                          // world
#define SCR_H 240
#define HW    640                          // screen
#define HH    480
#define BM_COLS 40                         // 16 x 16 cells
#define BM_ROWS 30

#define CHR_FONT 0x00000                   // 96 characters of 16 x 16 (128 bytes each)
#define CHR_SPR  0x03000                   // sprite cells, up to 0x06000
#define CHR_POOL 0x06000                   // bitmap tiles, 128 bytes each, up to the end of CHR RAM
#define POOL_N   ((0x20000 - CHR_POOL) / 128)   // 832; tile 0 stays blank

#define CHR_STAT 0                         // bitmap ids
#define CHR_DYN  1

#define L_TEXT 0
#define L_DYN  2
#define L_STAT 3

// sprite cells (32 bytes each, from CHR_SPR), drawn at twice the size of the 320 x 240 version
#define CELL_POD     0                     // 32 x 32 asterisk (pull): 16 cells
#define CELL_PODPUSH 16                    // asterisk in a ring (push)
#define CELL_TGT     32                    // 4 frames of the spinning target, 16 cells each
#define CELL_SHOT    96                    // 8 x 8
#define CELL_SPARK   97
#define CELL_DOT     98                    // 4 x 4 dot
#define CELL_CURSOR  100                   // 32 x 32
#define CELL_BIGPOD  116                   // 64 x 64 planet: 64 cells
#define CELL_BIGPUSH 180                   // 64 x 64
#define N_CELLS      244

// palettes
#define PAL_FONT    0
#define PAL_INK     1                      // 1-7: coloured text
#define PAL_STAT    16
#define PAL_DYN     17
#define PAL_PULL    18
#define PAL_PUSH    19
#define PAL_PULLFIX 20
#define PAL_PUSHFIX 21
#define PAL_BAD     22                     // a pod that cannot be put here
#define PAL_TARGET  23
#define PAL_SHOT    24
#define PAL_CURSOR  25
#define PAL_AIM     26
#define PAL_DIM     27                     // dimmed inventory dots
#define PAL_SPARK   44                     // 44-47: sparks, bright to faint
#define BG_ENTRY    4095

enum { INK_WHITE, INK_YELLOW, INK_CYAN, INK_ORANGE, INK_PURPLE, INK_GREY, INK_GREEN };
#define INK(i) (PAL_INK + (i))

// static bitmap colours
#define C_FRAME  1
#define C_DIM    2
#define C_HALO   4
#define C_WALL   5
#define C_WALLHI 6
#define C_BHALO  7
#define C_BOUNCE 8
#define C_NOPOD  9
#define C_BASE   10
#define C_TITLE1 11
#define C_TITLE2 12
#define C_TITLE3 13
#define C_BHI    14

// sprites
#define S_CURSOR 0
#define S_HELD   1
#define S_SHOT   2
#define S_TARGET 3
#define S_TRING  4
#define S_INV    5                         // 5-14: inventory dots
#define N_INV    10
#define S_POD    16                        // 16-31
#define S_RING   32                        // 32-47
#define S_FX     48
#define N_FX     64
#define S_USED   (S_FX + N_FX)

// ---------------------------------------------------------------- small helpers
static uint32_t rnd(uint32_t n) { return n ? mc_rand() % n : 0; }
static int iabs(int v) { return v < 0 ? -v : v; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void set_rgb(int pal, int i, int r, int g, int b) { mc_color(pal * 16 + i, MC_RGB(r, g, b)); }
static void text(int col, int row, const char *s, int ink) { mc_text(L_TEXT, col, row, s, INK(ink)); }
static void text_c(int row, const char *s, int ink) { text((40 - (int)strlen(s)) / 2, row, s, ink); }
static void text_clear(void) { mc_text_clear(L_TEXT); }

// ---------------------------------------------------------------- bitmaps (screen pixels)
static uint16_t cell_tile[2][BM_ROWS][BM_COLS];    // tile of each cell, 0 = none
static uint8_t cell_shared[2][BM_ROWS][BM_COLS];   // the cell shows the shared hatching tile
static uint16_t pool_free[POOL_N]; static int n_free;
static int hatch_tile;
static const int bm_layer[2] = { L_STAT, L_DYN }, bm_pal[2] = { PAL_STAT, PAL_DYN };

static void pool_reset(void) {
  n_free = 0;
  for (int t = POOL_N - 1; t >= 1; t--) pool_free[n_free++] = (uint16_t)t;
  memset(cell_tile, 0, sizeof cell_tile); memset(cell_shared, 0, sizeof cell_shared);
  hatch_tile = 0;
  for (int b = 0; b < 2; b++) mc_tilemap_fill(bm_layer[b], TILE(0, bm_pal[b]));
}
static void bm_setup(int layer, int pal, int prio) {
  mc_layer(layer, MC_CHR(CHR_POOL), MC_TILE16 | MC_4BPP, 64, 32, prio);
  mc_tilemap_fill(layer, TILE(0, pal));
  mc_layer_scroll(layer, 0, 0);
}
static int tile_new(void) {
  if (!n_free) return 0;
  int t = pool_free[--n_free];
  mc_fill(MC_CHRRAM + CHR_POOL + t * 128, 0, 128);
  return t;
}
static void bm_clear(int b) {                       // frees every tile of one bitmap
  for (int r = 0; r < BM_ROWS; r++) for (int c = 0; c < BM_COLS; c++) {
    if (cell_tile[b][r][c] && !cell_shared[b][r][c]) pool_free[n_free++] = cell_tile[b][r][c];
    cell_tile[b][r][c] = 0; cell_shared[b][r][c] = 0;
  }
  mc_tilemap_fill(bm_layer[b], TILE(0, bm_pal[b]));
}
static inline volatile uint8_t *bm_cell(int b, int x, int y, bool make) {
  if ((unsigned)x >= HW || (unsigned)y >= HH) return 0;
  int c = x >> 4, r = y >> 4, t = cell_tile[b][r][c];
  if (t && cell_shared[b][r][c] && make) {          // copy on write
    int n = tile_new(); if (!n) return 0;
    for (int i = 0; i < 128; i++) MC_CHRRAM[CHR_POOL + n * 128 + i] = MC_CHRRAM[CHR_POOL + t * 128 + i];
    t = n; cell_tile[b][r][c] = (uint16_t)t; cell_shared[b][r][c] = 0;
    mc_tile(bm_layer[b], c, r, TILE(t, bm_pal[b]));
  }
  if (!t) {
    if (!make) return 0;
    t = tile_new(); if (!t) return 0;
    cell_tile[b][r][c] = (uint16_t)t;
    mc_tile(bm_layer[b], c, r, TILE(t, bm_pal[b]));
  }
  return MC_CHRRAM + CHR_POOL + t * 128 + (y & 15) * 8 + ((x & 15) >> 1);
}
static inline void bm_plot(int b, int x, int y, int c) {
  volatile uint8_t *p = bm_cell(b, x, y, c != 0);
  if (!p) return;
  uint8_t v = *p;
  *p = (x & 1) ? (uint8_t)((v & 0x0F) | (c << 4)) : (uint8_t)((v & 0xF0) | c);
}
static inline int bm_get(int b, int x, int y) {
  volatile uint8_t *p = bm_cell(b, x, y, false);
  if (!p) return 0;
  return (x & 1) ? *p >> 4 : *p & 15;
}
static void bm_disc(int b, int x, int y, int r, int c) {
  for (int dy = -r; dy <= r; dy++)
    for (int dx = -r; dx <= r; dx++)
      if (dx * dx + dy * dy <= r * r + r) bm_plot(b, x + dx, y + dy, c);
}
static void bm_line(int b, int x0, int y0, int x1, int y1, int r, int c) {
  int dx = x1 - x0, dy = y1 - y0, n = iabs(dx) > iabs(dy) ? iabs(dx) : iabs(dy);
  if (n == 0) n = 1;
  for (int i = 0; i <= n; i++) {
    int x = x0 + (dx * i + (dx >= 0 ? n / 2 : -n / 2)) / n, y = y0 + (dy * i + (dy >= 0 ? n / 2 : -n / 2)) / n;
    if (r) bm_disc(b, x, y, r, c); else bm_plot(b, x, y, c);
  }
}
// wipes the pixels of one colour from a bitmap, freeing the tiles left empty
static void bm_wipe(int b, int col) {
  for (int r = 0; r < BM_ROWS; r++) for (int c = 0; c < BM_COLS; c++) {
    int t = cell_tile[b][r][c];
    if (!t || cell_shared[b][r][c]) continue;
    volatile uint8_t *p = MC_CHRRAM + CHR_POOL + t * 128;
    uint8_t any = 0;
    for (int i = 0; i < 128; i++) {
      uint8_t v = p[i];
      if (v) { if ((v & 15) == col) v &= 0xF0; if ((v >> 4) == col) v &= 0x0F; p[i] = v; any |= v; }
    }
    if (!any) { pool_free[n_free++] = (uint16_t)t; cell_tile[b][r][c] = 0; mc_tile(bm_layer[b], c, r, TILE(0, bm_pal[b])); }
  }
}
// a rectangle of hatching (screen pixels): whole cells share one tile, the edges are drawn
static void bm_hatch(int b, int x0, int y0, int x1, int y1, int col) {
  if (!hatch_tile) {
    hatch_tile = tile_new();
    if (hatch_tile) for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) if (((x + y) & 7) == 0) {
      volatile uint8_t *p = MC_CHRRAM + CHR_POOL + hatch_tile * 128 + y * 8 + (x >> 1);
      *p = (x & 1) ? (uint8_t)((*p & 0x0F) | (col << 4)) : (uint8_t)((*p & 0xF0) | col);
    }
  }
  for (int r = 0; r < BM_ROWS; r++) for (int c = 0; c < BM_COLS; c++) {
    int cx0 = c * 16, cy0 = r * 16;
    if (cx0 >= x0 && cx0 + 15 <= x1 && cy0 >= y0 && cy0 + 15 <= y1 && !cell_tile[b][r][c] && hatch_tile) {
      cell_tile[b][r][c] = (uint16_t)hatch_tile; cell_shared[b][r][c] = 1;
      mc_tile(bm_layer[b], c, r, TILE(hatch_tile, bm_pal[b]));
    } else if (cx0 + 15 >= x0 && cx0 <= x1 && cy0 + 15 >= y0 && cy0 <= y1) {
      for (int y = (cy0 > y0 ? cy0 : y0); y <= cy0 + 15 && y <= y1; y++)
        for (int x = (cx0 > x0 ? cx0 : x0); x <= cx0 + 15 && x <= x1; x++)
          if (((x + y) & 7) == 0) bm_plot(b, x, y, col);
    }
  }
}

// ---------------------------------------------------------------- sprite graphics
#define GW 64
static uint8_t gfx[GW * GW];               // one picture being drawn, 1 byte a pixel

static void gfx_begin(void) { memset(gfx, 0, sizeof gfx); }
static void gfx_put(int x, int y, int c, int w) { if ((unsigned)x < (unsigned)w && (unsigned)y < (unsigned)w) gfx[y * GW + x] = (uint8_t)c; }
static void gfx_upload(int cell, int size) {   // size 8 to 64: picture -> cells, row-major
  static uint8_t buf[GW * GW / 2];
  int n = size / 8, k = 0;
  for (int cy = 0; cy < n; cy++)
    for (int cx = 0; cx < n; cx++)
      for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x += 2) {
          int a = gfx[(cy * 8 + y) * GW + cx * 8 + x], b = gfx[(cy * 8 + y) * GW + cx * 8 + x + 1];
          buf[k++] = (uint8_t)(a | (b << 4));
        }
  mc_chr_upload(CHR_SPR + cell * 32, buf, (uint32_t)k);
}
static void gfx_ring(int c2, int r2, int thick2, int col, int size) {   // in half pixels
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++) {
      int dx = 2 * x + 1 - c2, dy = 2 * y + 1 - c2;
      int d = (int)mc_isqrt((uint32_t)(dx * dx + dy * dy));
      if (iabs(d - r2) <= thick2) gfx_put(x, y, col, size);
    }
}
static void gfx_line(int x0, int y0, int x1, int y1, int c, int size) {
  int dx = x1 - x0, dy = y1 - y0, n = iabs(dx) > iabs(dy) ? iabs(dx) : iabs(dy);
  if (!n) n = 1;
  for (int i = 0; i <= n; i++) gfx_put(x0 + (dx * i + (dx >= 0 ? n / 2 : -n / 2)) / n, y0 + (dy * i + (dy >= 0 ? n / 2 : -n / 2)) / n, c, size);
}
// an asterisk of 8 branches, centre (c, c) of a picture, branches 2 pixels thick
static void gfx_asterisk(int c, int r, int col, int size) {
  int d = r * 3 / 4;
  for (int k = 0; k < 2; k++) {
    gfx_line(c - r, c - k, c + r, c - k, col, size); gfx_line(c - k, c - r, c - k, c + r, col, size);
    gfx_line(c - d - k, c - d, c + d - k, c + d, col, size); gfx_line(c - d - k, c + d, c + d - k, c - d, col, size);
  }
}

static void make_sprites(void) {
  gfx_begin(); gfx_asterisk(16, 10, 1, 32); gfx_upload(CELL_POD, 32);
  gfx_begin(); gfx_asterisk(16, 6, 1, 32); gfx_ring(31, 23, 1, 1, 32); gfx_upload(CELL_PODPUSH, 32);
  for (int f = 0; f < 4; f++) {                    // hatching turned by 22.5 degrees a frame
    gfx_begin(); gfx_ring(31, 27, 2, 1, 32);
    int32_t c = mc_cos(f * 64), sn = mc_sin(f * 64);
    for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
      int dx = 2 * x + 1 - 31, dy = 2 * y + 1 - 31;
      if (dx * dx + dy * dy >= 22 * 22) continue;
      int u = (dx * c + dy * sn) >> 16;            // half pixels across the hatching
      if (((u + 64) % 8) < 2) gfx_put(x, y, 1, 32);
    }
    gfx_upload(CELL_TGT + f * 16, 32);
  }
  gfx_begin();
  for (int y = 2; y <= 4; y++) for (int x = 2; x <= 4; x++) gfx_put(x, y, (x == 3 && y == 3) ? 1 : 2, 8);
  gfx_put(3, 1, 2, 8); gfx_put(3, 5, 2, 8); gfx_put(1, 3, 2, 8); gfx_put(5, 3, 2, 8); gfx_put(3, 3, 1, 8);
  gfx_upload(CELL_SHOT, 8);
  gfx_begin(); gfx_put(3, 3, 1, 8); gfx_put(3, 2, 2, 8); gfx_put(3, 4, 2, 8); gfx_put(2, 3, 2, 8); gfx_put(4, 3, 2, 8);
  gfx_upload(CELL_SPARK, 8);
  gfx_begin(); for (int y = 2; y < 6; y++) for (int x = 2; x < 6; x++) gfx_put(x, y, 1, 8); gfx_upload(CELL_DOT, 8);
  gfx_begin();
  for (int i = 0; i < 32; i++) if (i < 11 || i > 20) { gfx_put(i, 16, 1, 32); gfx_put(16, i, 1, 32); }
  gfx_upload(CELL_CURSOR, 32);
  gfx_begin(); gfx_ring(64, 54, 1, 1, 64); gfx_asterisk(32, 12, 1, 64); gfx_upload(CELL_BIGPOD, 64);
  gfx_begin(); gfx_ring(64, 54, 1, 1, 64); gfx_ring(64, 36, 1, 1, 64); gfx_asterisk(32, 8, 1, 64); gfx_upload(CELL_BIGPUSH, 64);
  mc_sprite_sheet(MC_CHR(CHR_SPR));
}

// the 16 x 16 font: an anti-aliased vector font (font16.h, made by make_font.py), as text tiles;
// colour indices 1 (full) to 4 (faint) are coverage levels, blended with the background by the palettes
#include "font16.h"
static void make_font(void) {
  mc_chr_upload(CHR_FONT, font16, sizeof font16);
  mc_layer(L_TEXT, MC_CHR(CHR_FONT), MC_TILE16 | MC_4BPP, 64, 32, 3);
  mc_text_clear(L_TEXT);
}

// ---------------------------------------------------------------- palettes
static void two(int pal, int r, int g, int b) { set_rgb(pal, 1, r, g, b); set_rgb(pal, 2, r * 3 / 5, g * 3 / 5, b * 3 / 5); }
static void make_palettes(void) {
  // green phosphor: INK_WHITE is the bright green, INK_GREY the dim one. Indices 1-4 of an ink
  // palette are the font's coverage levels, blended with the black background.
  static const uint8_t ink[7][3] = { {90, 255, 90}, {255, 230, 60}, {80, 230, 255}, {255, 150, 40},
                                     {240, 80, 255}, {40, 160, 40}, {180, 255, 180} };
  static const uint8_t cover[4] = { 100, 72, 47, 22 };   // percent
  for (int p = 0; p < 7; p++)
    for (int k = 0; k < 4; k++)
      set_rgb(PAL_INK + p, 1 + k, ink[p][0] * cover[k] / 100, ink[p][1] * cover[k] / 100, ink[p][2] * cover[k] / 100);
  for (int k = 0; k < 4; k++) set_rgb(PAL_FONT, 1 + k, 255 * cover[k] / 100, 255 * cover[k] / 100, 255 * cover[k] / 100);
  set_rgb(PAL_STAT, C_FRAME, 40, 200, 40);
  set_rgb(PAL_STAT, C_DIM, 20, 70, 20);
  set_rgb(PAL_STAT, C_HALO, 20, 90, 20);
  set_rgb(PAL_STAT, C_WALL, 60, 240, 60);
  set_rgb(PAL_STAT, C_WALLHI, 60, 240, 60);
  set_rgb(PAL_STAT, C_BHALO, 10, 70, 90);
  set_rgb(PAL_STAT, C_BOUNCE, 60, 220, 255);
  set_rgb(PAL_STAT, C_BHI, 60, 220, 255);
  set_rgb(PAL_STAT, C_NOPOD, 70, 18, 18);
  set_rgb(PAL_STAT, C_BASE, 60, 240, 60);
  set_rgb(PAL_STAT, C_TITLE1, 15, 70, 15);
  set_rgb(PAL_STAT, C_TITLE2, 50, 210, 50);
  set_rgb(PAL_STAT, C_TITLE3, 170, 255, 170);
  set_rgb(PAL_DYN, 4, 20, 90, 20);                 // aim line
  set_rgb(PAL_DYN, 5, 60, 240, 60);                // barrel
  two(PAL_PULL, 255, 150, 20);                     // the player's pods: orange (pull), yellow (push)
  two(PAL_PUSH, 255, 240, 60);
  two(PAL_PULLFIX, 255, 40, 40); set_rgb(PAL_PULLFIX, 2, 120, 20, 20);   // fixed pods: red
  two(PAL_PUSHFIX, 200, 40, 160); set_rgb(PAL_PUSHFIX, 2, 100, 20, 80);
  two(PAL_BAD, 90, 90, 90);
  two(PAL_TARGET, 230, 60, 255);
  two(PAL_SHOT, 255, 255, 255);
  set_rgb(PAL_CURSOR, 1, 150, 255, 150);
  set_rgb(PAL_AIM, 1, 255, 230, 60);
  set_rgb(PAL_DIM, 1, 90, 80, 30);
  for (int k = 0; k < 4; k++) {
    int m = 256 - k * 60;
    set_rgb(PAL_SPARK + k, 1, 200 * m / 256, 255 * m / 256, 200 * m / 256);
    set_rgb(PAL_SPARK + k, 2, 40 * m / 256, 180 * m / 256, 40 * m / 256);
  }
  mc_color(BG_ENTRY, MC_RGB(0, 0, 0));
}

// ---------------------------------------------------------------- sound
static int sfx_click, sfx_click_f, sfx_fire, sfx_bounce, sfx_crash, sfx_win, sfx_buzz;
static void sfx_update(void) {
  if (sfx_click > 0) { sfx_click--; mc_psg(0, PSG_SQUARE, (uint32_t)sfx_click_f, 64, sfx_click * 18, sfx_click * 18); }
  if (sfx_fire > 0) { sfx_fire--; mc_psg(1, PSG_SAW, (uint32_t)(900 - sfx_fire * 20), 0, sfx_fire * 5, sfx_fire * 5);
                      if (!sfx_fire) mc_psg_volume(1, 0, 0); }
  if (sfx_bounce > 0) { sfx_bounce--; mc_psg(2, PSG_TRIANGLE, (uint32_t)(1400 + sfx_bounce * 40), 0, sfx_bounce * 14, sfx_bounce * 14); }
  if (sfx_crash > 0) { sfx_crash--; mc_psg(3, PSG_NOISE, (uint32_t)(400 + sfx_crash * 60), 0, sfx_crash * 4, sfx_crash * 4); }
  if (sfx_win > 0) {                          // a rising arpeggio
    sfx_win--;
    static const uint16_t notes[8] = { 523, 659, 784, 1047, 784, 1047, 1319, 1568 };
    int k = 7 - sfx_win / 6;
    mc_psg(4, PSG_SQUARE, notes[clampi(k, 0, 7)], 128, (sfx_win % 6) * 16 + 20, (sfx_win % 6) * 16 + 20);
    if (!sfx_win) mc_psg_volume(4, 0, 0);
  }
  if (sfx_buzz > 0) { sfx_buzz--; mc_psg(5, PSG_SQUARE, 110, 128, sfx_buzz * 10, sfx_buzz * 10); }
}
static void sfx_silence(void) {
  sfx_click = sfx_fire = sfx_bounce = sfx_crash = sfx_win = sfx_buzz = 0;
  for (int c = 0; c < 8; c++) mc_psg_volume(c, 0, 0);
}
static void click(int f) { sfx_click = 6; sfx_click_f = f; }

static const char tune_a[] = "A3 . E4 . A4 . C5 . B4 . E4 . G4 . B4 . F4 . C5 . A4 . F4 . E4 . G#4 . B4 . E5 .";
static const char tune_b[] = "A2 - - - - - - - G2 - - - - - - - F2 - - - - - - - E2 - - - - - - -";
static void music_on(void) { mc_tune(6, tune_a, 10, 34); mc_tune(7, tune_b, 10, 46); }
static void music_off(void) { mc_tune_stop(6); mc_tune_stop(7); }

// ---------------------------------------------------------------- sparks
typedef struct { int x, y, vx, vy, life, max; } spark_t;   // 8.8 fixed point
static spark_t fx[N_FX];
static int fx_next;
static void spark(int x, int y, int speed, int life) {
  spark_t *p = &fx[fx_next]; fx_next = (fx_next + 1) % N_FX;
  int a = (int)rnd(1024), s = speed / 3 + (int)rnd((uint32_t)speed);
  p->x = x << 8; p->y = y << 8;
  p->vx = (mc_cos(a) * s) >> 16; p->vy = (mc_sin(a) * s) >> 16;
  p->max = p->life = life / 2 + (int)rnd((uint32_t)life);
}
static void burst(int x, int y, int n, int speed) { for (int i = 0; i < n; i++) spark(x, y, speed, 30); }
static void fx_update(void) {
  for (int i = 0; i < N_FX; i++) {
    spark_t *p = &fx[i];
    if (p->life <= 0) { mc_sprite_hide(S_FX + i); continue; }
    p->life--; p->x += p->vx; p->y += p->vy; p->vx -= p->vx >> 5; p->vy -= p->vy >> 5;
    int k = 3 - (p->life * 4) / (p->max + 1);
    mc_sprite(S_FX + i, (p->x >> 7) - 4, (p->y >> 7) - 4, CELL_SPARK, 1, 1, PAL_SPARK + clampi(k, 0, 3), 5);
  }
}
static void fx_clear(void) { memset(fx, 0, sizeof fx); for (int i = 0; i < N_FX; i++) mc_sprite_hide(S_FX + i); }

// ---------------------------------------------------------------- input (any pad, keyboard, mouse)
static uint32_t pad_all(void) { return mc_pad(1) | mc_pad(2) | mc_pad(3) | mc_pad(4); }
static uint32_t pressed_all(void) {
  uint32_t p = mc_pad_pressed(1) | mc_pad_pressed(2) | mc_pad_pressed(3) | mc_pad_pressed(4);
  if (mc_key_pressed(KEY_ENTER)) p |= IN_B(1);
  if (mc_key_pressed(KEY_BACKSPACE)) p |= IN_B(2);
  if (mc_key_pressed(KEY_SPACE)) p |= IN_B(3);
  if (mc_key_pressed(KEY_ESC)) p |= IN_START;
  if (mc_key_pressed(KEY_UP)) p |= IN_UP;
  if (mc_key_pressed(KEY_DOWN)) p |= IN_DOWN;
  if (mc_key_pressed(KEY_LEFT)) p |= IN_LEFT;
  if (mc_key_pressed(KEY_RIGHT)) p |= IN_RIGHT;
  return p;
}
static uint32_t held_all(void) {
  uint32_t p = pad_all();
  if (mc_key(KEY_UP)) p |= IN_UP;
  if (mc_key(KEY_DOWN)) p |= IN_DOWN;
  if (mc_key(KEY_LEFT)) p |= IN_LEFT;
  if (mc_key(KEY_RIGHT)) p |= IN_RIGHT;
  if (mc_key(KEY_SPACE)) p |= IN_B(3);
  return p;
}
static bool any_button(void) {
  return (pressed_all() & (IN_B(1) | IN_B(2) | IN_B(3) | IN_START)) || (mc_mouse_pressed() & MOUSE_LEFT);
}

// ---------------------------------------------------------------- save (NVRAM)
#define SAVE_MAGIC 0x31565247u             // "GRV1"
typedef struct { uint32_t magic; uint8_t unlocked; uint8_t best[GV_LEVELS]; } save_t;
static save_t save;
static void save_load(void) {
  for (unsigned i = 0; i < sizeof save; i++) ((uint8_t *)&save)[i] = MC_NVRAM[i];
  if (save.magic != SAVE_MAGIC || save.unlocked == 0 || save.unlocked > GV_LEVELS) {
    memset(&save, 0, sizeof save); save.magic = SAVE_MAGIC; save.unlocked = 1;
  }
}
static void save_store(void) { for (unsigned i = 0; i < sizeof save; i++) MC_NVRAM[i] = ((uint8_t *)&save)[i]; }

// ---------------------------------------------------------------- the level being played
#define MAX_PLAYER 8
typedef struct { int x, y, sign; bool on; } ppod_t;
static int level;
static const gv_level *lv;
static ppod_t pp[MAX_PLAYER];
static int angle, shots;
static gv_world world;
static int cur_x, cur_y, cur_hold;         // cursor, frames a direction has been held
static int held = -1;                      // player pod carried, HELD_AIM, or -1
#define HELD_AIM 100
static int sel = 1;                        // type to put next: +1 pull, -1 push
static bool pod_mode;                      // levels with pods to place: B2 switches aiming / placing
static int aim_hold;
static int trace_slot, trace_count;
static uint32_t t_frame;

static int count_on(int sign) { int n = 0; for (int i = 0; i < MAX_PLAYER; i++) if (pp[i].on && pp[i].sign == sign) n++; return n; }
static int left_of(int sign) { return (sign > 0 ? lv->n_pull : lv->n_push) - count_on(sign); }
static bool aim_free(void) { return lv->amax > lv->amin; }
static bool can_place_any(void) { return lv->n_pull + lv->n_push > 0; }

// the world as the physics sees it: fixed pods, then the player's (skip = one not counted)
static void build_world(int skip) {
  gv_world_init(&world, lv);
  for (int i = 0; i < MAX_PLAYER; i++)
    if (pp[i].on && i != skip) {
      gv_pod p = { (int16_t)pp[i].x, (int16_t)pp[i].y, (int8_t)pp[i].sign, GV_POD_POWER };
      world.pods[world.n_pods++] = p;
    }
}

// ---------------------------------------------------------------- scenery
// the frame of the playfield (screen pixels), with a gap where a launcher sits on it (world lx, ly)
static void draw_frame(int lx, int ly) {
  int y0 = 2 * GV_TOP - 2;
  for (int x = 0; x < HW; x++) { bm_plot(CHR_STAT, x, y0, C_FRAME); bm_plot(CHR_STAT, x, HH - 1, C_FRAME); }
  for (int y = y0; y < HH; y++) { bm_plot(CHR_STAT, 0, y, C_FRAME); bm_plot(CHR_STAT, HW - 1, y, C_FRAME); }
  if (lx < -100) return;
  int cx = 2 * lx, cy = 2 * ly;
  for (int y = cy - 18; y <= cy + 18; y++) for (int x = cx - 18; x <= cx + 18; x++) {
    int dx = x - cx, dy = y - cy;
    if (dx * dx + dy * dy <= 18 * 18 && bm_get(CHR_STAT, x, y)) bm_plot(CHR_STAT, x, y, 0);
  }
}
// launcher: the part of a circle inside the playfield
static void draw_launcher(void) {
  int cx = 2 * lv->lx, cy = 2 * lv->ly;
  for (int a = 0; a < 1024; a++) {
    int x = cx + FIX_INT(mc_cos(a) * 18 + 32768), y = cy + FIX_INT(mc_sin(a) * 18 + 32768);
    if (x < 0 || x >= HW || y < 2 * GV_TOP - 2 || y >= HH) continue;
    bm_plot(CHR_STAT, x, y, C_BASE);
  }
}
// a wall: the outline of a bar 5 world pixels wide around the segment
static void draw_wall(const gv_wall *w) {
  int dx = w->x2 - w->x1, dy = w->y2 - w->y1;
  int len = (int)mc_isqrt((uint32_t)(dx * dx + dy * dy)); if (!len) len = 1;
  int nx = (-dy * 4 + (dy > 0 ? -len / 2 : len / 2)) / len, ny = (dx * 4 + (dx > 0 ? len / 2 : -len / 2)) / len;
  int ex = (dx * 4 + (dx > 0 ? len / 2 : -len / 2)) / len, ey = (dy * 4 + (dy > 0 ? len / 2 : -len / 2)) / len;
  int ax = 2 * w->x1 - ex, ay = 2 * w->y1 - ey, bx = 2 * w->x2 + ex, by = 2 * w->y2 + ey;
  int c = w->kind == W_BOUNCE ? C_BOUNCE : C_WALL;
  bm_line(CHR_STAT, ax + nx, ay + ny, bx + nx, by + ny, 0, c);
  bm_line(CHR_STAT, ax - nx, ay - ny, bx - nx, by - ny, 0, c);
  bm_line(CHR_STAT, ax + nx, ay + ny, ax - nx, ay - ny, 0, c);
  bm_line(CHR_STAT, bx + nx, by + ny, bx - nx, by - ny, 0, c);
}
static void draw_level(void) {
  pool_reset();
  for (int i = 0; i < lv->n_nopod; i++) {          // forbidden zones: hatching
    const gv_rect *r = &lv->nopod[i];
    bm_hatch(CHR_STAT, 2 * r->x0, 2 * r->y0, 2 * r->x1 + 1, 2 * r->y1 + 1, C_NOPOD);
  }
  draw_frame(lv->lx, lv->ly);
  for (int i = 0; i < lv->n_walls; i++) draw_wall(&lv->walls[i]);
  draw_launcher();
}

// aim line and barrel, on the trace bitmap (screen pixels): their pixels are remembered to be wiped
// later, and the aim line never covers a trace
#define AIM_PIX 1600
static uint16_t aim_px[AIM_PIX], aim_py[AIM_PIX];
static int aim_n, aim_drawn = -9999;
static void aim_put(int x, int y, int c, bool over) {
  if ((unsigned)x >= HW || (unsigned)y >= HH || aim_n >= AIM_PIX) return;
  int v = bm_get(CHR_DYN, x, y);
  if (v && !over) return;
  if (v == 5 || v == 4) return;
  bm_plot(CHR_DYN, x, y, c);
  aim_px[aim_n] = (uint16_t)x; aim_py[aim_n] = (uint16_t)y; aim_n++;
}
static void aim_erase(void) {
  for (int i = 0; i < aim_n; i++) { int v = bm_get(CHR_DYN, aim_px[i], aim_py[i]); if (v == 4 || v == 5) bm_plot(CHR_DYN, aim_px[i], aim_py[i], 0); }
  aim_n = 0; aim_drawn = -9999;
}
static void aim_draw(void) {
  if (aim_drawn == angle) return;
  aim_erase();
  int32_t c, s; gv_dir(angle, &c, &s);
  int32_t ox = (2 * lv->lx) << 16, oy = (2 * lv->ly) << 16;
  int32_t x = ox + c * 36, y = oy + s * 36;
  for (int i = 0; i < 900; i++) {                  // the long guide, a dimmed line
    x += c; y += s;
    int px = x >> 16, py = y >> 16;
    if (px < 1 || px >= HW - 1 || py < 2 * GV_TOP || py >= HH - 1) break;
    aim_put(px, py, 4, false);
  }
  // the barrel: a 28 x 10 outline from the centre outwards
  int32_t nx = -s * 5, ny = c * 5;
  int32_t x0 = ox + c * 8, y0 = oy + s * 8, x1 = ox + c * 36, y1 = oy + s * 36;
  for (int k = 0; k < 4; k++) {
    int32_t ax, ay, bx, by;
    if (k == 0) { ax = x0 + nx; ay = y0 + ny; bx = x1 + nx; by = y1 + ny; }
    else if (k == 1) { ax = x0 - nx; ay = y0 - ny; bx = x1 - nx; by = y1 - ny; }
    else if (k == 2) { ax = x1 + nx; ay = y1 + ny; bx = x1 - nx; by = y1 - ny; }
    else { ax = x0 + nx; ay = y0 + ny; bx = x0 - nx; by = y0 - ny; }
    for (int i = 0; i <= 32; i++)
      aim_put((ax + (int32_t)(((int64_t)(bx - ax) * i) >> 5) + 32768) >> 16, (ay + (int32_t)(((int64_t)(by - ay) * i) >> 5) + 32768) >> 16, 5, true);
  }
  aim_drawn = angle;
}
// the angle, in degrees with a tenth, near the launcher ("-30.3"): 0 = right, minus = up
static void angle_text(void) {
  int d = angle % 3600; if (d > 1800) d -= 3600; if (d < -1800) d += 3600;   // tenths, up is negative
  char s[10]; int k = 0;
  if (d < 0) { s[k++] = '-'; d = -d; }
  if (d >= 1000) s[k++] = (char)('0' + d / 1000);
  if (d >= 100) s[k++] = (char)('0' + d / 100 % 10);
  s[k++] = (char)('0' + d / 10 % 10); s[k++] = '.'; s[k++] = (char)('0' + d % 10);
  while (k < 6) s[k++] = ' ';
  s[k] = 0;
  int col = lv->lx / 8, row = lv->ly / 8 + 2;
  if (lv->ly >= SCR_H - 16) { row = lv->ly / 8 - 3; col = lv->lx / 8 + 2; }
  if (lv->lx <= 8) col = 1;
  text(clampi(col, 0, 34), clampi(row, 2, 28), s, INK_WHITE);
}

// traces of the last three shots: slot colours, newest brightest
static void trace_colours(void) {
  static const uint8_t c[3][3] = { {235, 235, 235}, {130, 130, 130}, {70, 70, 70} };
  for (int s = 0; s < 3; s++) {
    int age = (trace_slot - s + 3) % 3;
    if (age >= trace_count) age = 2;
    set_rgb(PAL_DYN, 1 + s, c[age][0], c[age][1], c[age][2]);
  }
}
static void trace_dot(int32_t x, int32_t y, int col) {   // world 16.16 -> a 2 x 2 dot on the screen
  int px = x >> 15, py = y >> 15;
  bm_plot(CHR_DYN, px, py, col); bm_plot(CHR_DYN, px + 1, py, col); bm_plot(CHR_DYN, px, py + 1, col); bm_plot(CHR_DYN, px + 1, py + 1, col);
}
static void trace_new(void) {                      // next slot, wiping the shot it held
  trace_slot = (trace_slot + 1) % 3;
  if (trace_count < 3) trace_count++;
  bm_wipe(CHR_DYN, 1 + trace_slot);
  trace_colours();
}

// ---------------------------------------------------------------- sprites of the level
static void pod_sprite(int n, int x, int y, int sign, bool fixed, int power, bool bad, uint32_t t) {
  (void)t;
  int pal = bad ? PAL_BAD : fixed ? (sign > 0 ? PAL_PULLFIX : PAL_PUSHFIX) : (sign > 0 ? PAL_PULL : PAL_PUSH);
  bool big = power > GV_POD_POWER + 40;
  if (big) mc_sprite(S_POD + n, 2 * x - 32, 2 * y - 32, sign > 0 ? CELL_BIGPOD : CELL_BIGPUSH, 8, 8, pal, 4);
  else mc_sprite(S_POD + n, 2 * x - 16, 2 * y - 16, sign > 0 ? CELL_POD : CELL_PODPUSH, 4, 4, pal, 4);
  mc_sprite_hide(S_RING + n);
}
static void draw_target(uint32_t t) {
  mc_sprite(S_TARGET, 2 * lv->tx - 16, 2 * lv->ty - 16, CELL_TGT + (int)((t / 6) % 4) * 16, 4, 4, PAL_TARGET, 4);
  mc_sprite_hide(S_TRING);
}
static void draw_pods(uint32_t t) {
  int n = 0;
  for (int i = 0; i < lv->n_fixed && n < 16; i++, n++)
    pod_sprite(n, lv->fixed[i].x, lv->fixed[i].y, lv->fixed[i].sign, true, lv->fixed[i].power, false, t);
  for (int i = 0; i < MAX_PLAYER && n < 16; i++) {
    if (!pp[i].on || i == held) continue;
    pod_sprite(n++, pp[i].x, pp[i].y, pp[i].sign, false, GV_POD_POWER, false, t);
  }
  for (; n < 16; n++) { mc_sprite_hide(S_POD + n); mc_sprite_hide(S_RING + n); }
}
static void draw_aim(bool show) { if (show) aim_draw(); else aim_erase(); }

// ---------------------------------------------------------------- HUD
static void num2(char *s, int v) { s[0] = (char)('0' + v / 10 % 10); s[1] = (char)('0' + v % 10); }
static const char *const level_hint[GV_LEVELS] = {
  "VISEZ AVEC HAUT ET BAS, TIREZ AVEC B3\nCHAQUE TIR LAISSE SA TRACE.",
  "UN PUITS ROUGE ATTIRE LE TIR.\nPASSEZ A COTE SANS TOMBER DEDANS.",
  "LE PUITS VOUS AIDERA A REDESCENDRE\nDERRIERE LE MUR.",
  "VISEZ LA FENETRE.",
  "LES MURS BLEUS FONT REBONDIR LE TIR.",
  "DEUX PUITS, UN MUR.\nLE DIXIEME DE DEGRE COMPTE.",
  "CET ASTRE ATTIRE TRES FORT.",
  "UN REBOND, PUIS LE PUITS VOUS\nPOSE DANS LA COUPE.",
  "TIREZ VERS LE BAS : LES PUITS FERONT\nFAIRE AU TIR LE TOUR DE L'ETAGERE.",
  "TOUTE UNE CONSTELLATION A TRAVERSER.",
  "CONTOURNEZ LE MUR PAR LA DROITE\nET REVENEZ PAR-DESSOUS.",
  "DEUX TROUS D'AIGUILLE.",
  "A VOUS DE POSER : B2, PUIS B1.\nUN PUITS PEUT ALLER DANS UN MUR.",
  "UN PUITS ORANGE ATTIRE, LE JAUNE\nREPOUSSE (B4 CHANGE DE TYPE).",
  "RIEN NE SE POSE DANS LES ZONES\nHACHUREES.",
  "SUIVEZ LE COULOIR.",
  "ENTREZ DANS LE COFFRE PAR LE HAUT.",
  "LE TROU NOIR AVALE TOUT CE QUI\nS'APPROCHE TROP.",
  "UN VIRAGE SERRE APRES L'AUTRE.",
  "TOUT CE QUE VOUS AVEZ APPRIS.\nBONNE CHANCE.",
};
static void inventory(void) {                       // dots for the pods left: yellow pull, orange push
  int n = 0, x = 4;
  for (int pass = 0; pass < 2; pass++) {
    int sign = pass == 0 ? 1 : -1, k = left_of(sign);
    for (int i = 0; i < k && n < N_INV; i++, n++, x += 6)
      mc_sprite(S_INV + n, 2 * x - 4, HH - 22, CELL_DOT, 1, 1, sel == sign ? (sign > 0 ? PAL_PULL : PAL_PUSH) : PAL_DIM, 6);
    if (k) x += 6;
  }
  for (; n < N_INV; n++) mc_sprite_hide(S_INV + n);
}
static void hud(void) {
  char s[48];
  memset(s, ' ', 40); s[40] = 0;
  text(0, 0, s, INK_WHITE);
  if (level >= 0) {
    s[0] = 'N'; s[1] = 'I'; s[2] = 'V'; s[3] = 'E'; s[4] = 'A'; s[5] = 'U'; s[6] = ' '; num2(s + 7, level + 1); s[9] = 0;
    text(0, 0, s, INK_GREY);
    text(10, 0, lv->name, INK_WHITE);
  } else text(0, 0, lv->name, INK_ORANGE);
  char t[12] = "TIRS 00/20"; num2(t + 5, shots); num2(t + 8, GV_SHOTS); text(30, 0, t, shots >= GV_SHOTS - 3 ? INK_ORANGE : INK_WHITE);
  inventory();
}
static void level_text(void) {                      // the advice of the level, top left, until the first shot
  text(1, 2, "                                      ", INK_GREY);
  text(1, 3, "                                      ", INK_GREY);
  if (shots > 0) return;
  const char *h = level >= 0 ? level_hint[level] : "VOTRE NIVEAU : A VOUS DE JOUER !\nSTART OU B6 : RETOUR CONSTRUCTION", *nl = strchr(h, '\n');
  char l1[40]; int n = nl ? (int)(nl - h) : (int)strlen(h);
  memcpy(l1, h, (size_t)n); l1[n] = 0;
  text(1, 2, l1, INK_GREY);
  if (nl) text(1, 3, nl + 1, INK_GREY);
}
static void hint(const char *s) {
  char b[41]; memset(b, ' ', 40); b[40] = 0; text(0, 28, b, INK_WHITE);
  text((40 - (int)strlen(s)) - 1, 28, s, INK_GREY);
}

// ---------------------------------------------------------------- vector title
// strokes on a 4 x 6 grid: pairs of points, -1 = pen up, -2 = end
static const int8_t L_G[] = {4,1, 3,0, 1,0, 0,1, 0,5, 1,6, 3,6, 4,5, 4,3, 2,3, -2};
static const int8_t L_R[] = {0,6, 0,0, 3,0, 4,1, 4,2, 3,3, 0,3, -1, 2,3, 4,6, -2};
static const int8_t L_A[] = {0,6, 0,2, 2,0, 4,2, 4,6, -1, 0,4, 4,4, -2};
static const int8_t L_V[] = {0,0, 2,6, 4,0, -2};
static const int8_t L_I[] = {1,0, 3,0, -1, 2,0, 2,6, -1, 1,6, 3,6, -2};
static const int8_t L_T[] = {0,0, 4,0, -1, 2,0, 2,6, -2};
static const int8_t L_O[] = {1,0, 3,0, 4,1, 4,5, 3,6, 1,6, 0,5, 0,1, 1,0, -2};
static const int8_t L_N[] = {0,6, 0,0, 4,6, 4,0, -2};
static void vector_word(const int8_t *const *letters, int n, int x0, int y0, int sc, int r, int c) {
  for (int k = 0; k < n; k++) {
    const int8_t *p = letters[k];
    int px = -1, py = -1, x = x0 + k * (5 * sc - sc / 2);
    for (; *p != -2; ) {
      if (*p == -1) { px = -1; p++; continue; }
      int nx = x + p[0] * sc, ny = y0 + p[1] * sc;
      if (px >= 0) bm_line(CHR_STAT, px, py, nx, ny, r, c);
      px = nx; py = ny; p += 2;
    }
  }
}
static void draw_title(void) {
  static const int8_t *const word[8] = { L_G, L_R, L_A, L_V, L_I, L_T, L_O, L_N };
  pool_reset();
  draw_frame(-999, 0);
  int sc = 14, w = 8 * (5 * sc - sc / 2) - sc, x0 = (HW - w) / 2, y0 = 68;
  vector_word(word, 8, x0, y0, sc, 2, C_TITLE1);
  vector_word(word, 8, x0, y0, sc, 1, C_TITLE3);
}

// ---------------------------------------------------------------- display set-up
static void display_play(void) {
  mc_display(DISP_ON | DISP_LAYER(L_TEXT) | DISP_LAYER(L_DYN) | DISP_LAYER(L_STAT) | DISP_SPRITES);
}
static void hide_all(void) {
  for (int i = 0; i < S_USED; i++) mc_sprite_hide(i);
  for (int i = 0; i < S_USED; i++) { mc_sprite_zoom(i, 0x100, 0x100); mc_sprite_flip(i, false, false); }
}

// ---------------------------------------------------------------- title screen (orbit demo)
static void title_screen(void) {
  sfx_silence(); hide_all(); fx_clear(); text_clear();
  draw_title();
  bm_clear(CHR_DYN);
  trace_slot = 0; trace_count = 1;
  set_rgb(PAL_DYN, 1, 235, 235, 235); set_rgb(PAL_DYN, 2, 130, 130, 130); set_rgb(PAL_DYN, 3, 70, 70, 70);
  text_c(15, "VISEZ AU DIXIEME DE DEGRE, COURBEZ", INK_GREY);
  text_c(16, "LE TIR, ATTEIGNEZ LA CIBLE.", INK_GREY);
  text_c(27, "MONOCORE-1  2026", INK_GREY);
  display_play();
  music_on();
  // demo: a shot in orbit around a planet, its trace fading
  static const gv_pod demo_pods[] = { { 160, 176, 1, 150 } };
  static const gv_level demo = { "DEMO", 160, 222, 0, 0, 0, 52, -100, -100, 0, 0, demo_pods, 1, 0, 0, 0, 0 };
  gv_world dw; gv_world_init(&dw, &demo);
  gv_body b; gv_launch(&b, &demo, 0);
  b.x = 160 << 16; b.y = 218 << 16; b.vx = 141000; b.vy = 0;   // about 2.15 px a frame: an ellipse
  int slot = 0;
  for (uint32_t t = 0;; t++) {
    mc_frame(); sfx_update(); fx_update();
    if ((t / 30) & 1) text_c(22, "APPUYEZ SUR UN BOUTON", INK_YELLOW); else text_c(22, "                     ", INK_YELLOW);
    for (int s = 0; s < GV_SUB; s++) gv_step(&dw, &b);
    trace_dot(b.x, b.y, 1 + slot);
    if (t % 160 == 159) {                          // rotate the trace colours
      slot = (slot + 1) % 3;
      bm_wipe(CHR_DYN, 1 + slot);
      for (int k = 0; k < 3; k++) {
        static const uint8_t c[3][3] = { {235, 235, 235}, {130, 130, 130}, {70, 70, 70} };
        int age = (slot - k + 3) % 3;
        set_rgb(PAL_DYN, 1 + k, c[age][0], c[age][1], c[age][2]);
      }
    }
    pod_sprite(0, 160, 176, 1, true, 150, false, t);
    mc_sprite(S_SHOT, (b.x >> 15) - 3, (b.y >> 15) - 3, CELL_SHOT, 1, 1, PAL_SHOT, 5);
    if (t > 20 && any_button()) { click(880); break; }
  }
  music_off();
}

// ---------------------------------------------------------------- level select
static int select_screen(int start) {
  hide_all(); fx_clear(); text_clear();
  bm_clear(CHR_DYN);
  draw_title();
  int c = start == GV_LEVELS ? GV_LEVELS : clampi(start, 0, save.unlocked - 1);
  text_c(12, "CHOIX DU NIVEAU", INK_PURPLE);
  hint("B1: JOUER   START: TITRE");
  text(1, 13, "EN VERT : GAGNE (RECORD EN TIRS)", INK_GREY);
  display_play();
  for (;;) {
    mc_frame(); sfx_update();
    for (int i = 0; i < GV_LEVELS; i++) {
      int col = (i < 10) ? 0 : 20, row = 14 + (i % 10);
      char s[24]; memset(s, ' ', 20); s[20] = 0;
      bool open = i < save.unlocked;
      s[0] = i == c ? '>' : ' ';
      num2(s + 1, i + 1);
      if (open) {
        const char *n = gv_levels[i].name; int k = 0;
        for (; n[k] && k < 14; k++) s[4 + k] = n[k];
        if (save.best[i]) { num2(s + 18, save.best[i]); }
      } else { s[4] = '-'; s[5] = '-'; s[6] = '-'; }
      int ink = !open ? INK_GREY : i == c ? INK_YELLOW : save.best[i] ? INK_GREEN : INK_WHITE;
      text(col, row, s, ink);
    }
    text(1, 25, c == GV_LEVELS ? "> CONSTRUIRE UN NIVEAU" : "  CONSTRUIRE UN NIVEAU", c == GV_LEVELS ? INK_YELLOW : INK_ORANGE);
    uint32_t p = pressed_all();
    int old = c;
    if (c == GV_LEVELS) {                            // the building entry, under the left column
      if (p & IN_UP) c = 9 < save.unlocked ? 9 : save.unlocked - 1;
    } else {
      if (p & IN_UP) c = c % 10 == 0 ? c : c - 1;
      if (p & IN_DOWN) c = (c % 10 == 9 || c + 1 >= GV_LEVELS) ? GV_LEVELS : c + 1;
      if (p & IN_LEFT) c = c >= 10 ? c - 10 : c;
      if (p & IN_RIGHT) c = c + 10 < GV_LEVELS ? c + 10 : c;
      if (c >= save.unlocked && c != GV_LEVELS) c = (p & IN_DOWN) ? GV_LEVELS : old;
    }
    if (c != old) click(660);
    if (p & (IN_B(1) | IN_B(3)) || (mc_mouse_pressed() & MOUSE_LEFT)) { click(990); return c; }
    if (p & IN_START) return -1;
  }
}

// ---------------------------------------------------------------- playing a level
static void mode_text(void);
static void level_start_lv(const gv_level *L, int n) {
  level = n; lv = L;
  memset(pp, 0, sizeof pp);
  angle = lv->angle; if (angle < lv->amin || angle > lv->amax) angle = (lv->amin + lv->amax) / 2;
  shots = 0; held = -1; pod_mode = false; aim_hold = 0;
  sfx_silence();
  sel = lv->n_pull ? 1 : -1;
  cur_x = SCR_W / 2; cur_y = SCR_H / 2; cur_hold = 0;
  hide_all(); fx_clear(); text_clear();
  draw_level();
  bm_clear(CHR_DYN);
  trace_slot = 0; trace_count = 0; trace_colours();
  aim_n = 0; aim_drawn = -9999;
  hud(); level_text(); angle_text(); mode_text();
  display_play();
  if (mc_mouse_present()) mc_mouse_set(2 * cur_x, 2 * cur_y);
}

static int near_player_pod(int x, int y) {
  int best = -1, bd = 10 * 10;
  for (int i = 0; i < MAX_PLAYER; i++) {
    if (!pp[i].on || i == held) continue;
    int dx = pp[i].x - x, dy = pp[i].y - y, d = dx * dx + dy * dy;
    if (d <= bd) { bd = d; best = i; }
  }
  return best;
}
static bool can_put(int x, int y, int skip) { build_world(skip); return gv_can_place(&world, x, y, -1); }

static void move_cursor(void) {
  uint32_t h = held_all();
  int dx = 0, dy = 0;
  if (h & IN_LEFT) dx--;
  if (h & IN_RIGHT) dx++;
  if (h & IN_UP) dy--;
  if (h & IN_DOWN) dy++;
  if (!pod_mode) dx = dy = 0;                      // the pad aims instead
  if (dx || dy) cur_hold++; else cur_hold = 0;
  int sp = cur_hold < 6 ? 1 : cur_hold < 20 ? 2 : 4;
  cur_x += dx * sp; cur_y += dy * sp;
  int sx = mc_stick_x(1, 0), sy = mc_stick_y(1, 0);
  if (iabs(sx) > 24) cur_x += sx / 24;
  if (iabs(sy) > 24) cur_y += sy / 24;
  static int mx = -1, my = -1;
  if (mc_mouse_present()) {
    int nx = mc_mouse_x() / 2, ny = mc_mouse_y() / 2;
    if (nx != mx || ny != my) { cur_x = nx; cur_y = ny; }
    mx = nx; my = ny;
  }
  cur_x = clampi(cur_x, 0, SCR_W - 1); cur_y = clampi(cur_y, GV_TOP, SCR_H - 1);
  if (mc_mouse_present()) { mc_mouse_set(2 * cur_x, 2 * cur_y); mx = cur_x; my = cur_y; }
}

static void mode_text(void) {
  text(1, 29, "START/B6 : MENU", INK_GREY);
  if (!can_place_any()) return;
  text(29, 29, pod_mode ? "MODE PUITS" : "MODE VISEE", pod_mode ? INK_ORANGE : INK_GREY);
}

// returns 1 when the shot is fired, 2 to go to the menu
static int edit_frame(uint32_t t) {
  move_cursor();
  uint32_t p = pressed_all();
  uint32_t m = mc_mouse_pressed();
  int wheel = mc_mouse_wheel();
  if (m & MOUSE_LEFT) { if (!pod_mode && can_place_any()) { pod_mode = true; mode_text(); } p |= IN_B(1); }
  if (m & MOUSE_RIGHT) p |= IN_B(5);
  if (m & MOUSE_MIDDLE) p |= IN_B(3);
  if (!can_place_any()) pod_mode = false;

  // aiming: up / down (keyboard arrows too), 0.1 degree a step, faster when held; wheel 0.1 a notch
  uint32_t h = held_all();
  int d = 0;
  // in the PUITS mode the arrows move the cursor: the wheel still aims
  int up = pod_mode ? 0 : ((h & (IN_UP | IN_LEFT)) ? 1 : 0);
  int dn = pod_mode ? 0 : ((h & (IN_DOWN | IN_RIGHT)) ? 1 : 0);
  if (up || dn) {
    aim_hold++;
    int step = aim_hold == 1 ? 1 : aim_hold < 15 ? 0 : aim_hold < 45 ? 1 : aim_hold < 90 ? 3 : 10;
    d = (dn - up) * step;
  } else aim_hold = 0;
  d -= wheel;
  if (d && aim_free()) angle = clampi(angle + d, lv->amin, lv->amax);
  static int shown = -9999;
  if (angle != shown) { angle_text(); shown = angle; }

  if ((p & IN_B(2)) && can_place_any()) {
    if (held >= 0) { pp[held].on = false; held = -1; hud(); }
    pod_mode = !pod_mode; click(pod_mode ? 700 : 500); mode_text();
  }
  if (pod_mode && (p & IN_B(1))) {
    if (held >= 0) {
      if (can_put(cur_x, cur_y, held)) { pp[held].x = cur_x; pp[held].y = cur_y; held = -1; click(520); }
      else sfx_buzz = 8;
    } else {
      int k = near_player_pod(cur_x, cur_y);
      if (k >= 0) { held = k; click(780); }
      else if (left_of(sel) > 0 && can_put(cur_x, cur_y, -1)) {
        for (int i = 0; i < MAX_PLAYER; i++) if (!pp[i].on) {
          pp[i].on = true; pp[i].x = cur_x; pp[i].y = cur_y; pp[i].sign = sel; break; }
        click(sel > 0 ? 440 : 620);
        if (left_of(sel) == 0 && left_of(-sel) > 0) sel = -sel;
        hud();
      } else sfx_buzz = 8;
    }
  }
  if (pod_mode && (p & IN_B(5))) {                  // remove (right click, B5)
    int k = held >= 0 ? held : near_player_pod(cur_x, cur_y);
    if (k >= 0) { pp[k].on = false; if (held == k) held = -1; click(300); sel = pp[k].sign; hud(); }
  }
  if (pod_mode && (p & IN_B(4)) && lv->n_pull && lv->n_push && held < 0) { sel = -sel; click(sel > 0 ? 440 : 620); hud(); }
  if (p & (IN_START | IN_B(6))) return 2;

  draw_pods(t);
  draw_target(t);
  draw_aim(true);
  mc_sprite_hide(S_SHOT);
  if (pod_mode && held >= 0) {
    bool ok = can_put(cur_x, cur_y, held);
    pod_sprite(15, cur_x, cur_y, pp[held].sign, false, GV_POD_POWER, !ok, t);
    mc_sprite_hide(S_CURSOR);
  } else if (pod_mode) {
    mc_sprite(S_CURSOR, 2 * cur_x - 16, 2 * cur_y - 16, CELL_CURSOR, 4, 4, PAL_CURSOR, 7);
    if (near_player_pod(cur_x, cur_y) >= 0) hint("B1 PRENDRE  B5 RETIRER  B2 VISEE");
    else if (left_of(sel) > 0) hint(sel > 0 ? "B1 POSER  B4 TYPE  B2 VISEE  B3 TIR" : "B1 REPOUSSOIR  B4 TYPE  B2 VISEE");
    else hint("B2 VISEE   B3 TIR");
  } else {
    mc_sprite_hide(S_CURSOR);
    hint(can_place_any() ? "HAUT/BAS VISER  B2 PUITS  B3 TIR" : "HAUT/BAS VISER   B3 TIR");
  }
  if (held < 0 && (p & IN_B(3))) return 1;
  return 0;
}

// flies the shot; returns the final event
static int fly(void) {
  if (held >= 0 && held != HELD_AIM) pp[held].on = true;   // a carried pod is dropped where it was
  held = -1;
  build_world(-1);
  gv_body b; gv_launch(&b, lv, angle);
  shots++; hud(); if (shots == 1) level_text();
  trace_new();
  int col = 1 + trace_slot;
  mc_sprite_hide(S_CURSOR); mc_sprite_hide(S_HELD); draw_aim(false);
  sfx_fire = 24;
  hint("B3: ACCELERER   B2: ABANDONNER");
  for (uint32_t t = 0;; t++) {
    int steps = (held_all() & IN_B(3)) ? 3 : 1;
    if (pressed_all() & IN_B(2)) return EV_LOST;
    int ev = EV_NONE;
    for (int k = 0; k < steps && (ev == EV_NONE || ev == EV_BOUNCE); k++) {
      ev = EV_NONE;
      for (int s = 0; s < GV_SUB; s++) {
        int e = gv_step(&world, &b);
        if (s == 0) trace_dot(b.x, b.y, col);
        if (e == EV_BOUNCE) { sfx_bounce = 8; burst(b.x >> 16, b.y >> 16, 4, 80); ev = EV_BOUNCE; }
        else if (e != EV_NONE) { ev = e; break; }
      }
      if (ev == EV_NONE || ev == EV_BOUNCE) { if (++b.frame >= GV_LIFE) ev = EV_LOST; }
    }
    int x = b.x >> 16, y = b.y >> 16;
    mc_sprite(S_SHOT, (b.x >> 15) - 3, (b.y >> 15) - 3, CELL_SHOT, 1, 1, PAL_SHOT, 5);
    draw_pods(t_frame + t); draw_target(t_frame + t);
    mc_frame(); sfx_update(); fx_update();
    if (ev != EV_NONE && ev != EV_BOUNCE) {
      mc_sprite_hide(S_SHOT);
      if (ev == EV_TARGET) { burst(lv->tx, lv->ty, 48, 400); sfx_win = 48; }
      else { burst(x, y, 20, 180); sfx_crash = 20; }
      t_frame += t;
      return ev;
    }
  }
}

static void wait_frames(int n) { for (int i = 0; i < n; i++) { mc_frame(); sfx_update(); fx_update(); draw_target(t_frame++); draw_pods(t_frame); } }

static const char *fail_text(int ev) {
  switch (ev) {
    case EV_WALL: return "LE TIR S'EST ECRASE";
    case EV_POD: return "LE TIR EST TOMBE DANS UN PUITS";
    case EV_OUT: return "LE TIR EST SORTI DE L'ECRAN";
    default: return "LE TIR S'EST PERDU";
  }
}

// pause menu: 0 continue, 1 restart, 2 level select
static int pause_menu(void) {
  static const char *items_n[4] = { "CONTINUER", "RECOMMENCER LE NIVEAU", "CHOIX DU NIVEAU", "QUITTER VERS LE TITRE" };
  static const char *items_e[4] = { "CONTINUER", "RECOMMENCER LE NIVEAU", "RETOUR A LA CONSTRUCTION", "QUITTER VERS LE TITRE" };
  const char *const *items = level >= 0 ? items_n : items_e;
  int c = 0;
  mc_sprite_hide(S_CURSOR);
  for (;;) {
    for (int i = 0; i < 4; i++) {
      char s[32]; memset(s, ' ', 28); s[28] = 0;
      s[0] = i == c ? '>' : ' ';
      for (int k = 0; items[i][k]; k++) s[2 + k] = items[i][k];
      text(6, 11 + i * 2, s, i == c ? INK_YELLOW : INK_WHITE);
    }
    mc_frame(); sfx_update(); draw_target(t_frame++); draw_pods(t_frame);
    uint32_t p = pressed_all();
    if (p & IN_UP && c > 0) { c--; click(660); }
    if (p & IN_DOWN && c < 3) { c++; click(660); }
    if (p & (IN_START | IN_B(6))) c = 0;
    if (p & (IN_B(1) | IN_START | IN_B(6)) || (mc_mouse_pressed() & MOUSE_LEFT)) {
      for (int i = 0; i < 4; i++) text(6, 11 + i * 2, "                            ", INK_WHITE);
      click(990);
      return c;
    }
  }
}

// plays level n; returns the next level, or -1 for the level select
// plays a level; returns the next level, -1 for the level select (or back to building), -2 for
// the title. n < 0: a built level, no record kept.
static int play_lv(const gv_level *L, int n) {
  level_start_lv(L, n);
  for (;;) {
    mc_frame(); sfx_update(); fx_update();
    int r = edit_frame(t_frame++);
    if (r == 2) {
      int m = pause_menu();
      hud(); level_text(); angle_text(); mode_text();
      if (m == 1) { level_start_lv(L, n); continue; }
      if (m == 2) return -1;
      if (m == 3) return -2;
      continue;
    }
    if (r != 1) continue;
    int ev = fly();
    if (ev == EV_TARGET) {
      char s[40] = "CIBLE ATTEINTE EN 00 TIRS";
      num2(s + 18, shots);
      if (shots == 1) { s[21] = 'T'; s[22] = 'I'; s[23] = 'R'; s[24] = 0; }
      if (s[18] == '0') { memmove(s + 18, s + 19, strlen(s + 19) + 1); }
      bool record = false;
      if (n >= 0) {
        record = save.best[n] == 0 || shots < save.best[n];
        if (record) save.best[n] = (uint8_t)clampi(shots, 1, 99);
        if (save.unlocked < GV_LEVELS && save.unlocked <= n + 1) save.unlocked = (uint8_t)(n + 2);
        save_store();
      }
      text_c(12, s, INK_PURPLE);
      if (record && shots > 0) text_c(14, "NOUVEAU RECORD", INK_GREEN);
      hint(n < 0 ? "B1: CONSTRUCTION   B2: REJOUER" : n + 1 < GV_LEVELS ? "B1: SUIVANT  B2: REJOUER  START: CHOIX" : "B1: CONTINUER");
      wait_frames(30);
      bool again = false;
      while (!again) {
        wait_frames(1);
        uint32_t p = pressed_all();
        if (p & IN_B(1) || (mc_mouse_pressed() & MOUSE_LEFT)) return n < 0 ? -1 : n + 1;
        if (p & IN_B(2)) { if (n >= 0) return n; level_start_lv(L, n); again = true; }
        if (p & (IN_START | IN_B(6))) return -1;
      }
    } else {
      text_c(13, fail_text(ev), INK_ORANGE);
      if (shots >= GV_SHOTS) text_c(15, "PLUS DE TIRS : LE NIVEAU RECOMMENCE", INK_YELLOW);
      wait_frames(shots >= GV_SHOTS ? 110 : 50);
      text_c(13, "                              ", INK_WHITE);
      text_c(15, "                                   ", INK_WHITE);
      if (shots >= GV_SHOTS) {                     // new series of shots; the pods stay where they are
        shots = 0; bm_clear(CHR_DYN); aim_n = 0; aim_drawn = -9999;
        trace_slot = 0; trace_count = 0; trace_colours();
      }
      hud(); level_text(); angle_text(); mode_text();
    }
  }
}

static void ending(void) {
  hide_all(); fx_clear(); text_clear();
  draw_title(); bm_clear(CHR_DYN);
  display_play();
  text_c(14, "BRAVO !", INK_YELLOW);
  text_c(16, "TOUS LES NIVEAUX SONT RESOLUS.", INK_WHITE);
  text_c(18, "ESSAYEZ DE BATTRE VOS RECORDS.", INK_GREY);
  music_on();
  for (uint32_t t = 0;; t++) {
    mc_frame(); sfx_update(); fx_update();
    if (t % 6 == 0) burst(40 + (int)rnd(240), 150 + (int)rnd(70), 10, 300);
    if (t > 60 && any_button()) break;
  }
  music_off();
}

static int play_level(int n) { return play_lv(&gv_levels[n], n); }

// ---------------------------------------------------------------- level building
// Six built levels, kept in NVRAM after the save (written back at every change). A built level
// becomes a gv_level like the others: its fixed pods and walls point into the slot.
#define ED_SLOTS 6
#define ED_WALLS 24
#define ED_PODS  10
#define ED_MAGIC 0xA7
#define ED_BASE  256                       // NVRAM offset of the slots
typedef struct {
  uint8_t magic, n_pull, n_push, nw, np, pad[3];
  int16_t lx, ly, tx, ty;
  gv_wall w[ED_WALLS];
  gv_pod p[ED_PODS];
} ed_slot_t;
static ed_slot_t ed;
static int ed_n;                            // slot being edited
static gv_level ed_lv;
static char ed_name[24];

static uint32_t ed_addr(int n) { return ED_BASE + (uint32_t)n * ((sizeof(ed_slot_t) + 15) & ~15u); }
static void ed_default(void) {
  memset(&ed, 0, sizeof ed); ed.magic = ED_MAGIC;
  ed.lx = 0; ed.ly = 126; ed.tx = 290; ed.ty = 126;
}
static bool ed_read(int n, ed_slot_t *o) {
  for (unsigned i = 0; i < sizeof *o; i++) ((uint8_t *)o)[i] = MC_NVRAM[ed_addr(n) + i];
  return o->magic == ED_MAGIC && o->nw <= ED_WALLS && o->np <= ED_PODS && o->n_pull <= 4 && o->n_push <= 3;
}
static void ed_load(int n) { ed_n = n; if (!ed_read(n, &ed)) ed_default(); }
static void ed_store(void) { for (unsigned i = 0; i < sizeof ed; i++) MC_NVRAM[ed_addr(ed_n) + i] = ((uint8_t *)&ed)[i]; }

// the aiming range follows the edge the launcher sits on; away from the edges, all round
static void ed_build(void) {
  gv_level *L = &ed_lv;
  memset(L, 0, sizeof *L);
  memcpy(ed_name, "MON NIVEAU 0", 13); ed_name[11] = (char)('1' + ed_n);
  L->name = ed_name;
  L->lx = ed.lx; L->ly = ed.ly; L->tx = ed.tx; L->ty = ed.ty; L->speed = 40;
  if (ed.lx == 0) { L->amin = -890; L->amax = 890; L->angle = 0; }
  else if (ed.lx == SCR_W - 1) { L->amin = 910; L->amax = 2690; L->angle = 1800; }
  else if (ed.ly == GV_TOP) { L->amin = 10; L->amax = 1790; L->angle = 900; }
  else if (ed.ly == SCR_H - 1) { L->amin = -1790; L->amax = -10; L->angle = -900; }
  else { L->amin = -1800; L->amax = 1799; L->angle = 0; }
  L->n_pull = ed.n_pull; L->n_push = ed.n_push;
  L->fixed = ed.p; L->n_fixed = ed.np;
  L->walls = ed.w; L->n_walls = ed.nw;
}

enum { T_LAUNCHER, T_TARGET, T_WALL, T_BOUNCE, T_PULL, T_PLANET, T_PUSH, T_STOCK, N_TOOLS };
static const char *const tool_name[N_TOOLS] = { "CANON", "CIBLE", "MUR", "MUR REBONDISSANT", "PUITS",
                                                "GROS PUITS", "REPOUSSOIR", "PUITS A POSER" };
static const char *const tool_help[N_TOOLS] = {
  "B1 PLACER (PRES D'UN BORD)", "B1 PLACER", "B1 DEBUT/FIN  B2 EFFACER", "B1 DEBUT/FIN  B2 EFFACER",
  "B1 POSER   B2 EFFACER", "B1 POSER   B2 EFFACER", "B1 POSER   B2 EFFACER", "B1 ORANGE +1   B2 JAUNE +1" };

static int snap4(int v) { return (v + 2) & ~3; }
static int32_t seg_d2(const gv_wall *w, int x, int y) {      // squared distance, world pixels
  int32_t dx = w->x2 - w->x1, dy = w->y2 - w->y1, l2 = dx * dx + dy * dy;
  int32_t t = l2 ? (((x - w->x1) * dx + (y - w->y1) * dy) * 256) / l2 : 0;
  t = clampi(t, 0, 256);
  int32_t cx = w->x1 + dx * t / 256, cy = w->y1 + dy * t / 256;
  return (x - cx) * (x - cx) + (y - cy) * (y - cy);
}
// removes what is nearest to the cursor: a pod, then a wall
static bool ed_erase(int x, int y) {
  int best = -1; int32_t bd = 12 * 12;
  for (int i = 0; i < ed.np; i++) { int32_t dx = ed.p[i].x - x, dy = ed.p[i].y - y, d = dx * dx + dy * dy; if (d < bd) { bd = d; best = i; } }
  if (best >= 0) { ed.p[best] = ed.p[--ed.np]; return true; }
  bd = 8 * 8;
  for (int i = 0; i < ed.nw; i++) { int32_t d = seg_d2(&ed.w[i], x, y); if (d < bd) { bd = d; best = i; } }
  if (best >= 0) { ed.w[best] = ed.w[--ed.nw]; return true; }
  return false;
}

static void ed_redraw(void) {
  ed_build();
  lv = &ed_lv; level = -1;
  hide_all(); fx_clear(); text_clear();
  memset(pp, 0, sizeof pp);
  draw_level(); bm_clear(CHR_DYN); aim_n = 0; aim_drawn = -9999;
  angle = ed_lv.angle;
}
static void ed_text(int tool, int wall_on) {
  char s[48]; memset(s, ' ', 40); s[40] = 0;
  text(0, 0, s, INK_WHITE); text(0, 28, s, INK_WHITE); text(0, 29, s, INK_WHITE);
  text(0, 0, "CONSTRUCTION", INK_ORANGE);
  text(13, 0, ed_name, INK_WHITE);
  char c[16] = "+0 -0"; c[1] = (char)('0' + ed.n_pull); c[4] = (char)('0' + ed.n_push);
  text(27, 0, "A POSER", INK_GREY); text(35, 0, c, INK_YELLOW);
  text(1, 28, "OUTIL :", INK_GREY); text(9, 28, tool_name[tool], INK_YELLOW);
  text(27, 28, "B4/B5 OUTIL", INK_GREY);
  text(1, 29, wall_on ? "B1 FIN DU MUR   B2 ANNULER" : tool_help[tool], INK_WHITE);
  text(30, 29, "B3 ESSAYER", INK_GREEN);
}
// the wall being drawn, as a dim line on the trace bitmap (the aim line's pixel list is reused)
static void ed_preview(int x0, int y0, int x1, int y1) {
  aim_erase();
  int dx = x1 - x0, dy = y1 - y0, n = iabs(dx) > iabs(dy) ? iabs(dx) : iabs(dy);
  if (!n) n = 1;
  for (int i = 0; i <= n; i++) aim_put(2 * x0 + 2 * dx * i / n, 2 * y0 + 2 * dy * i / n, 4, true);
}

// the building screen of slot n; returns when the player leaves
static int ed_menu(void) {
  static const char *items[4] = { "CONTINUER", "ESSAYER LE NIVEAU", "TOUT EFFACER", "QUITTER LA CONSTRUCTION" };
  int c = 0;
  mc_sprite_hide(S_CURSOR);
  for (;;) {
    for (int i = 0; i < 4; i++) {
      char s[32]; memset(s, ' ', 28); s[28] = 0;
      s[0] = i == c ? '>' : ' ';
      for (int k = 0; items[i][k]; k++) s[2 + k] = items[i][k];
      text(6, 11 + i * 2, s, i == c ? INK_YELLOW : INK_WHITE);
    }
    mc_frame(); sfx_update(); draw_target(t_frame++); draw_pods(t_frame);
    uint32_t p = pressed_all();
    if (p & IN_UP && c > 0) { c--; click(660); }
    if (p & IN_DOWN && c < 3) { c++; click(660); }
    if (p & (IN_START | IN_B(6))) { c = 0; p |= IN_B(1); }
    if (p & IN_B(1) || (mc_mouse_pressed() & MOUSE_LEFT)) {
      for (int i = 0; i < 4; i++) text(6, 11 + i * 2, "                            ", INK_WHITE);
      click(990);
      return c;
    }
  }
}
static int edit_level(int n) {
  ed_load(n);
  int tool = T_PULL, wall_on = 0, wx = 0, wy = 0;
  bool dirty = true;
  pod_mode = true;
  cur_x = SCR_W / 2; cur_y = SCR_H / 2;
  for (;;) {
    if (dirty) { ed_redraw(); ed_text(tool, wall_on); display_play(); dirty = false; pod_mode = true; }
    mc_frame(); sfx_update(); fx_update(); t_frame++;
    move_cursor();
    uint32_t p = pressed_all(), m = mc_mouse_pressed();
    int wheel = mc_mouse_wheel();
    if (m & MOUSE_LEFT) p |= IN_B(1);
    if (m & MOUSE_RIGHT) p |= IN_B(2);
    if (m & MOUSE_MIDDLE) p |= IN_B(3);
    int x = snap4(cur_x), y = snap4(cur_y);
    x = clampi(x, 0, SCR_W - 1); y = clampi(y, GV_TOP, SCR_H - 1);
    int dt = ((p & IN_B(5)) ? 1 : 0) - ((p & IN_B(4)) ? 1 : 0) + (wheel > 0 ? 1 : wheel < 0 ? -1 : 0);
    if (dt && !wall_on) { tool = (tool + dt + N_TOOLS) % N_TOOLS; click(600); ed_text(tool, 0); }
    if (p & IN_B(1)) {
      bool changed = true;
      switch (tool) {
        case T_LAUNCHER: {                           // sticks to the nearest edge within 16 pixels
          int lx = cur_x, ly = cur_y;
          if (lx < 16) lx = 0; else if (lx > SCR_W - 17) lx = SCR_W - 1;
          else if (ly < GV_TOP + 16) ly = GV_TOP; else if (ly > SCR_H - 17) ly = SCR_H - 1;
          ed.lx = (int16_t)lx; ed.ly = (int16_t)ly; break; }
        case T_TARGET: ed.tx = (int16_t)clampi(x, 8, SCR_W - 9); ed.ty = (int16_t)clampi(y, GV_TOP + 8, SCR_H - 9); break;
        case T_WALL: case T_BOUNCE:
          if (!wall_on) { wall_on = 1; wx = x; wy = y; changed = false; ed_text(tool, 1); click(700); }
          else {
            wall_on = 0;
            if ((x != wx || y != wy) && ed.nw < ED_WALLS) {
              gv_wall w = { (int16_t)wx, (int16_t)wy, (int16_t)x, (int16_t)y, (uint8_t)(tool == T_BOUNCE ? W_BOUNCE : W_SOLID) };
              ed.w[ed.nw++] = w;
            } else sfx_buzz = 8;
          }
          break;
        case T_PULL: case T_PLANET: case T_PUSH:
          if (ed.np < ED_PODS) {
            gv_pod q = { (int16_t)x, (int16_t)y, (int8_t)(tool == T_PUSH ? -1 : 1), (uint8_t)(tool == T_PLANET ? 180 : GV_POD_POWER) };
            ed.p[ed.np++] = q;
          } else { sfx_buzz = 8; changed = false; }
          break;
        case T_STOCK: ed.n_pull = (uint8_t)((ed.n_pull + 1) % 5); break;
      }
      if (changed) { ed_store(); dirty = true; click(520); }
    }
    if (p & IN_B(2)) {
      if (wall_on) { wall_on = 0; aim_erase(); ed_text(tool, 0); }
      else if (tool == T_STOCK) { ed.n_push = (uint8_t)((ed.n_push + 1) % 4); ed_store(); dirty = true; click(620); }
      else if (ed_erase(cur_x, cur_y)) { ed_store(); dirty = true; click(300); }
      else sfx_buzz = 8;
    }
    if (wall_on) ed_preview(wx, wy, x, y);
    if (p & (IN_START | IN_B(6) | IN_B(3))) {
      int c = (p & IN_B(3)) ? 1 : ed_menu();
      if (c == 2) { ed_default(); ed_store(); }
      if (c == 1) {
        wall_on = 0;
        ed_build();
        int r = play_lv(&ed_lv, -1);
        if (r == -2) return -2;
      }
      if (c == 3) return -1;
      dirty = true;
      continue;
    }
    draw_pods(t_frame); draw_target(t_frame);
    mc_sprite(S_CURSOR, 2 * x - 16, 2 * y - 16, CELL_CURSOR, 4, 4, PAL_CURSOR, 7);
  }
}

// the six slots: B1 builds, B3 plays
static int slots_screen(void) {
  hide_all(); fx_clear(); text_clear();
  bm_clear(CHR_DYN); draw_title();
  text_c(12, "CONSTRUIRE UN NIVEAU", INK_ORANGE);
  hint("B1 CONSTRUIRE   B3 JOUER   START RETOUR");
  text_c(26, "TOUT EST ENREGISTRE AU FUR ET A MESURE.", INK_GREY);
  display_play();
  static int c = 0;
  for (;;) {
    mc_frame(); sfx_update();
    for (int i = 0; i < ED_SLOTS; i++) {
      ed_slot_t o; bool used = ed_read(i, &o) && (o.nw || o.np);
      char s[40]; memset(s, ' ', 30); s[30] = 0;
      s[0] = i == c ? '>' : ' ';
      memcpy(s + 2, "MON NIVEAU 1", 12); s[13] = (char)('1' + i);
      if (used) {
        char d[20] = "00 MURS, 00 PUITS"; num2(d, o.nw); num2(d + 9, o.np);
        memcpy(s + 16, d, strlen(d));
      } else memcpy(s + 16, "(VIDE)", 6);
      text(5, 14 + i * 1 + i, s, i == c ? INK_YELLOW : used ? INK_WHITE : INK_GREY);
    }
    uint32_t p = pressed_all();
    if (p & IN_UP && c > 0) { c--; click(660); }
    if (p & IN_DOWN && c < ED_SLOTS - 1) { c++; click(660); }
    if (p & (IN_START | IN_B(6))) return -1;
    if (p & IN_B(1) || (mc_mouse_pressed() & MOUSE_LEFT)) { click(990); return c; }
    if (p & IN_B(3)) { click(990); return 100 + c; }
  }
}
static int building(void) {
  for (;;) {
    int c = slots_screen();
    if (c < 0) return -1;
    int r;
    if (c >= 100) { ed_load(c - 100); ed_build(); r = play_lv(&ed_lv, -1); }
    else r = edit_level(c);
    if (r == -2) return -2;
  }
}


// ---------------------------------------------------------------- main

int main(void) {
  mc_init();
  mc_video_mode(HW, HH);                          // 640 x 480 interlaced
  make_font();
  make_palettes();
  mc_background(BG_ENTRY);
  bm_setup(L_STAT, PAL_STAT, 0);
  bm_setup(L_DYN, PAL_DYN, 1);
  pool_reset();
  make_sprites();
  mc_sprites_used(S_USED);
  mc_srand(0xC0FFEE);
  save_load();
  int lvl = save.unlocked - 1;
#ifdef START_LEVEL
  lvl = START_LEVEL - 1; save.unlocked = GV_LEVELS;
#endif
  for (;;) {
  title:
    title_screen();
    for (;;) {
      int c = select_screen(lvl);
      if (c < 0) break;
      if (c == GV_LEVELS) { lvl = GV_LEVELS; if (building() == -2) goto title; continue; }
      lvl = c;
      for (;;) {
        int next = play_level(lvl);
        if (next == -2) goto title;
        if (next < 0) break;
        if (next >= GV_LEVELS) { ending(); lvl = GV_LEVELS - 1; break; }
        lvl = next;
      }
    }
  }
}
