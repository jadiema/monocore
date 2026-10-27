// GRAVITON: a gravity puzzle for MONOCORE-1.
// Place pods that pull or push, then fire: the shot must reach the purple target.
// All graphics are drawn at start-up: two 320 x 240 bitmaps in CHR RAM (scenery, shot traces),
// sprites for pods, rings, target, shot, cursor and sparks.
#include "mc.h"
#include <string.h>
#include "physics.h"
#include "levels.h"

// ---------------------------------------------------------------- layout
#define SCR_W 320
#define SCR_H 240
#define BM_COLS 40
#define BM_ROWS 30
#define BM_SIZE (BM_COLS * BM_ROWS * 32)   // 38400 bytes, 4 bpp

#define CHR_STAT 0x00000                   // scenery bitmap (layer 3)
#define CHR_DYN  0x09600                   // shot traces bitmap (layer 2)
#define CHR_SPR  0x13000                   // sprite cells

#define L_TEXT 0
#define L_DYN  2
#define L_STAT 3

// sprite cells (32 bytes each, from CHR_SPR)
#define CELL_POD    0                      // 16 x 16: 4 cells
#define CELL_PODFIX 4                      // 16 x 16, with a locked rim
#define CELL_TARGET 8                      // 16 x 16
#define CELL_SHOT   12                     // 8 x 8
#define CELL_DOT    13                     // 8 x 8, a 2 x 2 dot
#define CELL_SPARK  14                     // 8 x 8, a small star
#define CELL_CURSOR 16                     // 16 x 16
#define CELL_RING   20                     // 32 x 32: 16 cells
#define CELL_BIGPOD 36                     // 32 x 32 planet: 16 cells
#define N_CELLS     52

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
#define PAL_RPULL   32                     // 32-35: rings of the pulling pods, bright to faint
#define PAL_RPUSH   36                     // 36-39
#define PAL_RTARGET 40                     // 40-43
#define PAL_SPARK   44                     // 44-47: sparks, bright to faint
#define BG_ENTRY    4095

enum { INK_WHITE, INK_YELLOW, INK_CYAN, INK_ORANGE, INK_PURPLE, INK_GREY, INK_GREEN };
#define INK(i) (PAL_INK + (i))

// static bitmap colours
#define C_GRID   1
#define C_STAR1  2
#define C_STAR2  3
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
#define S_AIM    5                         // 5-14
#define N_AIM    10
#define S_POD    16                        // 16-31
#define S_RING   32                        // 32-47
#define S_FX     48
#define N_FX     64
#define S_USED   (S_FX + N_FX)

// ---------------------------------------------------------------- small helpers
static uint32_t rnd(uint32_t n) { return n ? mc_rand() % n : 0; }
static int iabs(int v) { return v < 0 ? -v : v; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int font_white = 1, font_dark = 2;

static void set_rgb(int pal, int i, int r, int g, int b) { mc_color(pal * 16 + i, MC_RGB(r, g, b)); }
static void text(int col, int row, const char *s, int ink) { mc_text(L_TEXT, col, row, s, INK(ink)); }
static void text_c(int row, const char *s, int ink) { text((40 - (int)strlen(s)) / 2, row, s, ink); }
static void text_clear(void) { mc_text_clear(L_TEXT); }

// ---------------------------------------------------------------- bitmaps
static void bm_setup(int layer, uint32_t base, int pal, int prio) {
  mc_layer(layer, MC_CHR(base), MC_TILE8 | MC_4BPP, 64, 32, prio);
  for (int r = 0; r < 32; r++)
    for (int c = 0; c < 64; c++)
      mc_tile(layer, c, r, (c < BM_COLS && r < BM_ROWS) ? TILE(r * BM_COLS + c, pal) : TILE(0, pal) | 0);
  mc_layer_scroll(layer, 0, 0);
}
static void bm_clear(uint32_t base) { mc_fill(MC_CHRRAM + base, 0, BM_SIZE); }

static inline void bm_plot(uint32_t base, int x, int y, int c) {
  if ((unsigned)x >= SCR_W || (unsigned)y >= SCR_H) return;
  volatile uint8_t *p = MC_CHRRAM + base + ((y >> 3) * BM_COLS + (x >> 3)) * 32 + (y & 7) * 4 + ((x & 7) >> 1);
  uint8_t v = *p;
  *p = (x & 1) ? (uint8_t)((v & 0x0F) | (c << 4)) : (uint8_t)((v & 0xF0) | c);
}
static inline int bm_get(uint32_t base, int x, int y) {
  if ((unsigned)x >= SCR_W || (unsigned)y >= SCR_H) return 0;
  uint8_t v = MC_CHRRAM[base + ((y >> 3) * BM_COLS + (x >> 3)) * 32 + (y & 7) * 4 + ((x & 7) >> 1)];
  return (x & 1) ? v >> 4 : v & 15;
}
static void bm_disc(uint32_t base, int x, int y, int r, int c) {
  for (int dy = -r; dy <= r; dy++)
    for (int dx = -r; dx <= r; dx++)
      if (dx * dx + dy * dy <= r * r + r) bm_plot(base, x + dx, y + dy, c);
}
// a line stamped with a disc of radius r (r = 0: one pixel)
static void bm_line(uint32_t base, int x0, int y0, int x1, int y1, int r, int c) {
  int dx = x1 - x0, dy = y1 - y0, n = iabs(dx) > iabs(dy) ? iabs(dx) : iabs(dy);
  if (n == 0) n = 1;
  for (int i = 0; i <= n; i++) {
    int x = x0 + (dx * i + (dx >= 0 ? n / 2 : -n / 2)) / n, y = y0 + (dy * i + (dy >= 0 ? n / 2 : -n / 2)) / n;
    if (r) bm_disc(base, x, y, r, c); else bm_plot(base, x, y, c);
  }
}
static void bm_circle(uint32_t base, int x, int y, int r, int c) {
  for (int a = 0; a < 1024; a += 4) bm_plot(base, x + FIX_INT(mc_cos(a) * r + 32768), y + FIX_INT(mc_sin(a) * r + 32768), c);
}

// ---------------------------------------------------------------- sprite graphics
static uint8_t gfx[32 * 32];               // one picture being drawn, 1 byte a pixel

static void gfx_begin(void) { memset(gfx, 0, sizeof gfx); }
static void gfx_put(int x, int y, int c, int w) { if ((unsigned)x < (unsigned)w && (unsigned)y < (unsigned)w) gfx[y * 32 + x] = (uint8_t)c; }
static void gfx_upload(int cell, int size) {   // size 8, 16 or 32: picture -> cells, row-major
  static uint8_t buf[32 * 16];
  int n = size / 8, k = 0;
  for (int cy = 0; cy < n; cy++)
    for (int cx = 0; cx < n; cx++)
      for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x += 2) {
          int a = gfx[(cy * 8 + y) * 32 + cx * 8 + x], b = gfx[(cy * 8 + y) * 32 + cx * 8 + x + 1];
          buf[k++] = (uint8_t)(a | (b << 4));
        }
  mc_chr_upload(CHR_SPR + cell * 32, buf, (uint32_t)k);
}
// a shaded ball: colours 1 (rim) .. 6 (highlight), centre (c, c), radius r
static void gfx_ball(int c, int r, int size) {
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++) {
      int dx = 2 * x + 1 - 2 * c, dy = 2 * y + 1 - 2 * c, d2 = dx * dx + dy * dy, R = 2 * r;
      if (d2 > R * R) continue;
      int hx = dx + R / 2, hy = dy + R / 2, h2 = hx * hx + hy * hy;     // light from the top left
      int k = 6 - (h2 * 6) / (R * R * 2 + 1);
      if (d2 > (R - 2) * (R - 2)) k = 1;
      gfx_put(x, y, clampi(k, 1, 6), size);
    }
}
static void gfx_ring(int c2, int r2, int thick2, int col, int size) {   // in half pixels
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++) {
      int dx = 2 * x + 1 - c2, dy = 2 * y + 1 - c2;
      int d = (int)mc_isqrt((uint32_t)(dx * dx + dy * dy));
      if (iabs(d - r2) <= thick2) gfx_put(x, y, col, size);
    }
}

static void make_sprites(void) {
  gfx_begin(); gfx_ball(8, 6, 16); gfx_upload(CELL_POD, 16);
  gfx_begin(); gfx_ball(8, 5, 16); gfx_ring(16, 14, 1, 7, 16); gfx_upload(CELL_PODFIX, 16);
  gfx_begin(); gfx_ball(8, 7, 16);
  gfx_ring(16, 6, 1, 7, 16); gfx_upload(CELL_TARGET, 16);
  gfx_begin(); gfx_ball(4, 2, 8);
  for (int i = 0; i < 8; i++) { if (!gfx[3 * 32 + i]) gfx[3 * 32 + i] = 8; if (!gfx[i * 32 + 3]) gfx[i * 32 + 3] = 8; }
  gfx_upload(CELL_SHOT, 8);
  gfx_begin(); gfx_put(3, 3, 1, 8); gfx_put(4, 3, 1, 8); gfx_put(3, 4, 1, 8); gfx_put(4, 4, 1, 8); gfx_upload(CELL_DOT, 8);
  gfx_begin();
  gfx_put(3, 3, 1, 8); gfx_put(3, 2, 2, 8); gfx_put(3, 4, 2, 8); gfx_put(2, 3, 2, 8); gfx_put(4, 3, 2, 8);
  gfx_upload(CELL_SPARK, 8);
  gfx_begin();
  for (int i = 0; i < 16; i++) {
    if (i < 6 || i > 9) { gfx_put(i, 7, 1, 16); gfx_put(i, 8, 1, 16); gfx_put(7, i, 1, 16); gfx_put(8, i, 1, 16); }
  }
  for (int i = 0; i < 4; i++) { gfx_put(i, 0, 2, 16); gfx_put(0, i, 2, 16); gfx_put(15 - i, 0, 2, 16); gfx_put(15, i, 2, 16);
                                gfx_put(i, 15, 2, 16); gfx_put(0, 15 - i, 2, 16); gfx_put(15 - i, 15, 2, 16); gfx_put(15, 15 - i, 2, 16); }
  gfx_upload(CELL_CURSOR, 16);
  gfx_begin(); gfx_ring(32, 28, 2, 1, 32); gfx_upload(CELL_RING, 32);
  gfx_begin(); gfx_ball(16, 13, 32); gfx_ring(32, 30, 1, 7, 32); gfx_upload(CELL_BIGPOD, 32);
  mc_sprite_sheet(MC_CHR(CHR_SPR));
}

// ---------------------------------------------------------------- palettes
static void ramp(int pal, int r, int g, int b) {   // 1 = rim (dark) ... 6 = highlight, 7 = rim line, 8 = glow
  for (int k = 1; k <= 6; k++) {
    int m = 90 + k * 28;                            // 118..258
    int rr = r * m / 256, gg = g * m / 256, bb = b * m / 256;
    if (k == 6) { rr = (rr + 255) / 2; gg = (gg + 255) / 2; bb = (bb + 255) / 2; }
    set_rgb(pal, k, clampi(rr, 0, 255), clampi(gg, 0, 255), clampi(bb, 0, 255));
  }
  set_rgb(pal, 7, 170, 175, 190);
  set_rgb(pal, 8, r / 2, g / 2, b / 2);
}
static void ring_pals(int pal, int r, int g, int b) {
  for (int k = 0; k < 4; k++) { int m = 256 - k * 60; set_rgb(pal + k, 1, r * m / 256, g * m / 256, b * m / 256); }
}
static void make_palettes(void) {
  font_white = 1;
  for (int i = 1; i < 16; i++) if (PALETTE[i] == MC_RGB(255, 255, 255)) font_white = i;
  font_dark = font_white == 1 ? 2 : 1;
  for (int i = 1; i < 16; i++) if (i != font_white && PALETTE[i]) font_dark = i;
  static const uint8_t ink[7][3] = { {240, 240, 255}, {255, 225, 90}, {90, 220, 255}, {255, 150, 60},
                                     {200, 120, 255}, {120, 125, 150}, {120, 240, 140} };
  for (int p = 0; p < 7; p++) {
    for (int i = 0; i < 16; i++) PALETTE[(PAL_INK + p) * 16 + i] = PALETTE[i];
    set_rgb(PAL_INK + p, font_white, ink[p][0], ink[p][1], ink[p][2]);
    set_rgb(PAL_INK + p, font_dark, 6, 8, 24);
  }
  // scenery
  set_rgb(PAL_STAT, C_GRID, 16, 22, 52);
  set_rgb(PAL_STAT, C_STAR1, 60, 70, 110);
  set_rgb(PAL_STAT, C_STAR2, 170, 180, 230);
  set_rgb(PAL_STAT, C_HALO, 50, 55, 85);
  set_rgb(PAL_STAT, C_WALL, 175, 180, 205);
  set_rgb(PAL_STAT, C_WALLHI, 245, 245, 255);
  set_rgb(PAL_STAT, C_BHALO, 10, 70, 95);
  set_rgb(PAL_STAT, C_BOUNCE, 40, 200, 240);
  set_rgb(PAL_STAT, C_BHI, 190, 250, 255);
  set_rgb(PAL_STAT, C_NOPOD, 70, 18, 34);
  set_rgb(PAL_STAT, C_BASE, 255, 220, 80);
  set_rgb(PAL_STAT, C_TITLE1, 60, 20, 110);
  set_rgb(PAL_STAT, C_TITLE2, 150, 80, 255);
  set_rgb(PAL_STAT, C_TITLE3, 240, 220, 255);
  // traces: set by trace_colours()
  ramp(PAL_PULL, 70, 150, 255);
  ramp(PAL_PUSH, 255, 110, 40);
  ramp(PAL_PULLFIX, 50, 100, 190);
  ramp(PAL_PUSHFIX, 190, 80, 30);
  ramp(PAL_BAD, 130, 40, 50);
  ramp(PAL_TARGET, 190, 70, 255);
  set_rgb(PAL_TARGET, 7, 255, 220, 255);
  ramp(PAL_SHOT, 255, 240, 160);
  set_rgb(PAL_SHOT, 8, 255, 170, 40);
  set_rgb(PAL_CURSOR, 1, 255, 255, 255); set_rgb(PAL_CURSOR, 2, 120, 230, 255);
  set_rgb(PAL_AIM, 1, 255, 220, 80);
  ring_pals(PAL_RPULL, 80, 160, 255);
  ring_pals(PAL_RPUSH, 255, 120, 50);
  ring_pals(PAL_RTARGET, 200, 90, 255);
  for (int k = 0; k < 4; k++) {
    int m = 256 - k * 60;
    set_rgb(PAL_SPARK + k, 1, 255 * m / 256, 250 * m / 256, 200 * m / 256);
    set_rgb(PAL_SPARK + k, 2, 255 * m / 256, 150 * m / 256, 50 * m / 256);
  }
  mc_color(BG_ENTRY, MC_RGB(4, 5, 16));
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
    mc_sprite(S_FX + i, (p->x >> 8) - 4, (p->y >> 8) - 4, CELL_SPARK, 1, 1, PAL_SPARK + clampi(k, 0, 3), 5);
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
static int trace_slot, trace_count;
static uint32_t t_frame;

static int count_on(int sign) { int n = 0; for (int i = 0; i < MAX_PLAYER; i++) if (pp[i].on && pp[i].sign == sign) n++; return n; }
static int left_of(int sign) { return (sign > 0 ? lv->n_pull : lv->n_push) - count_on(sign); }
static bool aim_free(void) { return lv->amax > lv->amin; }

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
static void draw_stars(int n) {
  for (int i = 0; i < n; i++) {
    int x = (int)rnd(SCR_W), y = GV_TOP + (int)rnd(SCR_H - GV_TOP);
    if (bm_get(CHR_STAT, x, y)) continue;
    bm_plot(CHR_STAT, x, y, rnd(5) ? C_STAR1 : C_STAR2);
  }
}
static void draw_grid(void) {
  for (int x = 20; x < SCR_W; x += 40) for (int y = GV_TOP + 2; y < SCR_H; y += 4) bm_plot(CHR_STAT, x, y, C_GRID);
  for (int y = GV_TOP + 28; y < SCR_H; y += 40) for (int x = 0; x < SCR_W; x += 4) bm_plot(CHR_STAT, x, y, C_GRID);
}
static void draw_level(void) {
  bm_clear(CHR_STAT);
  mc_srand(0x1234u + (uint32_t)level * 77u);
  draw_grid();
  for (int i = 0; i < lv->n_nopod; i++) {          // forbidden zones: hatching
    const gv_rect *r = &lv->nopod[i];
    for (int y = r->y0; y <= r->y1; y++)
      for (int x = r->x0; x <= r->x1; x++)
        if (((x + y) % 7) == 0 || x == r->x0 || x == r->x1 || y == r->y0 || y == r->y1) bm_plot(CHR_STAT, x, y, C_NOPOD);
  }
  draw_stars(140);
  for (int pass = 0; pass < 3; pass++)
    for (int i = 0; i < lv->n_walls; i++) {
      const gv_wall *w = &lv->walls[i];
      bool b = w->kind == W_BOUNCE;
      int r = pass == 0 ? 3 : pass == 1 ? 2 : 0;
      int c = pass == 0 ? (b ? C_BHALO : C_HALO) : pass == 1 ? (b ? C_BOUNCE : C_WALL) : (b ? C_BHI : C_WALLHI);
      bm_line(CHR_STAT, w->x1, w->y1, w->x2, w->y2, r, c);
    }
  bm_circle(CHR_STAT, lv->lx, lv->ly, 7, C_BASE);   // launcher
  bm_circle(CHR_STAT, lv->lx, lv->ly, 8, C_HALO);
  bm_disc(CHR_STAT, lv->lx, lv->ly, 2, C_BASE);
  for (int x = 0; x < SCR_W; x++) bm_plot(CHR_STAT, x, GV_TOP - 2, C_HALO);
}

// traces of the last three shots: slot colours, newest brightest
static void trace_colours(void) {
  static const uint8_t c[3][3] = { {255, 235, 140}, {170, 110, 40}, {80, 45, 30} };
  for (int s = 0; s < 3; s++) {
    int age = (trace_slot - s + 3) % 3;
    if (age >= trace_count) age = 2;
    set_rgb(PAL_DYN, 1 + s, c[age][0], c[age][1], c[age][2]);
  }
}
static void trace_new(void) {                      // next slot, wiping the shot it held
  trace_slot = (trace_slot + 1) % 3;
  if (trace_count < 3) trace_count++;
  int s = 1 + trace_slot;
  volatile uint8_t *p = MC_CHRRAM + CHR_DYN;
  for (int i = 0; i < BM_SIZE; i++) {
    uint8_t v = p[i];
    if (!v) continue;
    if ((v & 15) == s) v &= 0xF0;
    if ((v >> 4) == s) v &= 0x0F;
    p[i] = v;
  }
  trace_colours();
}

// ---------------------------------------------------------------- sprites of the level
static void pod_sprite(int n, int x, int y, int sign, bool fixed, int power, bool bad, uint32_t t) {
  int pal = bad ? PAL_BAD : fixed ? (sign > 0 ? PAL_PULLFIX : PAL_PUSHFIX) : (sign > 0 ? PAL_PULL : PAL_PUSH);
  bool big = power > GV_POD_POWER + 40;
  if (big) mc_sprite(S_POD + n, x - 16, y - 16, CELL_BIGPOD, 4, 4, pal, 4);
  else mc_sprite(S_POD + n, x - 8, y - 8, fixed ? CELL_PODFIX : CELL_POD, 2, 2, pal, 4);
  // the field: a ring that closes in (pull) or opens out (push)
  int ph = (int)((t * 2 + (uint32_t)n * 23) % 64);
  if (sign < 0) ph = 63 - ph;
  int z = big ? 0x90 + ph * 5 : 0x50 + ph * 6;     // 0x50 .. 0x1D0 (0x90 .. 0x1D3 for planets)
  int size = 32 * z / 256;
  int fade = sign > 0 ? 3 - ph / 16 : ph / 16;
  if (bad) fade = 3;
  mc_sprite(S_RING + n, x - size / 2, y - size / 2, CELL_RING, 4, 4, (sign > 0 ? PAL_RPULL : PAL_RPUSH) + clampi(fade, 0, 3), 3);
  mc_sprite_zoom(S_RING + n, z, z);
}

static void draw_target(uint32_t t) {
  mc_sprite(S_TARGET, lv->tx - 8, lv->ty - 8, CELL_TARGET, 2, 2, PAL_TARGET, 4);
  int ph = (int)(t % 48), z = 0x60 + ph * 5, size = 32 * z / 256;
  mc_sprite(S_TRING, lv->tx - size / 2, lv->ty - size / 2, CELL_RING, 4, 4, PAL_RTARGET + clampi(ph / 12, 0, 3), 3);
  mc_sprite_zoom(S_TRING, z, z);
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
static void draw_aim(bool show) {
  for (int i = 0; i < N_AIM; i++) {
    if (!show) { mc_sprite_hide(S_AIM + i); continue; }
    int d = 12 + i * 6;
    int x = lv->lx + FIX_INT(gv_cos(angle) * d + 32768), y = lv->ly + FIX_INT(gv_sin(angle) * d + 32768);
    mc_sprite(S_AIM + i, x - 4, y - 4, CELL_DOT, 1, 1, PAL_AIM, 4);
  }
}

// ---------------------------------------------------------------- HUD
static void num2(char *s, int v) { s[0] = (char)('0' + v / 10 % 10); s[1] = (char)('0' + v % 10); }
static void hud(void) {
  char s[48];
  memset(s, ' ', 40); s[40] = 0;
  text(0, 0, s, INK_WHITE);
  s[0] = 'N'; s[1] = 'I'; s[2] = 'V'; s[3] = ' '; num2(s + 4, level + 1); s[6] = 0;
  text(0, 0, s, INK_GREY);
  text(7, 0, lv->name, INK_WHITE);
  int col = 24;
  if (lv->n_pull) { char p[8] = "+ 0/0"; p[2] = (char)('0' + left_of(1)); p[4] = (char)('0' + lv->n_pull);
                    text(col, 0, p, sel > 0 ? INK_CYAN : INK_GREY); col += 6; }
  if (lv->n_push) { char p[8] = "- 0/0"; p[2] = (char)('0' + left_of(-1)); p[4] = (char)('0' + lv->n_push);
                    text(col, 0, p, sel < 0 ? INK_ORANGE : INK_GREY); col += 6; }
  char t[8] = "T 00"; num2(t + 2, shots); text(36, 0, t, INK_YELLOW);
}
static void hint(const char *s) {
  char b[41]; memset(b, ' ', 40); b[40] = 0; text(0, 29, b, INK_WHITE);
  text_c(29, s, INK_GREY);
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
  bm_clear(CHR_STAT);
  mc_srand(99);
  draw_grid();
  draw_stars(260);
  int sc = 7, w = 8 * (5 * sc - sc / 2) - sc, x0 = (SCR_W - w) / 2, y0 = 34;
  vector_word(word, 8, x0, y0, sc, 3, C_TITLE1);
  vector_word(word, 8, x0, y0, sc, 1, C_TITLE2);
  vector_word(word, 8, x0, y0, sc, 0, C_TITLE3);
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
  set_rgb(PAL_DYN, 1, 255, 235, 140); set_rgb(PAL_DYN, 2, 150, 90, 40); set_rgb(PAL_DYN, 3, 70, 40, 30);
  text_c(15, "PLACEZ LES PUITS, COURBEZ LE TIR,", INK_GREY);
  text_c(16, "ATTEIGNEZ LA CIBLE.", INK_GREY);
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
    for (int s = 0; s < GV_SUB; s++) {
      gv_step(&dw, &b);
      bm_plot(CHR_DYN, b.x >> 16, b.y >> 16, 1 + slot);
    }
    if (t % 160 == 159) {                          // rotate the trace colours
      slot = (slot + 1) % 3;
      volatile uint8_t *p = MC_CHRRAM + CHR_DYN;
      int s = 1 + slot;
      for (int i = 0; i < BM_SIZE; i++) { uint8_t v = p[i]; if (!v) continue;
        if ((v & 15) == s) v &= 0xF0; if ((v >> 4) == s) v &= 0x0F; p[i] = v; }
      for (int k = 0; k < 3; k++) {
        static const uint8_t c[3][3] = { {255, 235, 140}, {150, 90, 40}, {70, 40, 30} };
        int age = (slot - k + 3) % 3;
        set_rgb(PAL_DYN, 1 + k, c[age][0], c[age][1], c[age][2]);
      }
    }
    pod_sprite(0, 160, 176, 1, true, 150, false, t);
    mc_sprite(S_SHOT, (b.x >> 16) - 4, (b.y >> 16) - 4, CELL_SHOT, 1, 1, PAL_SHOT, 5);
    if (t % 3 == 0) spark(b.x >> 16, b.y >> 16, 40, 12);
    if (t > 20 && any_button()) { click(880); break; }
  }
  music_off();
}

// ---------------------------------------------------------------- level select
static int select_screen(int start) {
  hide_all(); fx_clear(); text_clear();
  bm_clear(CHR_DYN);
  draw_title();
  int c = clampi(start, 0, save.unlocked - 1);
  text_c(13, "CHOIX DU NIVEAU", INK_PURPLE);
  hint("B1: JOUER   START: TITRE");
  display_play();
  for (;;) {
    mc_frame(); sfx_update();
    for (int i = 0; i < GV_LEVELS; i++) {
      int col = (i < 8) ? 0 : 20, row = 15 + (i % 8);
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
    uint32_t p = pressed_all();
    int old = c;
    if (p & IN_UP) c = c % 8 == 0 ? c : c - 1;
    if (p & IN_DOWN) c = (c % 8 == 7 || c + 1 >= GV_LEVELS) ? c : c + 1;
    if (p & IN_LEFT) c = c >= 8 ? c - 8 : c;
    if (p & IN_RIGHT) c = c + 8 < GV_LEVELS ? c + 8 : c;
    if (c >= save.unlocked) c = old;
    if (c != old) click(660);
    if (p & (IN_B(1) | IN_B(3)) || (mc_mouse_pressed() & MOUSE_LEFT)) { click(990); return c; }
    if (p & IN_START) return -1;
  }
}

// ---------------------------------------------------------------- playing a level
static void level_start(int n) {
  level = n; lv = &gv_levels[n];
  memset(pp, 0, sizeof pp);
  angle = lv->angle; if (angle < lv->amin || angle > lv->amax) angle = (lv->amin + lv->amax) / 2;
  shots = 0; held = -1;
  sfx_silence();
  sel = lv->n_pull ? 1 : -1;
  cur_x = SCR_W / 2; cur_y = SCR_H / 2; cur_hold = 0;
  hide_all(); fx_clear(); text_clear();
  draw_level();
  bm_clear(CHR_DYN);
  trace_slot = 0; trace_count = 0; trace_colours();
  hud();
  display_play();
  if (mc_mouse_present()) mc_mouse_set(cur_x, cur_y);
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
static bool on_launcher(int x, int y) { int dx = x - lv->lx, dy = y - lv->ly; return dx * dx + dy * dy <= 12 * 12; }
static bool can_put(int x, int y, int skip) { build_world(skip); return gv_can_place(&world, x, y, -1); }

static void aim_to(int x, int y) {
  int a = mc_atan2(y - lv->ly, x - lv->lx);
  while (a < lv->amin) a += 1024;
  while (a > lv->amin + 1023) a -= 1024;
  if (a > lv->amax) a = (a - lv->amax < lv->amin + 1024 - a) ? lv->amax : lv->amin;
  angle = a;
}

static void move_cursor(void) {
  uint32_t h = held_all();
  int dx = 0, dy = 0;
  if (h & IN_LEFT) dx--;
  if (h & IN_RIGHT) dx++;
  if (h & IN_UP) dy--;
  if (h & IN_DOWN) dy++;
  if (dx || dy) cur_hold++; else cur_hold = 0;
  int sp = cur_hold < 6 ? 1 : cur_hold < 20 ? 2 : 4;
  cur_x += dx * sp; cur_y += dy * sp;
  int sx = mc_stick_x(1, 0), sy = mc_stick_y(1, 0);
  if (iabs(sx) > 24) cur_x += sx / 24;
  if (iabs(sy) > 24) cur_y += sy / 24;
  static int mx = -1, my = -1;
  if (mc_mouse_present()) {
    int nx = mc_mouse_x(), ny = mc_mouse_y();
    if (nx != mx || ny != my) { cur_x = nx; cur_y = ny; }
    mx = nx; my = ny;
  }
  cur_x = clampi(cur_x, 0, SCR_W - 1); cur_y = clampi(cur_y, GV_TOP, SCR_H - 1);
  if (mc_mouse_present()) { mc_mouse_set(cur_x, cur_y); mx = cur_x; my = cur_y; }
}

// returns 1 when the shot is fired, 2 to go to the menu
static int edit_frame(uint32_t t) {
  move_cursor();
  uint32_t p = pressed_all();
  uint32_t m = mc_mouse_pressed();
  int wheel = mc_mouse_wheel();
  if (m & MOUSE_LEFT) p |= IN_B(1);
  if (m & MOUSE_RIGHT) p |= IN_B(2);
  if (m & MOUSE_MIDDLE) p |= IN_B(3);

  if (p & IN_B(1)) {
    if (held == HELD_AIM) { held = -1; click(700); }
    else if (held >= 0) {
      if (can_put(cur_x, cur_y, held)) { pp[held].x = cur_x; pp[held].y = cur_y; held = -1; click(520); }
      else sfx_buzz = 8;
    } else {
      int k = near_player_pod(cur_x, cur_y);
      if (k >= 0) { held = k; click(780); }
      else if (aim_free() && on_launcher(cur_x, cur_y)) { held = HELD_AIM; click(900); }
      else if (left_of(sel) > 0 && can_put(cur_x, cur_y, -1)) {
        for (int i = 0; i < MAX_PLAYER; i++) if (!pp[i].on) {
          pp[i].on = true; pp[i].x = cur_x; pp[i].y = cur_y; pp[i].sign = sel; break; }
        click(sel > 0 ? 440 : 620);
        if (left_of(sel) == 0 && left_of(-sel) > 0) sel = -sel;
        hud();
      } else sfx_buzz = 8;
    }
  }
  if (p & IN_B(2)) {
    int k = held >= 0 && held != HELD_AIM ? held : near_player_pod(cur_x, cur_y);
    if (k >= 0) { pp[k].on = false; if (held == k) held = -1; click(300); sel = pp[k].sign; hud(); }
    else if (held == HELD_AIM) held = -1;
    else if (lv->n_pull && lv->n_push) { sel = -sel; click(sel > 0 ? 440 : 620); hud(); }
  }
  if (aim_free()) {
    int d = 0;
    if (pad_all() & IN_B(4)) d -= 1;
    if (pad_all() & IN_B(5)) d += 1;
    d += wheel * 2;
    if (d) angle = clampi(angle + d, lv->amin, lv->amax);
    if (held == HELD_AIM) aim_to(cur_x, cur_y);
  }
  if (p & IN_START) return 2;

  // sprites
  draw_pods(t);
  draw_target(t);
  draw_aim(true);
  mc_sprite_hide(S_SHOT);
  if (held >= 0 && held != HELD_AIM) {
    bool ok = can_put(cur_x, cur_y, held);
    pod_sprite(15, cur_x, cur_y, pp[held].sign, false, GV_POD_POWER, !ok, t);
    mc_sprite_hide(S_CURSOR);
  } else {
    mc_sprite(S_CURSOR, cur_x - 8, cur_y - 8, CELL_CURSOR, 2, 2, PAL_CURSOR, 7);
    // what the button would do here
    if (held == HELD_AIM) hint("B1: LACHER LE CANON");
    else if (near_player_pod(cur_x, cur_y) >= 0) hint("B1: PRENDRE   B2: RETIRER");
    else if (aim_free() && on_launcher(cur_x, cur_y)) hint("B1: ORIENTER LE CANON");
    else if (left_of(sel) > 0) hint(sel > 0 ? "B1: PUITS  B2: TYPE  B3: TIR" : "B1: REPOUSSOIR  B2: TYPE  B3: TIR");
    else hint("B3: TIR   START: MENU");
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
  shots++; hud();
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
        bm_plot(CHR_DYN, b.x >> 16, b.y >> 16, col);
        if (e == EV_BOUNCE) { sfx_bounce = 8; burst(b.x >> 16, b.y >> 16, 4, 80); ev = EV_BOUNCE; }
        else if (e != EV_NONE) { ev = e; break; }
      }
      if (ev == EV_NONE || ev == EV_BOUNCE) { if (++b.frame >= GV_LIFE) ev = EV_LOST; }
    }
    int x = b.x >> 16, y = b.y >> 16;
    mc_sprite(S_SHOT, x - 4, y - 4, CELL_SHOT, 1, 1, PAL_SHOT, 5);
    if (t % 2 == 0) spark(x, y, 24, 10);
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
  static const char *items[3] = { "CONTINUER", "RECOMMENCER LE NIVEAU", "CHOIX DU NIVEAU" };
  int c = 0;
  mc_sprite_hide(S_CURSOR);
  for (;;) {
    for (int i = 0; i < 3; i++) {
      char s[32]; memset(s, ' ', 25); s[25] = 0;
      s[0] = i == c ? '>' : ' ';
      for (int k = 0; items[i][k]; k++) s[2 + k] = items[i][k];
      text(8, 12 + i * 2, s, i == c ? INK_YELLOW : INK_WHITE);
    }
    mc_frame(); sfx_update(); draw_target(t_frame++); draw_pods(t_frame);
    uint32_t p = pressed_all();
    if (p & IN_UP && c > 0) { c--; click(660); }
    if (p & IN_DOWN && c < 2) { c++; click(660); }
    if (p & IN_START) c = 0;
    if (p & (IN_B(1) | IN_START) || (mc_mouse_pressed() & MOUSE_LEFT)) {
      for (int i = 0; i < 3; i++) text(8, 12 + i * 2, "                         ", INK_WHITE);
      click(990);
      return c;
    }
  }
}

// plays level n; returns the next level, or -1 for the level select
static int play_level(int n) {
  level_start(n);
  for (;;) {
    mc_frame(); sfx_update(); fx_update();
    int r = edit_frame(t_frame++);
    if (r == 2) {
      int m = pause_menu();
      hud();
      if (m == 1) { level_start(n); continue; }
      if (m == 2) return -1;
      continue;
    }
    if (r != 1) continue;
    int ev = fly();
    if (ev == EV_TARGET) {
      char s[40] = "CIBLE ATTEINTE EN 00 TIRS";
      num2(s + 18, shots);
      if (shots == 1) { s[21] = 'T'; s[22] = 'I'; s[23] = 'R'; s[24] = 0; }
      if (s[18] == '0') { memmove(s + 18, s + 19, strlen(s + 19) + 1); }
      bool record = save.best[n] == 0 || shots < save.best[n];
      if (record) save.best[n] = (uint8_t)clampi(shots, 1, 99);
      if (save.unlocked < GV_LEVELS && save.unlocked <= n + 1) save.unlocked = (uint8_t)(n + 2);
      save_store();
      text_c(12, s, INK_PURPLE);
      if (record && shots > 0) text_c(14, "NOUVEAU RECORD", INK_GREEN);
      hint(n + 1 < GV_LEVELS ? "B1: NIVEAU SUIVANT   B2: REJOUER" : "B1: CONTINUER");
      wait_frames(30);
      for (;;) {
        wait_frames(1);
        uint32_t p = pressed_all();
        if (p & IN_B(1) || (mc_mouse_pressed() & MOUSE_LEFT)) return n + 1;
        if (p & IN_B(2)) return n;
        if (p & IN_START) return -1;
      }
    } else {
      text_c(13, fail_text(ev), INK_ORANGE);
      wait_frames(50);
      text_c(13, "                              ", INK_WHITE);
      hud();
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

// ---------------------------------------------------------------- main
int main(void) {
  mc_init();
  mc_video_mode(SCR_W, SCR_H);
  mc_text_layer(L_TEXT, 3);
  make_palettes();
  mc_background(BG_ENTRY);
  bm_setup(L_STAT, CHR_STAT, PAL_STAT, 0);
  bm_setup(L_DYN, CHR_DYN, PAL_DYN, 1);
  make_sprites();
  mc_sprites_used(S_USED);
  mc_srand(0xC0FFEE);
  save_load();
  int lvl = save.unlocked - 1;
#ifdef START_LEVEL
  lvl = START_LEVEL - 1; save.unlocked = GV_LEVELS;
#endif
  for (;;) {
    title_screen();
    for (;;) {
      int c = select_screen(lvl);
      if (c < 0) break;
      lvl = c;
      for (;;) {
        int next = play_level(lvl);
        if (next < 0) break;
        if (next >= GV_LEVELS) { ending(); lvl = GV_LEVELS - 1; break; }
        lvl = next;
      }
    }
  }
}
