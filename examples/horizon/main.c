// HORIZON - a pseudo-3D racing game for MONOCORE-1 (OutRun / Pole Position style), mc SDK.
//
// How the road is drawn: layer 2 holds a "texture" of 120 lines, line r being a straight road
// seen at the depth of screen line HORIZON + r (its width grows with r), twice: once in the light
// palette, once in the dark one. For each screen line, per-line scrolling picks the texture line
// (dy: right width, light or dark stripe) and shifts it sideways (dx: curves, player position).
// Roadside objects and other cars are sprites zoomed with their distance. Every graphic is built
// at start-up in CHR RAM; there is no image or sound file.
#include "mc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const uint8_t font_chr[];                  // SDK font, always added by build.py

// ---------------------------------------------------------------- constants
#define HORIZON   120                             // first road line on screen
#define ROWS      120                             // road lines (HORIZON to 239)
#define KZ        30000                           // depth of road line r: KZ / r
#define SEG       400                             // track segment length (world units)
#define TEX_EMPTY 240                             // transparent texture line
#define TEX_W     1024                            // road texture width; road centre at x = 512
#define ROAD_W    1024                            // half road width in world units
#define MAX_SPEED 200                             // world units per frame
#define PLAYER_Z  300                             // depth of the player's car

// CHR RAM map
#define CHR_SPR   0x0000
#define CHR_FONT  0x0800
#define CHR_ROAD  0x2000                          // 512 tiles at most
#define CHR_MTN   0x6000                          // 256 tiles at most
#define CHR_CLOUD 0x8000                          // 384 tiles at most
#define CELL_FONT (CHR_FONT / 32)

// sprite cells
#define CELL_CAR  0                               // 32 x 16: 4 x 2 cells
#define CELL_TREE 8                               // 16 x 32: 2 x 4 cells
#define CELL_BUSH 16                              // 16 x 16
#define CELL_SIGN 20                              // 16 x 16 (arrow to the right; flipped for left)

// palettes
#define PAL_HUD    1                              // 1-4: recoloured SDK font
#define PAL_LETTER 5                              // 5-11: title letters
#define PAL_CAR    16                             // player
#define PAL_OPP    17                             // 17-19: other cars
#define PAL_SCENE  20                             // trees, bushes, signs
#define PAL_ROAD_A 24
#define PAL_ROAD_B 25
#define PAL_MTN    26
#define PAL_CLOUD  27
#define SKY        4095

// sprite slots: the lowest number is in front
#define S_DIGITS  0                               // 0-2: big time digits
#define S_PLAYER  3
#define S_WORLD   4                               // 4-123: objects and cars, nearest first
#define N_WORLD   120
#define S_TITLE   (S_WORLD + N_WORLD)            // 124-130: title letters
#define S_USED    (S_TITLE + 7)

// ---------------------------------------------------------------- small canvas for sprites
// Draws into a w x h pixel canvas, then packs it into 4 bpp cells in sprite order.
static uint8_t canvas[32 * 32];
static int cv_w, cv_h;
static void cv_begin(int w, int h) { cv_w = w; cv_h = h; memset(canvas, 0, sizeof canvas); }
static void px(int x, int y, int c) { if (x >= 0 && x < cv_w && y >= 0 && y < cv_h) canvas[y * cv_w + x] = (uint8_t)c; }
static void rect(int x, int y, int w, int h, int c) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) px(x + i, y + j, c); }
static void cv_end(uint32_t chr_off) {
  uint8_t cells[16 * 32];
  int cw = cv_w / 8, ch = cv_h / 8;
  for (int cy = 0; cy < ch; cy++)
    for (int cx = 0; cx < cw; cx++)
      for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x += 2) {
          const uint8_t *s = &canvas[(cy * 8 + y) * cv_w + cx * 8 + x];
          cells[(cy * cw + cx) * 32 + y * 4 + x / 2] = (uint8_t)(s[0] | (s[1] << 4));
        }
  mc_chr_upload(chr_off, cells, (uint32_t)(cw * ch * 32));
}

static void make_sprites(void) {
  // car, seen from behind: 1 outline, 2 body, 3 dark body, 4 highlight, 5 tyres, 6 bumper,
  // 7 tail lights, 8 windscreen, 9 white
  cv_begin(32, 16);
  rect(10, 1, 12, 1, 1); rect(9, 2, 14, 3, 1); rect(10, 2, 12, 3, 8);   // roof and rear screen
  rect(11, 2, 3, 1, 9);                                                   // reflection
  rect(4, 5, 24, 1, 1); rect(3, 6, 26, 6, 1);
  rect(4, 5, 24, 6, 2); rect(5, 5, 22, 1, 4);                             // body and its highlight
  rect(5, 7, 4, 2, 7); rect(23, 7, 4, 2, 7);                              // tail lights
  rect(11, 8, 10, 3, 6); rect(12, 9, 8, 1, 1);                            // bumper and number plate
  rect(4, 10, 24, 1, 3);
  rect(2, 11, 7, 5, 5); rect(23, 11, 7, 5, 5);                            // tyres
  rect(9, 11, 14, 2, 3); rect(10, 13, 2, 2, 6); rect(20, 13, 2, 2, 6);   // diffuser, exhausts
  cv_end(CHR_SPR + CELL_CAR * 32);
  // tree: 10 dark green, 11 green, 12 light green, 13 trunk
  cv_begin(16, 32);
  rect(7, 24, 3, 8, 13);
  for (int layer = 0; layer < 3; layer++)
    for (int y = 0; y < 10; y++) {
      int top = layer * 8, half = 2 + y * (3 + layer) / 4;
      for (int x = -half; x <= half; x++) px(8 + x, top + y, x < -half + 2 ? 10 : x > half - 3 ? 12 : 11);
    }
  cv_end(CHR_SPR + CELL_TREE * 32);
  // bush
  cv_begin(16, 16);
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      int dx = 2 * x - 15, dy = 2 * y - 30, d = dx * dx + dy * dy;
      if (d < 240) px(x, y, d < 90 ? 12 : d < 170 ? 11 : 10);
    }
  cv_end(CHR_SPR + CELL_BUSH * 32);
  // curve sign: 14 yellow, 1 black arrow, 13 post
  cv_begin(16, 16);
  rect(7, 9, 2, 7, 13); rect(1, 0, 14, 10, 1); rect(2, 1, 12, 8, 14);
  for (int i = 0; i < 4; i++) { rect(4 + i * 2, 2 + i, 2, 1, 1); rect(4 + i * 2, 7 - i, 2, 1, 1); }
  cv_end(CHR_SPR + CELL_SIGN * 32);
  mc_chr_upload(CHR_FONT, font_chr, 96 * 32);
  mc_sprite_sheet(MC_CHR(CHR_SPR));
}

// ---------------------------------------------------------------- tile layers from a picture
typedef int (*pixel_fn)(int x, int y);
typedef int (*pal_fn)(int tile_row);

static int build_layer(int layer, uint32_t chr_off, int max_tiles, int map_w, int map_h,
                       pixel_fn colour, pal_fn palette, int prio) {
  uint8_t (*tiles)[32] = calloc((size_t)max_tiles, 32);
  uint32_t *hash = calloc((size_t)max_tiles, sizeof(uint32_t));
  uint32_t *map = malloc((size_t)(map_w * map_h) * sizeof(uint32_t));
  if (!tiles || !hash || !map) { printf("build_layer: out of memory\n"); abort(); }
  int count = 1;
  for (int ty = 0; ty < map_h; ty++)
    for (int tx = 0; tx < map_w; tx++) {
      uint8_t t[32];
      uint32_t h = 2166136261u, any = 0;
      for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x += 2) {
          uint8_t v = (uint8_t)(colour(tx * 8 + x, ty * 8 + y) | (colour(tx * 8 + x + 1, ty * 8 + y) << 4));
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
      map[ty * map_w + tx] = MC_TILE(n, palette(ty), 0);
    }
  mc_chr_upload(chr_off, tiles, (uint32_t)count * 32);
  mc_layer(layer, MC_CHR(chr_off), MC_TILE8 | MC_4BPP, map_w, map_h, prio);
  mc_tilemap_load(layer, map, map_w * map_h);
  free(map); free(hash); free(tiles);
  return count;
}

// road texture: line r (0-119 light palette, 120-239 dark palette, 240+ empty)
static int half_width(int r) { return 2 + r * 196 / (ROWS - 1); }
static int road_pixel(int x, int y) {
  if (y >= 2 * ROWS) return 0;
  int r = y % ROWS, hw = half_width(r), d = abs(x - TEX_W / 2);
  if (r == 0) return 1;
  if (d < hw) {
    int lw = hw / 40;
    if (r > 12 && (abs(d - hw / 3) <= lw)) return 4;       // lane markings
    return 3;                                               // tarmac
  }
  if (d < hw + hw / 7 + 1) return 2;                        // rumble strip
  return 1;                                                 // grass
}
static int road_palette(int ty) { return ty < ROWS / 8 ? PAL_ROAD_A : PAL_ROAD_B; }

static uint8_t mtn_top[512];
static int mtn_pixel(int x, int y) {
  int top = mtn_top[x];
  if (y < top || y >= 128) return 0;
  if (y - top < 2) return 4;                                // snow line
  return 1 + (y - 64 < 0 ? 0 : (y - 64) / 24 > 2 ? 2 : (y - 64) / 24);
}
static int mtn_palette(int ty) { (void)ty; return PAL_MTN; }

static int cloud_pixel(int x, int y) {                     // a few flat ellipses, repeated every 512 px
  static const int16_t cl[8][4] = {{40, 28, 46, 9}, {80, 22, 30, 7}, {170, 40, 56, 10}, {230, 18, 34, 6},
                                   {300, 34, 50, 9}, {360, 50, 40, 7}, {430, 24, 44, 8}, {480, 46, 30, 6}};
  for (int i = 0; i < 8; i++) {
    int dx = x - cl[i][0], dy = y - cl[i][1];
    if (dx > 256) dx -= 512;
    if (dx < -256) dx += 512;
    int a = cl[i][2], b = cl[i][3];
    int64_t e = (int64_t)dx * dx * b * b + (int64_t)dy * dy * a * a, lim = (int64_t)a * a * b * b;
    if (e < lim) return dy > b / 3 ? 2 : 1;                // shaded underside
  }
  return 0;
}
static int cloud_palette(int ty) { (void)ty; return PAL_CLOUD; }

// ---------------------------------------------------------------- track
typedef struct { int16_t length, curve; } section_t;      // curve: -200 (left) to 200 (right)
static const section_t sections[] = {
  {40, 0}, {35, 110}, {20, 0}, {45, -150}, {15, 0}, {30, 190}, {30, -90}, {55, 0}, {28, 170},
  {28, -170}, {35, 0}, {45, -120}, {25, 0}, {40, 150}, {20, 60}, {50, 0}, {35, -190}, {30, 0},
  {25, 120}, {25, -120}, {40, 0}};
#define NSEG_MAX 1024
static int16_t curve[NSEG_MAX];
static int8_t sign_dir[NSEG_MAX];                         // curve sign on this segment: -1, 0, 1
static int nseg;

static void make_track(void) {
  int16_t raw[NSEG_MAX];
  nseg = 0;
  for (unsigned s = 0; s < sizeof sections / sizeof sections[0]; s++)
    for (int i = 0; i < sections[s].length && nseg < NSEG_MAX; i++) raw[nseg++] = sections[s].curve;
  for (int i = 0; i < nseg; i++) {                         // eased curves: average over 9 segments
    int sum = 0;
    for (int k = -4; k <= 4; k++) sum += raw[(i + k + nseg) % nseg];
    curve[i] = (int16_t)(sum / 9);
  }
  memset(sign_dir, 0, sizeof sign_dir);
  for (int i = 0; i < nseg; i++) {                         // three signs before each bend
    int now = raw[i], next = raw[(i + 12) % nseg];
    if (now == 0 && next != 0)
      for (int k = 0; k < 3; k++) sign_dir[(i + k * 3) % nseg] = (int8_t)(next > 0 ? 1 : -1);
  }
}
static int seg_of(int32_t z) { return (int)((z / SEG) % nseg); }

// ---------------------------------------------------------------- per-frame road
static int32_t cam_z;                                      // distance driven (world units)
static int player_x, speed, crash;                         // player_x: 0 = road centre, +-ROAD_W = edges
static int line_off[240];                                  // road centre offset of each line (pixels)
static int depth_of_row[ROWS];

static void draw_road(void) {
  volatile linescroll_t *ls = mc_linescroll(2);
  int32_t x = 0, dx = 0;                                   // curve integration, 1/4096 pixel
  for (int r = ROWS - 1; r >= 1; r--) {
    int y = HORIZON + r;
    int32_t z = cam_z + depth_of_row[r];
    int seg = seg_of(z);
    dx += curve[seg];
    x += dx;
    int hw = half_width(r);
    int off = (int)(x >> 13) - player_x * hw / ROAD_W;
    // the texture repeats every TEX_W pixels and its edges are grass: the shift only has to stop
    // before the next copy of the road (half width + rumble) enters the 320-pixel window
    int lim = TEX_W - 160 - (TEX_W / 2 - 160) - hw - hw / 7 - 8 + 320;
    if (off > lim) off = lim;
    if (off < -lim) off = -lim;
    line_off[y] = off;
    int stripe = (int)((z / (SEG / 2)) & 1);              // light and dark bands, two per segment
    ls[y].dx = (int16_t)-off;
    ls[y].dy = (int16_t)(r + (stripe ? ROWS : 0) - y);
  }
  line_off[HORIZON] = line_off[HORIZON + 1];
  ls[HORIZON].dx = 0; ls[HORIZON].dy = 0;                  // horizon line: grass, light palette
  for (int y = 0; y < HORIZON; y++) { ls[y].dx = 0; ls[y].dy = (int16_t)(TEX_EMPTY - y); }
}

// ---------------------------------------------------------------- world objects and other cars
typedef struct { int32_t z; int x, cell, w, h, pal, base_zoom, flip; } item_t;
static item_t items[N_WORLD];
static int n_items;

typedef struct { int32_t z; int x, speed, pal; } opp_t;
#define N_OPP 7
static opp_t opps[N_OPP];

static void add_item(int32_t rel, int x, int cell, int w, int h, int pal, int base_zoom, int flip) {
  if (n_items < N_WORLD) items[n_items++] = (item_t){rel, x, cell, w, h, pal, base_zoom, flip};
}

static int by_depth(const void *a, const void *b) {        // nearest first (qsort from the libc)
  int32_t za = ((const item_t *)a)->z, zb = ((const item_t *)b)->z;
  return za < zb ? -1 : za > zb;
}

static void draw_world(void) {
  n_items = 0;
  int32_t track_len = (int32_t)nseg * SEG;
  int first = seg_of(cam_z);
  for (int k = 1; k < 44; k++) {                           // roadside objects of the next 43 segments
    int s = (first + k) % nseg;
    int32_t rel = (int32_t)k * SEG - cam_z % SEG;        // distance to that segment
    if (s % 4 == 0) add_item(rel, -1800, CELL_TREE, 2, 4, PAL_SCENE, 1000, 0);
    if (s % 4 == 2) add_item(rel, 1800, CELL_TREE, 2, 4, PAL_SCENE, 1000, 0);
    if (s % 6 == 1) add_item(rel + SEG / 2, (s & 8) ? 1400 : -1400, CELL_BUSH, 2, 2, PAL_SCENE, 600, 0);
    if (sign_dir[s]) add_item(rel, sign_dir[s] > 0 ? -1450 : 1450, CELL_SIGN, 2, 2, PAL_SCENE, 700, sign_dir[s] < 0);
  }
  for (int i = 0; i < N_OPP; i++) {
    int32_t rel = (opps[i].z - cam_z) % track_len;
    if (rel < 0) rel += track_len;
    if (rel > 120 && rel < 17000) add_item(rel, opps[i].x, CELL_CAR, 4, 2, opps[i].pal, 470, 0);
  }
  qsort(items, (size_t)n_items, sizeof(item_t), by_depth);
  int slot = S_WORLD;
  for (int i = 0; i < n_items; i++) {
    item_t *it = &items[i];
    if (it->z < 260) continue;
    int r = KZ / it->z;
    if (r < 3 || r >= ROWS) continue;
    int y = HORIZON + r, hw = half_width(r);
    int zoom = it->base_zoom * r / 100;                    // grows with the road width
    int sw = it->w * 8 * zoom >> 8, sh = it->h * 8 * zoom >> 8;
    int sx = 160 + line_off[y] + it->x * hw / ROAD_W;
    mc_sprite(slot, sx - sw / 2, y - sh, it->cell, it->w, it->h, it->pal, 4);
    mc_sprite_zoom(slot, zoom, zoom);
    mc_sprite_flip(slot, it->flip, false);
    slot++;
  }
  while (slot < S_WORLD + N_WORLD) mc_sprite_hide(slot++);
}

// ---------------------------------------------------------------- palettes, sky, text
static void set_rgb(int pal, int i, int r, int g, int b) { mc_color(pal * 16 + i, MC_RGB(r, g, b)); }

static void sky(void) {                                    // raster: sky gradient, every 4 lines
  int k = mc_raster_line * 255 / HORIZON;
  mc_color(SKY, MC_RGB(40 + k * 180 / 255, 110 + k * 110 / 255, 230 - k * 30 / 255));
}

static int font_white;
static void make_palettes(void) {
  static const uint8_t car[10][3] = {{0, 0, 0}, {20, 10, 15}, {220, 30, 40}, {140, 15, 25}, {255, 120, 120},
                                     {30, 30, 36}, {170, 170, 180}, {255, 200, 60}, {40, 60, 110}, {255, 255, 255}};
  static const uint8_t opp[3][3][3] = {{{40, 90, 230}, {20, 50, 150}, {130, 170, 255}},
                                       {{240, 200, 40}, {170, 130, 20}, {255, 240, 150}},
                                       {{235, 235, 240}, {150, 150, 165}, {255, 255, 255}}};
  for (int i = 1; i < 10; i++) set_rgb(PAL_CAR, i, car[i][0], car[i][1], car[i][2]);
  for (int p = 0; p < 3; p++) {
    for (int i = 1; i < 10; i++) set_rgb(PAL_OPP + p, i, car[i][0], car[i][1], car[i][2]);
    for (int i = 0; i < 3; i++) set_rgb(PAL_OPP + p, 2 + i, opp[p][i][0], opp[p][i][1], opp[p][i][2]);
  }
  static const uint8_t scene[15][3] = {{0}, {20, 10, 15}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0},
                                       {20, 80, 30}, {40, 140, 50}, {110, 200, 90}, {110, 70, 40}, {250, 210, 40}};
  for (int i = 1; i < 15; i++) set_rgb(PAL_SCENE, i, scene[i][0], scene[i][1], scene[i][2]);
  set_rgb(PAL_ROAD_A, 1, 70, 175, 70);  set_rgb(PAL_ROAD_A, 2, 240, 240, 240);
  set_rgb(PAL_ROAD_A, 3, 112, 112, 120); set_rgb(PAL_ROAD_A, 4, 240, 240, 240);
  set_rgb(PAL_ROAD_B, 1, 50, 150, 55);  set_rgb(PAL_ROAD_B, 2, 220, 40, 40);
  set_rgb(PAL_ROAD_B, 3, 102, 102, 110); set_rgb(PAL_ROAD_B, 4, 102, 102, 110);
  set_rgb(PAL_MTN, 1, 120, 130, 190); set_rgb(PAL_MTN, 2, 100, 110, 170); set_rgb(PAL_MTN, 3, 85, 95, 150);
  set_rgb(PAL_MTN, 4, 235, 240, 255);
  set_rgb(PAL_CLOUD, 1, 250, 250, 255); set_rgb(PAL_CLOUD, 2, 215, 220, 240);
  font_white = 1;
  for (int i = 1; i < 16; i++) if (PALETTE[i] == MC_RGB(255, 255, 255)) font_white = i;
  static const uint8_t ink[11][3] = {{255, 230, 60}, {255, 255, 255}, {255, 90, 80}, {90, 255, 120},
                                     {255, 70, 70}, {255, 160, 40}, {255, 230, 60}, {100, 230, 90},
                                     {70, 200, 255}, {120, 120, 255}, {230, 100, 255}};
  for (int p = 0; p < 11; p++) {
    for (int i = 0; i < 16; i++) PALETTE[(PAL_HUD + p) * 16 + i] = PALETTE[i];
    set_rgb(PAL_HUD + p, font_white, ink[p][0], ink[p][1], ink[p][2]);
  }
}

static void text(int col, int row, const char *s, int pal) { mc_text(0, col, row, s, pal); }
static void text_center(int row, const char *s, int pal) { text((40 - (int)strlen(s)) / 2, row, s, pal); }

static void big_text(int first_slot, int x, int y, const char *s, int zoom, int pal0, int pal_step) {
  for (int i = 0; s[i]; i++) {
    int n = first_slot + i;
    mc_sprite(n, x + i * (8 * zoom >> 8), y, CELL_FONT + s[i] - 32, 1, 1, pal0 + i * pal_step, 4);
    mc_sprite_zoom(n, zoom, zoom); mc_sprite_flip(n, false, false);
  }
}

// ---------------------------------------------------------------- sound
static void engine_sound(void) {
  int f = 55 + speed * 3 / 2 + (crash ? (int)(mc_rand() % 30) : 0);
  mc_psg(2, PSG_SAW, (uint32_t)f, 0, 28 + speed / 8, 28 + speed / 8);
  bool skid = crash > 0 || (abs(player_x) > ROAD_W + 150 && speed > 40);
  mc_psg(3, PSG_NOISE, 5000, 0, skid ? 70 : 0, skid ? 70 : 0);
}
static const char *tune_lead =
  "E5 - G5 - A5 - G5 E5 - D5 - C5 - D5 E5 - - - . C5 D5 E5 - G5 - E5 D5 - C5 - A4 - C5 D5 - - - . ";
static const char *tune_bass =
  "A2 A3 A2 A3 F2 F3 F2 F3 G2 G3 G2 G3 C3 C4 C3 C4 A2 A3 A2 A3 F2 F3 F2 F3 G2 G3 G2 G3 E2 E3 E2 E3 "
  "A2 A3 A2 A3 F2 F3 F2 F3 G2 G3 G2 G3 C3 C4 C3 C4 ";
static void music(bool on) {
  if (on) { mc_tune(0, tune_lead, 9, 34); mc_tune(1, tune_bass, 9, 42); }
  else { mc_tune_stop(0); mc_tune_stop(1); }
}

// ---------------------------------------------------------------- game
static int32_t mtn_x, cloud_x;
static int time_left, next_checkpoint, message;
static uint32_t best_m;

static void reset_opps(void) {
  for (int i = 0; i < N_OPP; i++)
    opps[i] = (opp_t){cam_z + 3000 + i * 4200, (i % 3 - 1) * 600, 90 + (int)(mc_rand() % 50), PAL_OPP + i % 3};
}

static void physics(uint32_t pad, bool demo) {
  int seg = seg_of(cam_z + PLAYER_Z);
  bool off_road = abs(player_x) > ROAD_W + 100;
  if (demo) {                                              // attract mode: the car steers itself
    speed = 150; player_x -= curve[seg] * speed / 1600; player_x -= player_x / 16;
  } else {
    if (crash) { crash--; speed = speed * 15 / 16; }
    else if (pad & IN_B(1)) speed += 2;
    else if (pad & (IN_B(2) | IN_DOWN)) speed -= 5;
    else speed -= 1;
    if (off_road && speed > MAX_SPEED * 2 / 5) speed -= 4;
    speed = speed < 0 ? 0 : speed > MAX_SPEED ? MAX_SPEED : speed;
    int steer = 44 * speed / MAX_SPEED + 6;
    if (pad & IN_LEFT)  player_x -= steer;
    if (pad & IN_RIGHT) player_x += steer;
    player_x -= curve[seg] * speed / 1600;                 // the bend pushes the car outwards
  }
  if (player_x > 2200) player_x = 2200;
  if (player_x < -2200) player_x = -2200;
  cam_z += speed;
  mtn_x += curve[seg] * speed; cloud_x += curve[seg] * speed;

  int32_t track_len = (int32_t)nseg * SEG;
  for (int i = 0; i < N_OPP; i++) {                        // other cars, and bumping into them
    opp_t *o = &opps[i];
    o->z += o->speed;
    int32_t rel = (o->z - cam_z) % track_len;
    if (rel < 0) rel += track_len;
    if (!demo && rel > PLAYER_Z - 120 && rel < PLAYER_Z + 60 && abs(o->x - player_x) < 700 && !crash) {
      crash = 40; speed = o->speed / 2; o->z += 600;
    }
    if (rel > track_len / 2) o->z += 30000;                // far behind: send it ahead again
  }
  if (!demo && off_road && abs(player_x) > 1650 && speed > 60 && !crash) {   // tree line
    int32_t near = cam_z + PLAYER_Z;
    if ((seg_of(near) % 2) == 0) { crash = 50; speed = 20; }
  }
}

static void scroll_scenery(void) {
  mc_layer_scroll(1, (int)(mtn_x >> 15), 0);
  mc_layer_scroll(3, (int)(cloud_x >> 16), 0);
  mc_layer_scroll(2, TEX_W / 2 - 160, 0);
}

static void draw_player(uint32_t t) {
  int bounce = (abs(player_x) > ROAD_W + 100 && speed > 20) ? (int)(t & 2) : (speed > 10 ? (int)((t >> 2) & 1) : 0);
  if (crash) bounce = (int)(mc_rand() % 4);
  mc_sprite(S_PLAYER, 160 - 24, 200 - bounce, CELL_CAR, 4, 2, PAL_CAR, 5);
  mc_sprite_zoom(S_PLAYER, 0x180, 0x180);
}

static void hud(void) {
  char line[48];
  snprintf(line, sizeof line, "%3d KM/H ", speed * 290 / MAX_SPEED);   text(1, 1, line, PAL_HUD + 1);
  snprintf(line, sizeof line, "%3u.%02u KM", (unsigned)(cam_z / 80000), (unsigned)(cam_z % 80000 / 800));
  text(28, 1, line, PAL_HUD + 1);
  text(17, 1, "TEMPS", PAL_HUD);
  snprintf(line, sizeof line, "%2d", (time_left + 59) / 60);
  big_text(S_DIGITS, 144, 18, line, 0x300, time_left < 600 && (time_left & 16) ? PAL_HUD + 2 : PAL_HUD, 0);
  mc_sprite_hide(S_DIGITS + 2);
  if (message > 0) { message--; text_center(9, message ? "  POINT DE CONTROLE !  " : "                       ", PAL_HUD + 3); }
}

static void title_screen(void) {
  static const char name[] = "HORIZON";
  mc_text_clear(0);
  text_center(12, "UNE COURSE EN FAUSSE 3D", PAL_HUD + 1);
  text_center(15, "B1 : ACCELERER   B2 : FREINER", PAL_HUD + 1);
  char line[40];
  snprintf(line, sizeof line, "RECORD %u.%02u KM", (unsigned)(best_m / 1000), (unsigned)(best_m % 1000 / 10));
  text_center(29, line, PAL_HUD);
  player_x = 0; crash = 0;
  for (uint32_t t = 0;; t++) {
    mc_frame();
    physics(0, true);
    scroll_scenery(); draw_road(); draw_world(); draw_player(t);
    for (int i = 0; i < 7; i++) {
      mc_sprite(S_TITLE + i, 48 + i * 32, 36 + (mc_sin((int)t * 10 + i * 100) * 6 >> 16), CELL_FONT + name[i] - 32, 1, 1, PAL_LETTER + i, 4);
      mc_sprite_zoom(S_TITLE + i, 0x400, 0x400); mc_sprite_flip(S_TITLE + i, false, false);
    }
    text_center(17, (t / 30) % 2 ? "                 " : "APPUYEZ SUR START", PAL_HUD);
    engine_sound();
    if (mc_pad_pressed(1) & IN_START) break;
  }
  for (int i = 0; i < 7; i++) mc_sprite_hide(S_TITLE + i);
}

static void race(void) {
  mc_text_clear(0);
  speed = 0; player_x = 0; crash = 0; message = 0;
  time_left = 60 * 60;
  next_checkpoint = (int)(cam_z / SEG) + 150;
  reset_opps();
  for (uint32_t t = 0; time_left > 0 || speed > 0; t++) {
    mc_frame();
    uint32_t pad = time_left > 0 ? mc_pad(1) : 0;
    physics(pad, false);
    if (time_left > 0) time_left--;
    if ((int)(cam_z / SEG) >= next_checkpoint) {           // every 150 segments: 25 more seconds
      next_checkpoint += 150; time_left += 25 * 60; message = 120;
    }
    scroll_scenery(); draw_road(); draw_world(); draw_player(t);
    hud();
    engine_sound();
  }
}

static int32_t race_start;
static void game_over(void) {
  uint32_t m = (uint32_t)((cam_z - race_start) / 80);
  text_center(10, "FIN DE LA COURSE", PAL_HUD + 2);
  char line[40];
  snprintf(line, sizeof line, "DISTANCE %u.%02u KM", (unsigned)(m / 1000), (unsigned)(m % 1000 / 10));
  text_center(12, line, PAL_HUD + 1);
  if (m > best_m) { best_m = m; text_center(14, "NOUVEAU RECORD !", PAL_HUD); }
  mc_psg(2, PSG_SAW, 0, 0, 0, 0); mc_psg(3, PSG_NOISE, 0, 0, 0, 0);
  for (int t = 0; t < 240; t++) {
    mc_frame();
    scroll_scenery(); draw_road(); draw_world(); draw_player((uint32_t)t);
    if (t > 60 && (mc_pad_pressed(1) & IN_START)) break;
  }
}

int main(void) {
  mc_init();
  mc_text_layer(0, 3);
  make_palettes();
  mc_color(SKY, MC_RGB(40, 110, 230));
  mc_background(SKY);
  text_center(14, "CONSTRUCTION DE LA ROUTE...", 0);
  mc_display(DISP_ON | DISP_LAYER(0));
  for (int r = 1; r < ROWS; r++) depth_of_row[r] = KZ / r;
  make_sprites();
  make_track();
  for (int x = 0; x < 512; x++) {
    int a = x * 2;
    mtn_top[x] = (uint8_t)(92 + ((mc_sin(a * 3) * 14 + mc_sin(a * 7) * 8 + mc_sin(a * 19) * 3) >> 16));
  }
  int nr = build_layer(2, CHR_ROAD, 512, 128, 32, road_pixel, road_palette, 1);
  int nm = build_layer(1, CHR_MTN, 256, 64, 32, mtn_pixel, mtn_palette, 0);
  int nc = build_layer(3, CHR_CLOUD, 384, 64, 32, cloud_pixel, cloud_palette, 0);
  mc_layer_linescroll(2, true);
  printf("road %d, mountains %d, clouds %d tiles; track %d segments; heap free %u\n",
         nr, nm, nc, nseg, (unsigned)mc_heap_free());
  for (int y = 0; y < HORIZON; y += 4) mc_raster_add(y, sky);
  mc_sprites_used(S_USED);
  mc_display(DISP_ON | DISP_LAYER(0) | DISP_LAYER(1) | DISP_LAYER(2) | DISP_LAYER(3) | DISP_SPRITES);
  music(true);
  best_m = 2000;
  for (;;) { title_screen(); race_start = cam_z; race(); game_over(); }
}
