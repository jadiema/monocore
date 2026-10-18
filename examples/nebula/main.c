// NEBULA - a horizontal shoot'em up for MONOCORE-1, written with the low-level "mc" SDK.
//
// Shows what mc adds to the console's C programs:
//   - every graphic is generated at start-up in CHR RAM (no image file): sprites from ASCII art,
//     three background layers from procedural height profiles (mc_chr_upload);
//   - parallax on three layers, a perspective floor with per-line scrolling (mc_linescroll);
//   - a sky gradient made of 54 raster effects (mc_raster_add);
//   - up to 240 sprites a frame through the shadow sprite table (mc_sprite, mc_frame);
//   - zoomed sprites (title letters, explosion particles), aimed enemy shots (mc_atan2, mc_cos);
//   - heap memory (calloc), snprintf, PSG sound effects and music (mc_psg, mc_tune).
#include "mc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const uint8_t font_chr[], font_pal[];   // SDK font, always added by build.py

// ---------------------------------------------------------------- memory map of CHR RAM
#define CHR_SPR    0x0000                 // sprite cells
#define CHR_STARS  0x1000                 // layer 2: stars
#define CHR_FAR    0x2000                 // layer 1: far mountains (384 tiles at most)
#define CHR_NEAR   0x5000                 // layer 3: near hills and floor (384 tiles at most)
#define CHR_FONT   0x8000                 // copy of the SDK font, for the title letters
#define CHR(off)   MC_CHR(off)
#define CELL_FONT  (CHR_FONT / 32)

// sprite cells (8 x 8 each); a 16 x 16 sprite uses 4 cells in the order tile + cy * 2 + cx
#define CELL_SHIP   0
#define CELL_SAUCER 4
#define CELL_ORB    8
#define CELL_LASER  12
#define CELL_SHOT   13
#define CELL_SPARK  14

// palettes
#define PAL_HUD    1                      // 1-3: recoloured SDK font
#define PAL_LETTER 4                      // 4-9: title letters
#define PAL_SPR    16
#define PAL_FX     17                     // 17-20: particles, from hot to cold
#define PAL_STARS  24
#define PAL_FAR    25
#define PAL_NEAR   26
#define SKY        4095                   // background colour entry, changed line by line

// sprite slots
#define S_PLAYER   0
#define S_LASER    1
#define N_LASER    16
#define S_ENEMY    17
#define N_ENEMY    24
#define S_SHOT     41
#define N_SHOT     32
#define S_FX       73
#define N_FX       160
#define S_USED     (S_FX + N_FX)

#define FP 8                              // positions in 8.8 fixed point
#define PX(v) ((v) >> FP)

// ---------------------------------------------------------------- sprite art
// '.' = transparent, 'A' to 'O' = colours 1 to 15 of PAL_SPR (or of PAL_FX for the spark)
static const char *const art_ship[16] = {
  "................", "................", "..AA............", "..ABA...........",
  "...ABBA.........", "...ACCBBAA......", "DDACCCCCBBBAA...", "EEACCCCFFFCCBBA.",
  "EEACCCCFFFCCBBA.", "DDACCCCCBBBAA...", "...ACCBBAA......", "...ABBA.........",
  "..ABA...........", "..AA............", "................", "................"};
static const char *const art_saucer[16] = {
  "................", "................", "................", "......GGGG......",
  "....GHHHHHHG....", "...GHHIHHIHHG...", "..GJJJJJJJJJJG..", ".GJKKJKKJKKJKJG.",
  "GJJJJJJJJJJJJJJG", ".GGJJJJJJJJJJGG.", "...GGGGGGGGGG...", "....I..I..I..I..",
  "................", "................", "................", "................"};
static const char *const art_orb[16] = {
  "................", ".....LLLLLL.....", "...LLMMMMMMLL...", "..LMMMMMMMMMML..",
  ".LMMMNNNNNNMMML.", ".LMMNNOOOONNMML.", "LMMMNOOLLOONMMML", "LMMMNOLLLLONMMML",
  "LMMMNOLLLLONMMML", "LMMMNOOLLOONMMML", ".LMMNNOOOONNMML.", ".LMMMNNNNNNMMML.",
  "..LMMMMMMMMMML..", "...LLMMMMMMLL...", ".....LLLLLL.....", "................"};
static const char *const art_laser[8] = {
  "........", "........", "........", "EKKKKKKE", "EKKKKKKE", "........", "........", "........"};
static const char *const art_shot[8] = {
  "........", "..NNNN..", ".NNOONN.", ".NOOOON.", ".NOOOON.", ".NNOONN.", "..NNNN..", "........"};
static const char *const art_spark[8] = {
  "........", "........", "...BB...", "..BAAB..", "..BAAB..", "...BB...", "........", "........"};

static int art_colour(char c) { return (c >= 'A' && c <= 'O') ? c - 'A' + 1 : 0; }

// packs an ASCII picture (w x h pixels, multiples of 8) into 4 bpp cells, in sprite order
static void pack_art(uint8_t *dst, const char *const *rows, int w, int h) {
  int cw = w / 8, ch = h / 8;
  for (int cy = 0; cy < ch; cy++)
    for (int cx = 0; cx < cw; cx++) {
      uint8_t *cell = dst + (cy * cw + cx) * 32;
      for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x += 2) {
          const char *r = rows[cy * 8 + y] + cx * 8 + x;
          cell[y * 4 + x / 2] = (uint8_t)(art_colour(r[0]) | (art_colour(r[1]) << 4));
        }
    }
}

static void make_sprites(void) {
  static uint8_t sheet[16 * 32];
  memset(sheet, 0, sizeof sheet);
  pack_art(sheet + CELL_SHIP * 32, art_ship, 16, 16);
  pack_art(sheet + CELL_SAUCER * 32, art_saucer, 16, 16);
  pack_art(sheet + CELL_ORB * 32, art_orb, 16, 16);
  pack_art(sheet + CELL_LASER * 32, art_laser, 8, 8);
  pack_art(sheet + CELL_SHOT * 32, art_shot, 8, 8);
  pack_art(sheet + CELL_SPARK * 32, art_spark, 8, 8);
  mc_chr_upload(CHR_SPR, sheet, sizeof sheet);
  mc_chr_upload(CHR_FONT, font_chr, 96 * 32);
  mc_sprite_sheet(CHR(CHR_SPR));
}

// ---------------------------------------------------------------- procedural backgrounds
// A layer is a 512 x 256 picture given by a function colour(x, y); it is cut into 8 x 8 tiles,
// identical tiles are shared (hash then compare) and the tiles go to CHR RAM, the map to the layer.
typedef int (*pixel_fn)(int x, int y);
static uint8_t far_top[512], near_top[512];

static int far_pixel(int x, int y) {
  int top = far_top[x];
  if (y < top) return 0;
  if (y - top < 2) return 6;                       // lit ridge
  int band = (y - 128) / 16;                       // bands aligned on tiles: shared solid tiles
  return 1 + (band < 0 ? 0 : band > 4 ? 4 : band);
}

static int near_pixel(int x, int y) {
  if (y >= 216) return (((x >> 4) ^ (y >> 3)) & 1) ? 7 : 8;   // floor checkerboard
  int top = near_top[x];
  if (y < top) return 0;
  if (y - top < 2) return 6;
  int band = (y - 160) / 16;
  return 1 + (band < 0 ? 0 : band > 4 ? 4 : band);
}

static int build_layer(int layer, uint32_t chr_off, int max_tiles, pixel_fn colour, int pal, int prio) {
  uint8_t (*tiles)[32] = calloc((size_t)max_tiles, 32);
  uint32_t *hash = calloc((size_t)max_tiles, sizeof(uint32_t));
  uint32_t *map = malloc(64 * 32 * sizeof(uint32_t));
  if (!tiles || !hash || !map) { printf("build_layer: out of memory\n"); abort(); }
  int count = 1;                                   // tile 0 stays empty
  for (int ty = 0; ty < 32; ty++)
    for (int tx = 0; tx < 64; tx++) {
      uint8_t t[32];
      uint32_t h = 2166136261u, any = 0;
      for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x += 2) {
          int a = colour(tx * 8 + x, ty * 8 + y), b = colour(tx * 8 + x + 1, ty * 8 + y);
          uint8_t v = (uint8_t)(a | (b << 4));
          t[y * 4 + x / 2] = v; any |= v; h = (h ^ v) * 16777619u;
        }
      int n = 0;
      if (any) {
        for (n = 1; n < count; n++) if (hash[n] == h && !memcmp(tiles[n], t, 32)) break;
        if (n == count) {
          if (count == max_tiles) { printf("build_layer: too many tiles\n"); abort(); }
          memcpy(tiles[count], t, 32); hash[count] = h; count++;
        }
      }
      map[ty * 64 + tx] = MC_TILE(n, pal, 0);
    }
  mc_chr_upload(chr_off, tiles, (uint32_t)count * 32);
  mc_layer(layer, CHR(chr_off), MC_TILE8 | MC_4BPP, 64, 32, prio);
  mc_tilemap_load(layer, map, 64 * 32);
  free(map); free(hash); free(tiles);
  return count;
}

static void make_stars(void) {
  static uint8_t t[4][32];
  memset(t, 0, sizeof t);
  t[1][3 * 4 + 1] = 0x01;                          // one dim pixel
  t[2][4 * 4 + 2] = 0x02;                          // one brighter pixel
  t[3][2 * 4 + 1] = 0x30; t[3][3 * 4 + 1] = 0x33; t[3][3 * 4 + 2] = 0x03; t[3][4 * 4 + 1] = 0x30;   // small cross
  mc_chr_upload(CHR_STARS, t, sizeof t);
  mc_layer(2, CHR(CHR_STARS), MC_TILE8 | MC_4BPP, 64, 32, 0);
  for (int y = 0; y < 32; y++)
    for (int x = 0; x < 64; x++) {
      uint32_t r = mc_rand() % 100;
      int n = r < 6 ? 1 : r < 9 ? 2 : r < 10 ? 3 : 0;
      mc_tile(2, x, y, MC_TILE(n, PAL_STARS, 0));
    }
}

static void make_backgrounds(void) {
  for (int x = 0; x < 512; x++) {                 // periodic profiles: angle = 1024 steps per turn
    int a = x * 2;
    far_top[x]  = (uint8_t)(150 + ((mc_sin(a * 2) * 18 + mc_sin(a * 5) * 9 + mc_sin(a * 11) * 4) >> 16));
    near_top[x] = (uint8_t)(186 + ((mc_sin(a * 3) * 13 + mc_sin(a * 7) * 7 + mc_sin(a * 17) * 3) >> 16));
  }
  int nf = build_layer(1, CHR_FAR, 384, far_pixel, PAL_FAR, 1);
  int nn = build_layer(3, CHR_NEAR, 384, near_pixel, PAL_NEAR, 2);
  make_stars();
  mc_layer_linescroll(3, true);
  printf("backgrounds: %d + %d tiles, heap free %u\n", nf, nn, (unsigned)mc_heap_free());
}

static void sky(void) {                           // raster effect: one sky colour every 4 lines
  int k = mc_raster_line * 255 / 212;
  mc_color(SKY, MC_RGB(8 + k * 112 / 255, 6 + k * 34 / 255, 30 + k * 80 / 255));
}

static void scroll_backgrounds(uint32_t t) {
  int far = (int)(t >> 2), near = (int)t, stars = (int)(t >> 3);
  mc_layer_scroll(2, stars, 0);
  mc_layer_scroll(1, far, 0);
  mc_layer_scroll(3, near, 0);
  volatile linescroll_t *ls = mc_linescroll(3);
  for (int y = 216; y < 240; y++) ls[y].dx = (int16_t)(near * (y - 214) / 6);   // perspective floor
  int tw = 140 + (mc_sin((int)t * 12) * 100 >> 16);                              // twinkling stars
  mc_color(PAL_STARS * 16 + 3, MC_RGB(tw, tw, 255));
}

// ---------------------------------------------------------------- palettes and text
static void set_rgb(int pal, int i, int r, int g, int b) { mc_color(pal * 16 + i, MC_RGB(r, g, b)); }
static int font_white;

static void make_palettes(void) {
  static const uint8_t spr[16][3] = {
    {0, 0, 0}, {20, 30, 90}, {60, 120, 230}, {200, 210, 225}, {255, 140, 30}, {255, 230, 80},
    {120, 240, 255}, {120, 20, 30}, {230, 50, 60}, {255, 220, 60}, {110, 110, 130}, {200, 200, 215},
    {60, 20, 90}, {150, 60, 200}, {255, 90, 180}, {255, 255, 255}};
  for (int i = 1; i < 16; i++) set_rgb(PAL_SPR, i, spr[i][0], spr[i][1], spr[i][2]);
  static const uint8_t fx[4][2][3] = {
    {{255, 255, 230}, {255, 220, 90}}, {{255, 220, 90}, {255, 130, 30}},
    {{255, 130, 30}, {200, 40, 20}}, {{170, 40, 20}, {80, 20, 30}}};
  for (int k = 0; k < 4; k++) {
    set_rgb(PAL_FX + k, 1, fx[k][0][0], fx[k][0][1], fx[k][0][2]);
    set_rgb(PAL_FX + k, 2, fx[k][1][0], fx[k][1][1], fx[k][1][2]);
  }
  set_rgb(PAL_STARS, 1, 90, 90, 140); set_rgb(PAL_STARS, 2, 160, 160, 210); set_rgb(PAL_STARS, 3, 230, 230, 255);
  static const uint8_t far[7][3] = {{0, 0, 0}, {90, 60, 140}, {75, 50, 120}, {60, 40, 100}, {48, 32, 84}, {36, 24, 66}, {160, 120, 210}};
  for (int i = 1; i < 7; i++) set_rgb(PAL_FAR, i, far[i][0], far[i][1], far[i][2]);
  static const uint8_t near[9][3] = {{0, 0, 0}, {20, 70, 80}, {16, 58, 68}, {12, 46, 56}, {9, 36, 46}, {6, 26, 36},
                                     {70, 170, 150}, {28, 36, 66}, {48, 60, 100}};
  for (int i = 1; i < 9; i++) set_rgb(PAL_NEAR, i, near[i][0], near[i][1], near[i][2]);
  // recoloured copies of the SDK font palette (palette 0, set by mc_text_layer)
  font_white = 1;
  for (int i = 1; i < 16; i++) if (PALETTE[i] == MC_RGB(255, 255, 255)) font_white = i;
  static const uint8_t ink[9][3] = {{255, 220, 60}, {90, 220, 255}, {255, 110, 200},
                                    {255, 80, 80}, {255, 170, 40}, {255, 240, 80}, {90, 240, 110}, {80, 200, 255}, {200, 110, 255}};
  for (int p = 0; p < 9; p++) {
    for (int i = 0; i < 16; i++) PALETTE[(PAL_HUD + p) * 16 + i] = PALETTE[i];
    set_rgb(PAL_HUD + p, font_white, ink[p][0], ink[p][1], ink[p][2]);
  }
}

static void text(int col, int row, const char *s, int pal) { mc_text(0, col, row, s, pal); }
static void text_center(int row, const char *s, int pal) { text((40 - (int)strlen(s)) / 2, row, s, pal); }
static void text_clear(void) { mc_text_clear(0); }

// ---------------------------------------------------------------- sound
static int sfx_shot, sfx_boom, sfx_boom_freq;

static void sfx_update(void) {
  if (sfx_shot > 0) { sfx_shot--; mc_psg(2, PSG_SQUARE, 700 + sfx_shot * 220, 96, sfx_shot * 9, sfx_shot * 9); }
  if (sfx_boom > 0) { sfx_boom -= 6; if (sfx_boom < 0) sfx_boom = 0; mc_psg(3, PSG_NOISE, (uint32_t)sfx_boom_freq, 0, sfx_boom, sfx_boom); }
}
static void sfx_explode(int big) { sfx_boom = big ? 230 : 150; sfx_boom_freq = big ? 2500 : 6000; }

static const char *tune_lead =
  "A4 - C5 E5 - D5 C5 - B4 - G4 - A4 - - . E5 - G5 A5 - G5 E5 - D5 - C5 - B4 - - . ";
static const char *tune_bass =
  "A2 A3 A2 A3 A2 A3 A2 A3 F2 F3 F2 F3 G2 G3 G2 G3 C3 C4 C3 C4 C3 C4 C3 C4 G2 G3 G2 G3 E2 E3 E2 E3 ";

static void music(bool on) {
  if (on) { mc_tune(0, tune_lead, 8, 40); mc_tune(1, tune_bass, 8, 55); }
  else { mc_tune_stop(0); mc_tune_stop(1); }
}

// ---------------------------------------------------------------- game objects
typedef struct { bool on; int x, y, vx, vy; } shot_t;
typedef struct { bool on; int type, x, y, base_y, phase, hp, t, flash; } enemy_t;
typedef struct { int x, y, vx, vy, life; } particle_t;

static shot_t lasers[N_LASER], shots[N_SHOT];
static enemy_t enemies[N_ENEMY];
static particle_t *fx;                            // N_FX particles, from the heap
static int fx_next;
static int px, py, lives, invuln, cooldown;
static uint32_t score, best;

static void spr16(int n, int x, int y, int cell, int pal) {
  mc_sprite(n, x, y, cell, 2, 2, pal, 4); mc_sprite_zoom(n, 0x100, 0x100);
}
static void spr8(int n, int x, int y, int cell, int pal) {
  mc_sprite(n, x, y, cell, 1, 1, pal, 4); mc_sprite_zoom(n, 0x100, 0x100);
}

static void spark(int x, int y, int speed, int life) {     // x, y in pixels; speed in 1/256 px
  particle_t *p = &fx[fx_next]; fx_next = (fx_next + 1) % N_FX;
  int a = (int)(mc_rand() & 1023), s = speed / 2 + (int)(mc_rand() % (uint32_t)(speed + 1));
  p->x = x << FP; p->y = y << FP;
  p->vx = (mc_cos(a) * s) >> 16; p->vy = (mc_sin(a) * s) >> 16;
  p->life = life / 2 + (int)(mc_rand() % (uint32_t)(life / 2 + 1));
}
static void explosion(int x, int y, int n, int speed) { for (int i = 0; i < n; i++) spark(x, y, speed, 40); }

static void update_fx(void) {
  for (int i = 0; i < N_FX; i++) {
    particle_t *p = &fx[i];
    if (p->life <= 0) { mc_sprite_hide(S_FX + i); continue; }
    p->life--; p->x += p->vx; p->y += p->vy;
    p->vx = p->vx * 15 / 16; p->vy = p->vy * 15 / 16;   // drag
    int stage = p->life > 30 ? 0 : p->life > 20 ? 1 : p->life > 10 ? 2 : 3;
    int z = 0x80 + p->life * 8;                         // particles shrink as they cool
    mc_sprite(S_FX + i, PX(p->x) - 4, PX(p->y) - 4, CELL_SPARK, 1, 1, PAL_FX + stage, 4);
    mc_sprite_zoom(S_FX + i, z, z);
  }
}

static void reset_objects(void) {
  memset(lasers, 0, sizeof lasers); memset(shots, 0, sizeof shots); memset(enemies, 0, sizeof enemies);
  memset(fx, 0, N_FX * sizeof(particle_t));
  mc_sprites_hide_all();
}

static void spawn_enemy(int type, int x, int y, int phase) {
  for (int i = 0; i < N_ENEMY; i++)
    if (!enemies[i].on) {
      enemy_t *e = &enemies[i];
      e->on = true; e->type = type; e->x = x << FP; e->y = e->base_y = y << FP;
      e->phase = phase; e->hp = type ? 4 : 1; e->t = (int)(mc_rand() % 60); e->flash = 0;
      return;
    }
}

static void fire_at_player(int ex, int ey) {      // aimed shot: angle from mc_atan2
  for (int i = 0; i < N_SHOT; i++)
    if (!shots[i].on) {
      int a = mc_atan2((py + 8) - ey, (px + 8) - ex);
      shots[i] = (shot_t){true, ex << FP, ey << FP, (mc_cos(a) * 420) >> 16, (mc_sin(a) * 420) >> 16};
      return;
    }
}

static bool hit(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
  return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

static void hurt_player(void) {
  if (invuln) return;
  explosion(px + 8, py + 8, 40, 700);
  sfx_explode(1);
  lives--; invuln = 120;
}

// ---------------------------------------------------------------- one frame of play
static void hud(void) {
  char line[48];
  snprintf(line, sizeof line, "SCORE %06u", (unsigned)score);   text(1, 0, line, PAL_HUD);
  snprintf(line, sizeof line, "VIES %d", lives < 0 ? 0 : lives); text(16, 0, line, PAL_HUD + 1);
  snprintf(line, sizeof line, "RECORD %06u", (unsigned)best);   text(26, 0, line, PAL_HUD + 2);
}

static void play_frame(uint32_t t) {
  uint32_t pad = mc_pad(1);
  if (pad & IN_LEFT)  px -= 2;
  if (pad & IN_RIGHT) px += 2;
  if (pad & IN_UP)    py -= 2;
  if (pad & IN_DOWN)  py += 2;
  px = px < 0 ? 0 : px > 300 ? 300 : px;
  py = py < 12 ? 12 : py > 196 ? 196 : py;

  // player lasers
  if (cooldown > 0) cooldown--;
  if ((pad & IN_B(1)) && !cooldown && lives > 0) {
    for (int i = 0; i < N_LASER; i++)
      if (!lasers[i].on) { lasers[i] = (shot_t){true, (px + 12) << FP, (py + 4) << FP, 7 << FP, 0}; break; }
    cooldown = 7; sfx_shot = 6;
  }
  for (int i = 0; i < N_LASER; i++) {
    shot_t *s = &lasers[i];
    if (s->on) { s->x += s->vx; if (PX(s->x) > 320) s->on = false; }
    if (s->on) spr8(S_LASER + i, PX(s->x), PX(s->y), CELL_LASER, PAL_SPR); else mc_sprite_hide(S_LASER + i);
  }

  // waves: five saucers every 100 frames, an orb every 260
  if (t % 100 == 0) { int y = 30 + (int)(mc_rand() % 130); for (int k = 0; k < 5; k++) spawn_enemy(0, 330 + k * 22, y, k * 60); }
  if (t % 260 == 130) spawn_enemy(1, 330, 40 + (int)(mc_rand() % 130), 0);

  for (int i = 0; i < N_ENEMY; i++) {
    enemy_t *e = &enemies[i];
    if (!e->on) { mc_sprite_hide(S_ENEMY + i); continue; }
    e->t++;
    if (e->type == 0) { e->x -= 320; e->y = e->base_y + ((mc_sin(e->t * 8 + e->phase) * 26) >> 8); }
    else {
      e->x -= 128; e->y = e->base_y + ((mc_sin(e->t * 3) * 14) >> 8);
      if (e->t % 70 == 0 && PX(e->x) < 310) fire_at_player(PX(e->x) + 8, PX(e->y) + 8);
    }
    int ex = PX(e->x), ey = PX(e->y);
    if (ex < -20) { e->on = false; mc_sprite_hide(S_ENEMY + i); continue; }
    for (int j = 0; j < N_LASER; j++) {
      shot_t *s = &lasers[j];
      if (s->on && hit(PX(s->x), PX(s->y) + 3, 8, 2, ex + 1, ey + 3, 14, 10)) {
        s->on = false; e->hp--; e->flash = 4;
        if (e->hp <= 0) {
          explosion(ex + 8, ey + 8, e->type ? 48 : 18, e->type ? 900 : 600);
          sfx_explode(e->type); score += e->type ? 500 : 100;
          e->on = false; break;
        }
        spark(ex + 2, ey + 8, 300, 12);
      }
    }
    if (!e->on) { mc_sprite_hide(S_ENEMY + i); continue; }
    if (lives > 0 && hit(ex + 2, ey + 4, 12, 8, px + 3, py + 6, 10, 4)) hurt_player();
    if (e->flash) e->flash--;
    spr16(S_ENEMY + i, ex, ey, e->type ? CELL_ORB : CELL_SAUCER, e->flash ? PAL_FX : PAL_SPR);
  }

  for (int i = 0; i < N_SHOT; i++) {
    shot_t *s = &shots[i];
    if (s->on) {
      s->x += s->vx; s->y += s->vy;
      int sx = PX(s->x), sy = PX(s->y);
      if (sx < -8 || sx > 328 || sy < -8 || sy > 248) s->on = false;
      else if (lives > 0 && hit(sx - 2, sy - 2, 4, 4, px + 4, py + 6, 8, 4)) { s->on = false; hurt_player(); }
    }
    if (s->on) spr8(S_SHOT + i, PX(s->x) - 4, PX(s->y) - 4, CELL_SHOT, PAL_SPR); else mc_sprite_hide(S_SHOT + i);
  }

  // player, blinking while invulnerable, with an engine trail
  if (invuln) invuln--;
  if (lives > 0 && !(invuln & 4)) spr16(S_PLAYER, px, py, CELL_SHIP, PAL_SPR); else mc_sprite_hide(S_PLAYER);
  if (lives > 0 && (t & 1)) {
    particle_t *p = &fx[fx_next]; fx_next = (fx_next + 1) % N_FX;
    *p = (particle_t){(px + 1) << FP, (py + 8 + (int)(mc_rand() % 3) - 1) << FP, -(300 + (int)(mc_rand() % 200)), 0, 14};
  }
  update_fx();
  if (score > best) best = score;
}

// ---------------------------------------------------------------- screens
static void save_best(void) {                     // NVRAM: 32 KB, not yet saved to the SD card by the core
  memcpy((void *)MC_NVRAM, "NBLA", 4);
  for (int i = 0; i < 4; i++) MC_NVRAM[4 + i] = (uint8_t)(best >> (8 * i));
}
static void load_best(void) {
  best = 5000;
  if (!memcmp((const void *)MC_NVRAM, "NBLA", 4))
    best = MC_NVRAM[4] | (MC_NVRAM[5] << 8) | ((uint32_t)MC_NVRAM[6] << 16) | ((uint32_t)MC_NVRAM[7] << 24);
}

static void title_screen(void) {
  static const char name[] = "NEBULA";
  reset_objects(); text_clear();
  char line[48];
  text_center(15, "UN EXEMPLE DU SDK MC", PAL_HUD + 1);
  text_center(18, "CROIX : PILOTER   B1 : TIRER", 0);
  snprintf(line, sizeof line, "RECORD %06u", (unsigned)best); text_center(24, line, PAL_HUD + 2);
  for (uint32_t t = 0;; t++) {
    mc_frame();
    scroll_backgrounds(mc_frames);
    for (int i = 0; i < 6; i++) {                 // big letters: font cells zoomed x4, on a wave
      int n = S_ENEMY + i;
      mc_sprite(n, 64 + i * 32, 40 + (mc_sin((int)t * 10 + i * 90) * 10 >> 16), CELL_FONT + name[i] - 32, 1, 1, PAL_LETTER + i, 4);
      mc_sprite_zoom(n, 0x400, 0x400);
    }
    text_center(21, (t / 30) % 2 ? "                 " : "APPUYEZ SUR START", PAL_HUD);
    if (mc_pad_pressed(1) & IN_START) break;
  }
}

static void game(void) {
  reset_objects(); text_clear();
  px = 40; py = 100; lives = 3; invuln = 90; cooldown = 0; score = 0;
  for (uint32_t t = 1; lives > 0 || invuln > 0; t++) {
    mc_frame();
    scroll_backgrounds(mc_frames);
    play_frame(t);
    sfx_update();
    hud();
  }
}

static void game_over(void) {
  text_center(11, "PARTIE TERMINEE", PAL_HUD + 3);
  if (score >= best) { save_best(); text_center(13, "NOUVEAU RECORD !", PAL_HUD); }
  for (int t = 0; t < 240; t++) {
    mc_frame();
    scroll_backgrounds(mc_frames);
    update_fx(); sfx_update();
    if (t > 60 && (mc_pad_pressed(1) & IN_START)) break;
  }
}

int main(void) {
  mc_init();
  mc_text_layer(0, 3);                            // layer 0: text, in front of everything
  make_palettes();
  mc_color(SKY, 0);
  mc_background(SKY);
  text_center(14, "PREPARATION DES DECORS...", 0);
  mc_display(DISP_ON | DISP_LAYER(0));
  make_sprites();
  make_backgrounds();
  fx = calloc(N_FX, sizeof(particle_t));
  mc_sprites_used(S_USED);
  for (int y = 0; y <= 212; y += 4) mc_raster_add(y, sky);
  mc_display(DISP_ON | DISP_LAYER(0) | DISP_LAYER(1) | DISP_LAYER(2) | DISP_LAYER(3) | DISP_SPRITES);
  load_best();
  music(true);
  for (;;) { title_screen(); game(); game_over(); }
}
