// VIRUS MEADOW - a cooperative 4-player pong / breakout for MONOCORE-1 ("mc" SDK).
//
// Four paddles guard the four sides of a square meadow (left, top, bottom, right). A virus grows
// from the middle of the meadow, cell by cell. A single puck breaks the virus cells; the players
// must keep the virus away from the fence around the field, and keep the puck inside.
//   - the virus reaches the fence: the meadow is lost (game over);
//   - the puck leaves the field: one puck lost, the virus gets a growth surge, the player who
//     missed serves again; no puck left: game over;
//   - every virus cell destroyed: level cleared, next level (more colonies, faster growth).
// Seats without a human are played by the console; START on a pad joins the game at any time
// (START again: pause). Every graphic is generated at start-up in CHR RAM.
#include "mc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const uint8_t font_chr[], font_pal[];   // SDK font, always added by build.py

// ---------------------------------------------------------------- geometry (arena pixels)
#define SCR_W     360                      // 360 x 270 = exactly x4 in 1080p
#define SCR_H     270
#define AX        52                       // arena origin on screen (256 x 256, centred)
#define AY        7
#define AW        256
#define GRID      32                       // 8 x 8 cells
#define F_MIN     3                        // virus field: cells 3..28 (26 x 26)
#define F_MAX     28
#define F_CELLS   ((F_MAX - F_MIN + 1) * (F_MAX - F_MIN + 1))
#define POST      24                       // corner posts, 24 x 24
#define PAD_LEN   40
#define PAD_T     6                        // paddle thickness
#define PAD_IN    4                        // paddle distance from the arena edge
#define PAD_MIN   (POST << 8)
#define PAD_MAX   ((AW - POST - PAD_LEN) << 8)
#define AI_ERR    12                       // aiming error of the console players, pixels
#define BALL_R    3                        // ball box: [x - 3, x + 3)
#define FP        8                        // 8.8 fixed point
#define PX(v)     ((v) >> FP)

// ---------------------------------------------------------------- CHR RAM and palettes
#define CHR_SPR    0x0000
#define CHR_VIRUS  0x1000
#define CHR_FONT   0x2000
#define CHR_PANEL  0x3000
#define CHR_BG     0x4000
#define BG_TILES   1500
#define CHR(off)   MC_CHR(off)

#define CELL_PADV  0                       // 1 x 5 cells
#define CELL_PADH  5                       // 5 x 1 cells
#define CELL_BALL  10
#define CELL_SPARK 11
#define CELL_RING  12                      // serve ring, 2 x 2 cells
#define CELL_FONT  (CHR_FONT / 32)

#define PAL_HUD     1                      // 1-4 players, 5 virus, 6 gold, 7 grey
#define PAL_BG      16
#define PAL_VIRUS   17
#define PAL_PLAYER  20                     // 20-23
#define PAL_NEUTRAL 24
#define PAL_FX      25
#define PAL_LETTER  26                     // 26-36
#define PAL_PANEL   37
#define BG_ENTRY    4095

#define S_BALL     0
#define S_RING     1
#define S_PAD      2                       // 2-5
#define S_FX       8
#define N_FX       64
#define S_USED     (S_FX + N_FX)

// ---------------------------------------------------------------- small helpers
static uint32_t hash2(int x, int y) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static int rnd(int n) { return n > 0 ? (int)(mc_rand() % (uint32_t)n) : 0; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static void set_rgb(int pal, int i, int r, int g, int b) { mc_color(pal * 16 + i, MC_RGB(r, g, b)); }

// ---------------------------------------------------------------- sprite art
static void put4(uint8_t *cells, int cw, int x, int y, int c) {   // one pixel into row-major cells
  uint8_t *cell = cells + ((y / 8) * cw + x / 8) * 32 + (y % 8) * 4 + (x % 8) / 2;
  if (x & 1) *cell = (uint8_t)((*cell & 0x0F) | (c << 4)); else *cell = (uint8_t)((*cell & 0xF0) | c);
}

static int paddle_pixel(int x, int y) {          // vertical paddle, 8 x 40
  if (x < 1 || x > 6) return 0;
  if ((y == 0 || y == PAD_LEN - 1) && (x == 1 || x == 6)) return 0;
  if (y == 0) return 4;
  if (y == PAD_LEN - 1) return 1;
  if ((y == 5 || y == 8 || y == PAD_LEN - 9 || y == PAD_LEN - 6) && x > 1 && x < 6) return 1;   // grips
  static const int8_t shade[8] = {0, 4, 3, 3, 2, 2, 1, 0};
  if (y > 14 && y < PAD_LEN - 15 && (x == 3 || x == 4)) return 5;                             // centre light
  return shade[x];
}

static const char *const art_ball[8] = {
  "........", "..1111..", ".155331.", ".153331.", ".133331.", ".133321.", "..1111..", "........"};
static const char *const art_spark[8] = {
  "........", "........", "...21...", "..2112..", "..2112..", "...12...", "........", "........"};
static const char *const art_ring[16] = {
  "................", ".....555555.....", "...55......55...", "..5..........5..",
  ".5............5.", ".5............5.", "5..............5", "5..............5",
  "5..............5", "5..............5", ".5............5.", ".5............5.",
  "..5..........5..", "...55......55...", ".....555555.....", "................"};

static void make_sprites(void) {
  static uint8_t sheet[16 * 32];
  memset(sheet, 0, sizeof sheet);
  for (int y = 0; y < PAD_LEN; y++)
    for (int x = 0; x < 8; x++) {
      put4(sheet + CELL_PADV * 32, 1, x, y, paddle_pixel(x, y));
      put4(sheet + CELL_PADH * 32, 5, y, x, paddle_pixel(x, y));   // transposed
    }
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) {
      char a = art_ball[y][x], s = art_spark[y][x];
      put4(sheet + CELL_BALL * 32, 1, x, y, a == '.' ? 0 : a - '0');
      put4(sheet + CELL_SPARK * 32, 1, x, y, s == '.' ? 0 : s - '0');
    }
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++)
      put4(sheet + CELL_RING * 32, 2, x, y, art_ring[y][x] == '.' ? 0 : 5);
  mc_chr_upload(CHR_SPR, sheet, sizeof sheet);
  mc_chr_upload(CHR_FONT, font_chr, 96 * 32);
  mc_sprite_sheet(CHR(CHR_SPR));
}

// ---------------------------------------------------------------- virus tiles
// bricks of 7 x 7 pixels (the eighth row and column let the grass show between them):
// 'a' bevel light, 'b' body, 'c' bevel dark, 'd' nucleus; two variants per strength (1, 2, 3)
static const char *const art_virus[6][8] = {
  {"aaaaaab.", "abbbbbc.", "abbdbbc.", "abdddbc.", "abbdbbc.", "abbbbbc.", "bcccccc.", "........"},
  {"aaaaaab.", "abbbbbc.", "abbbdbc.", "abbddbc.", "abbbbbc.", "abdbbbc.", "bcccccc.", "........"},
  {"aaaaaab.", "abbbbbc.", "abdddbc.", "abdbdbc.", "abdddbc.", "abbbbbc.", "bcccccc.", "........"},
  {"aaaaaab.", "abdbdbc.", "abbdbbc.", "abdbdbc.", "abbbbbc.", "abbdbbc.", "bcccccc.", "........"},
  {"aaaaaab.", "adbbbdc.", "abdddbc.", "abdadbc.", "abdddbc.", "adbbbdc.", "bcccccc.", "........"},
  {"aaaaaab.", "abdddbc.", "addadbc.", "adaaadc.", "addadbc.", "abdddbc.", "bcccccc.", "........"}};

static void make_virus_tiles(void) {
  static const int8_t code_colour[4] = {4, 2, 1, 3};   // a b c d -> glint body rim nucleus
  static uint8_t t[7][32];
  memset(t, 0, sizeof t);
  for (int k = 0; k < 6; k++) {
    int base = (k / 2) * 4;
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++) {
        char c = art_virus[k][y][x];
        put4(t[k + 1], 1, x, y, c == '.' ? 0 : base + code_colour[c - 'a']);
      }
  }
  mc_chr_upload(CHR_VIRUS, t, sizeof t);
  mc_layer(2, CHR(CHR_VIRUS), MC_TILE8 | MC_4BPP, 64, 64, 1);
  mc_tilemap_fill(2, 0);
}

// ---------------------------------------------------------------- text panels (layer 3)
// dark panels behind the text, on the text grid: tile 1 + (top | bottom << 1 | left << 2 | right << 3)
static void make_panel_tiles(void) {
  static uint8_t t[17][32];
  memset(t, 0, sizeof t);
  for (int f = 0; f < 16; f++)
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++) {
        bool edge = ((f & 1) && y == 0) || ((f & 2) && y == 7) || ((f & 4) && x == 0) || ((f & 8) && x == 7);
        put4(t[f + 1], 1, x, y, edge ? 2 : 1);
      }
  mc_chr_upload(CHR_PANEL, t, sizeof t);
  mc_layer(3, CHR(CHR_PANEL), MC_TILE8 | MC_4BPP, 64, 64, 2);
  mc_tilemap_fill(3, 0);
}
static void panel(int c0, int r0, int c1, int r1) {
  for (int r = r0; r <= r1; r++)
    for (int c = c0; c <= c1; c++) {
      int f = (r == r0) | (r == r1) << 1 | (c == c0) << 2 | (c == c1) << 3;
      mc_tile(3, c, r, MC_TILE(1 + f, PAL_PANEL, 0));
    }
}
static void panel_clear(void) { mc_tilemap_fill(3, 0); }

// ---------------------------------------------------------------- the meadow (layer 1)
// colours of PAL_BG
enum { G_DARK = 1, G_MID, G_LIGHT, G_BLADE, FL_WHITE, FL_YELLOW, FL_BLUE, SOIL_D, SOIL, STONE_D,
       STONE_L, WOOD, TAPE_A, TAPE_B, BORDER };

static int grass(int x, int y) {
  uint32_t h = hash2(x, y);
  int patch = (mc_sin(x * 9) + mc_sin(y * 11) + mc_sin((x + y) * 5)) >> 14;   // -12..12
  int v = (int)(h & 15) + patch;
  int c = v < 3 ? G_DARK : v < 13 ? G_MID : G_LIGHT;
  if (((hash2(x, y >> 2) >> 7) & 31) == 0) c = G_BLADE;                         // grass blades
  int cx = x >> 3, cy = y >> 3;
  uint32_t f = hash2(cx + 1000, cy + 77);
  if (f % 9 == 0) {                                                             // a small flower
    int fx = (cx << 3) + 2 + (int)(f >> 8) % 4, fy = (cy << 3) + 2 + (int)(f >> 12) % 4;
    int dx = x - fx, dy = y - fy;
    if (dx == 0 && dy == 0) return FL_YELLOW;
    if ((dx == 0 && (dy == 1 || dy == -1)) || (dy == 0 && (dx == 1 || dx == -1)))
      return (f >> 16) & 1 ? FL_WHITE : FL_BLUE;
  }
  return c;
}

static int meadow_pixel(int x, int y) {          // arena coordinates, -52..307 x -7..262
  if (x < 0 || y < 0 || x >= AW || y >= AW) {
    if (x >= -3 && x < AW + 3 && y >= -3 && y < AW + 3) return BORDER;          // frame
    return (((x + 4) & 7) == 0 && ((y + 4) & 7) == 0) ? BORDER : 0;            // dotted panel
  }
  bool lx = x < POST || x >= AW - POST, ly = y < POST || y >= AW - POST;
  if (lx && ly) {                                                               // stone corner post
    int u = x < POST ? x : x - (AW - POST), v = y < POST ? y : y - (AW - POST);
    if (u == 0 || v == 0) return STONE_L;
    if (u == POST - 1 || v == POST - 1) return SOIL_D;
    if (v % 8 == 7 || (u + (v / 8) * 4) % 8 == 7) return SOIL_D;                 // mortar
    if (v % 8 == 0) return STONE_L;
    return STONE_D;
  }
  int d = x < 16 ? x : x >= AW - 16 ? AW - 1 - x : y < 16 ? y : y >= AW - 16 ? AW - 1 - y : -1;
  if (d >= 0) return (hash2(x, y) & 3) ? SOIL : SOIL_D;                         // paddle track
  int f = x < POST ? x - 16 : x >= AW - POST ? AW - POST + 7 - x : y < POST ? y - 16 : y >= AW - POST ? AW - POST + 7 - y : -1;
  if (f >= 0) {                                                                 // fence: hazard tape
    if (f == 2 || f == 5) return WOOD;
    if (f == 3 || f == 4) return ((x + y) >> 2) & 1 ? TAPE_A : TAPE_B;
  }
  return grass(x, y);
}

static uint32_t meadow_map[64 * 64];            // kept to come back from the title picture

static void build_meadow(void) {
  uint8_t (*tiles)[32] = calloc(BG_TILES, 32);
  uint32_t *hash = calloc(BG_TILES, sizeof(uint32_t));
  uint32_t *map = meadow_map;
  if (!tiles || !hash) { printf("build_meadow: out of memory\n"); abort(); }
  int count = 1;
  for (int ty = 0; ty < 64; ty++)
    for (int tx = 0; tx < 64; tx++) {
      int ax = tx * 8 >= AW + 52 ? tx * 8 - 512 : tx * 8;                       // map -> arena
      int ay = ty * 8 >= AW + 7 ? ty * 8 - 512 : ty * 8;
      if (ax < -AX - 8 || ay < -AY - 8 || ax >= SCR_W - AX || ay >= SCR_H - AY) continue;
      uint8_t t[32];
      uint32_t h = 2166136261u, any = 0;
      for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x += 2) {
          uint8_t v = (uint8_t)(meadow_pixel(ax + x, ay + y) | (meadow_pixel(ax + x + 1, ay + y) << 4));
          t[y * 4 + x / 2] = v; any |= v; h = (h ^ v) * 16777619u;
        }
      int n = 0;
      if (any) {
        for (n = 1; n < count; n++) if (hash[n] == h && !memcmp(tiles[n], t, 32)) break;
        if (n == count) {
          if (count == BG_TILES) { printf("build_meadow: too many tiles\n"); abort(); }
          memcpy(tiles[count], t, 32); hash[count] = h; count++;
        }
      }
      map[ty * 64 + tx] = MC_TILE(n, PAL_BG, 0);
    }
  mc_chr_upload(CHR_BG, tiles, (uint32_t)count * 32);
  mc_layer(1, CHR(CHR_BG), MC_TILE8 | MC_4BPP, 64, 64, 0);
  mc_tilemap_load(1, map, 64 * 64);
  free(hash); free(tiles);
  printf("meadow: %d tiles, heap free %u\n", count, (unsigned)mc_heap_free());
}

// ---------------------------------------------------------------- palettes and text
static int font_white;
static const uint8_t player_rgb[4][3] = {{60, 230, 80}, {240, 50, 45}, {50, 130, 255}, {255, 170, 30}};   // as on the cover
static const char *const seat_name[4] = {"GAUCHE", "HAUT", "BAS", "DROITE"};

static void ramp(int pal, int r, int g, int b) {        // 1 dark .. 4 highlight, 5 white-ish
  set_rgb(pal, 1, r * 2 / 5, g * 2 / 5, b * 2 / 5);
  set_rgb(pal, 2, r * 7 / 10, g * 7 / 10, b * 7 / 10);
  set_rgb(pal, 3, r, g, b);
  set_rgb(pal, 4, (r + 255) / 2, (g + 255) / 2, (b + 255) / 2);
  set_rgb(pal, 5, (r + 3 * 255) / 4, (g + 3 * 255) / 4, (b + 3 * 255) / 4);
}

static void make_palettes(void) {
  static const uint8_t bg[16][3] = {
    {0, 0, 0}, {34, 92, 40}, {52, 124, 48}, {78, 156, 60}, {130, 200, 84}, {245, 245, 235},
    {250, 210, 60}, {130, 170, 255}, {58, 40, 28}, {92, 66, 42}, {92, 94, 106}, {150, 152, 164},
    {150, 110, 60}, {230, 60, 40}, {240, 225, 200}, {70, 110, 80}};
  for (int i = 1; i < 16; i++) set_rgb(PAL_BG, i, bg[i][0], bg[i][1], bg[i][2]);
  mc_color(BG_ENTRY, MC_RGB(14, 24, 18));
  for (int p = 0; p < 4; p++) ramp(PAL_PLAYER + p, player_rgb[p][0], player_rgb[p][1], player_rgb[p][2]);
  ramp(PAL_NEUTRAL, 200, 200, 210);
  set_rgb(PAL_FX, 1, 255, 230, 255); set_rgb(PAL_FX, 2, 220, 80, 230);
  font_white = 1;
  for (int i = 1; i < 16; i++) if (PALETTE[i] == MC_RGB(255, 255, 255)) font_white = i;
  int font_dark = font_white == 1 ? 2 : 1;
  for (int i = 1; i < 16; i++) if (i != font_white && PALETTE[i]) font_dark = i;
  for (int i = 0; i < 11; i++) {                                     // VIRUS purple, MEADOW green
    if (i < 5) { set_rgb(PAL_LETTER + i, font_white, 245, 90 + i * 20, 250); set_rgb(PAL_LETTER + i, font_dark, 60, 0, 70); }
    else { set_rgb(PAL_LETTER + i, font_white, 200 + (i - 5) * 9, 255, 90); set_rgb(PAL_LETTER + i, font_dark, 20, 55, 10); }
  }
  set_rgb(PAL_PANEL, 1, 12, 22, 16); set_rgb(PAL_PANEL, 2, 110, 170, 110);
  static const uint8_t ink[7][3] = {{90, 235, 110}, {255, 90, 90}, {110, 160, 255}, {255, 185, 60},
                                    {235, 120, 245}, {255, 200, 60}, {150, 160, 150}};
  for (int p = 0; p < 7; p++) {
    for (int i = 0; i < 16; i++) PALETTE[(PAL_HUD + p) * 16 + i] = PALETTE[i];
    set_rgb(PAL_HUD + p, font_white, ink[p][0], ink[p][1], ink[p][2]);
  }
}

static void virus_colours(uint32_t t) {          // pulsing virus, every frame
  int k = (mc_sin((int)t * 12) + 65536) >> 12;   // 0..32
  static const int16_t c[3][4][3] = {
    {{170, 90, 200}, {230, 160, 240}, {255, 215, 255}, {255, 255, 255}},
    {{120, 30, 150}, {180, 60, 200}, {235, 110, 240}, {255, 220, 255}},
    {{55, 8, 75}, {105, 18, 130}, {200, 40, 150}, {255, 100, 190}}};
  for (int s = 0; s < 3; s++)
    for (int i = 0; i < 4; i++) {
      int add = (i == 1 || i == 2) ? k - 16 : 0;
      set_rgb(PAL_VIRUS, s * 4 + i + 1, clampi(c[s][i][0] + add, 0, 255), clampi(c[s][i][1] + add / 2, 0, 255),
              clampi(c[s][i][2] + add, 0, 255));
    }
}

static void text(int col, int row, const char *s, int pal) { mc_text(0, col, row, s, pal); }
static void text_center(int row, const char *s, int pal) { text((45 - (int)strlen(s)) / 2, row, s, pal); }
static void text_clear(void) { mc_text_clear(0); }

// ---------------------------------------------------------------- sound
static int sfx_pad, sfx_pad_f, sfx_break, sfx_hit, sfx_hit_f, sfx_alarm, sfx_miss, sfx_grow;

static void sfx_update(void) {
  if (sfx_pad > 0) { sfx_pad--; mc_psg(2, PSG_SQUARE, (uint32_t)sfx_pad_f, 64, sfx_pad * 14, sfx_pad * 14); }
  if (sfx_break > 0) { sfx_break -= 10; if (sfx_break < 0) sfx_break = 0; mc_psg(3, PSG_NOISE, 5000, 0, sfx_break, sfx_break); }
  if (sfx_hit > 0) { sfx_hit--; mc_psg(4, PSG_TRIANGLE, (uint32_t)sfx_hit_f, 0, sfx_hit * 22, sfx_hit * 22); }
  if (sfx_miss > 0) { sfx_miss--; mc_psg(5, PSG_SAW, (uint32_t)(120 + sfx_miss * 6), 0, sfx_miss * 2, sfx_miss * 2); }
  else if (sfx_alarm > 0) { sfx_alarm--; int on = (sfx_alarm / 8) & 1; mc_psg(5, PSG_SQUARE, on ? 1320 : 990, 128, 30, 30); if (!sfx_alarm) mc_psg_volume(5, 0, 0); }
  else mc_psg_volume(5, 0, 0);
  if (sfx_grow > 0) { sfx_grow--; mc_psg(6, PSG_TRIANGLE, (uint32_t)(90 + sfx_grow * 10), 0, sfx_grow * 6, sfx_grow * 6); }
}
static void sfx_silence(void) { for (int c = 2; c < 8; c++) mc_psg_volume(c, 0, 0); sfx_pad = sfx_break = sfx_hit = sfx_alarm = sfx_miss = sfx_grow = 0; }

static const char *tune_lead =
  "E5 - G5 - A5 - G5 E5 - D5 - C5 - D5 - E5 - - - G4 - A4 - C5 - D5 - E5 - D5 - - - "
  "E5 - G5 - A5 - C6 B5 - A5 - G5 - E5 - G5 - - - A5 G5 E5 D5 C5 - D5 - C5 - - - . . ";
static const char *tune_bass =
  "C3 - G3 - C3 - G3 - A2 - E3 - A2 - E3 - F2 - C3 - F2 - C3 - G2 - D3 - G2 - B2 - "
  "C3 - G3 - C3 - G3 - A2 - E3 - A2 - E3 - F2 - C3 - G2 - D3 - C3 - G3 - C3 - . - ";
static const char *tune_title =
  "A4 - C5 - E5 - A5 - G5 - E5 - D5 - E5 - C5 - - - . . . . F4 - A4 - C5 - F5 - E5 - C5 - B4 - G4 - A4 - - - . . . . ";

static void music_game(void) { mc_tune(0, tune_lead, 7, 26); mc_tune(1, tune_bass, 7, 40); }
static void music_title(void) { mc_tune(0, tune_title, 9, 30); mc_tune(1, tune_bass, 9, 34); }
static void music_off(void) { mc_tune_stop(0); mc_tune_stop(1); }

// ---------------------------------------------------------------- particles
typedef struct { int x, y, vx, vy, life; } particle_t;
static particle_t fx[N_FX];
static int fx_next;

static void spark(int x, int y, int speed) {     // x, y in screen pixels
  particle_t *p = &fx[fx_next]; fx_next = (fx_next + 1) % N_FX;
  int a = rnd(1024), s = speed / 2 + rnd(speed + 1);
  p->x = x << FP; p->y = y << FP;
  p->vx = (mc_cos(a) * s) >> 16; p->vy = (mc_sin(a) * s) >> 16;
  p->life = 14 + rnd(14);
}
static void fx_clear(void) { memset(fx, 0, sizeof fx); for (int i = 0; i < N_FX; i++) mc_sprite_hide(S_FX + i); }

static void update_fx(void) {
  for (int i = 0; i < N_FX; i++) {
    particle_t *p = &fx[i];
    if (p->life <= 0) { mc_sprite_hide(S_FX + i); continue; }
    p->life--; p->x += p->vx; p->y += p->vy;
    p->vx = p->vx * 14 / 16; p->vy = p->vy * 14 / 16;
    int z = 0x60 + p->life * 10;
    mc_sprite(S_FX + i, PX(p->x) - 4, PX(p->y) - 4, CELL_SPARK, 1, 1, PAL_FX, 4);
    mc_sprite_zoom(S_FX + i, z, z);
  }
}

// ---------------------------------------------------------------- the virus
static uint8_t grid[GRID][GRID];                 // strength 0-3
static int16_t vlist[F_CELLS];                   // infected cells (y * GRID + x)
static int16_t vindex[GRID * GRID];
static int vcount;

static void virus_tile(int x, int y) {
  int s = grid[y][x];
  if (!s) { mc_tile(2, x, y, 0); return; }
  uint32_t h = hash2(x * 7 + s, y * 13);
  mc_tile(2, x, y, MC_TILE((s - 1) * 2 + 1 + (int)((h >> 4) & 1), PAL_VIRUS, 0));
}
static bool in_field(int x, int y) { return x >= F_MIN && x <= F_MAX && y >= F_MIN && y <= F_MAX; }
static bool on_edge(int x, int y) { return x == F_MIN || x == F_MAX || y == F_MIN || y == F_MAX; }

static void virus_clear(void) {
  memset(grid, 0, sizeof grid); vcount = 0;
  mc_tilemap_fill(2, 0);
}
static void infect(int x, int y, int s) {
  if (!grid[y][x]) { vindex[y * GRID + x] = (int16_t)vcount; vlist[vcount++] = (int16_t)(y * GRID + x); }
  grid[y][x] = (uint8_t)s;
  virus_tile(x, y);
}
static void heal(int x, int y) {                 // removes an infected cell
  int i = vindex[y * GRID + x], last = vlist[--vcount];
  vlist[i] = (int16_t)last; vindex[last] = (int16_t)i;
  grid[y][x] = 0;
  virus_tile(x, y);
}

static const int8_t dir4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

// one growth attempt: a random infected cell spreads to a random free neighbour.
// Returns 1 if a cell was infected, 2 if it touched the fence (edge of the field).
static int virus_grow(void) {
  for (int tries = 0; tries < 12 && vcount; tries++) {
    int c = vlist[rnd(vcount)], x = c % GRID, y = c / GRID;
    const int8_t *d = dir4[rnd(4)];
    int nx = x + d[0], ny = y + d[1];
    if (!in_field(nx, ny) || grid[ny][nx]) continue;
    infect(nx, ny, 1);
    return on_edge(nx, ny) ? 2 : 1;
  }
  return 0;
}
static void virus_age(void) {                    // a random cell gets stronger
  if (!vcount) return;
  int c = vlist[rnd(vcount)], x = c % GRID, y = c / GRID;
  if (grid[y][x] < 3) { grid[y][x]++; virus_tile(x, y); }
}
static int virus_margin(void) {                  // distance of the closest cell to the fence
  int m = 99;
  for (int i = 0; i < vcount; i++) {
    int x = vlist[i] % GRID, y = vlist[i] / GRID;
    int d = x - F_MIN; if (F_MAX - x < d) d = F_MAX - x;
    if (y - F_MIN < d) d = y - F_MIN;
    if (F_MAX - y < d) d = F_MAX - y;
    if (d < m) m = d;
  }
  return m;
}

static void virus_seed(int level) {              // the colonies of a level
  virus_clear();
  int colonies = level < 2 ? 1 : level < 4 ? 2 : level < 6 ? 3 : 4;
  int size = 18 + level * 12;
  for (int k = 0; k < colonies; k++) {
    int cx, cy;
    if (colonies == 1) { cx = 15 + rnd(2); cy = 15 + rnd(2); }
    else { int a = k * 1024 / colonies + rnd(80) + (colonies == 2 ? 128 : 0); cx = 16 + (mc_cos(a) * 6 >> 16); cy = 16 + (mc_sin(a) * 6 >> 16); }
    infect(cx, cy, 3);
    int target = vcount + size / colonies;
    for (int guard = 0; vcount < target && guard < 4000; guard++) {   // grow this colony only
      int c = vlist[vcount - 1 - rnd(vcount < 12 ? vcount : 12 + (vcount - 12) / 2)];
      int x = c % GRID, y = c / GRID;
      const int8_t *d = dir4[rnd(4)];
      int nx = x + d[0], ny = y + d[1];
      int dist = abs(nx - 16) > abs(ny - 16) ? abs(nx - 16) : abs(ny - 16);
      if (!in_field(nx, ny) || grid[ny][nx] || dist > 8) continue;
      int dc = abs(nx - cx) + abs(ny - cy);
      infect(nx, ny, dc < 2 ? 3 : dc < 4 + level / 2 ? 2 : 1);
    }
  }
}

// ---------------------------------------------------------------- players and puck
typedef struct {
  bool human;
  int pos, vel;                                  // paddle start along its axis, 8.8
  int prev_paddle, analog;                       // analog paddle (0-255) once it has moved
  int score, flash;
  int ai_err, ai_speed;
} seat_t;
static seat_t seat[4];                           // 0 left, 1 top, 2 bottom, 3 right = J1..J4
static int bx, by, bvx, bvy, bspeed, owner;      // puck: centre and velocity in 8.8, last paddle
static int serving, serve_timer;                 // seat holding the puck, -1 = in play
static int level, pucks, team_score, best;
static int grow_timer, grow_period, alarm, paused;

static bool vertical(int s) { return s == 0 || s == 3; }
static int pad_fixed(int s) { return s == 0 || s == 1 ? PAD_IN : AW - PAD_IN - PAD_T; }   // cross-axis start

static void seat_reset(int s) { seat[s].pos = (AW / 2 - PAD_LEN / 2) << FP; seat[s].vel = 0; seat[s].ai_err = 0; }

static void control_human(int s) {
  int p = s + 1;
  uint32_t pad = mc_pad(p);
  bool minus = vertical(s) ? (pad & IN_UP) : (pad & IN_LEFT);
  bool plus  = vertical(s) ? (pad & IN_DOWN) : (pad & IN_RIGHT);
  seat_t *st = &seat[s];
  if (minus || plus) {                           // digital: quick ramp up to 4.5 px per frame
    int dir = plus ? 1 : -1;
    if ((st->vel > 0) != (dir > 0) || st->vel == 0) st->vel = dir * 384;
    st->vel += dir * 96;
    st->vel = clampi(st->vel, -1152, 1152);
    st->analog = 0;
  } else st->vel = 0;
  int stick = vertical(s) ? mc_stick_y(p, 0) : mc_stick_x(p, 0);
  if (stick > 24 || stick < -24) { st->vel = stick * 9; st->analog = 0; }
  st->pos += st->vel;
  int spin = mc_spinner_delta(p);                // spinner: relative
  if (spin) { st->pos += spin * 512; st->analog = 0; }
  int pd = mc_paddle(p);                         // paddle: absolute, used once it moves
  if (abs(pd - st->prev_paddle) > 3) { if (st->prev_paddle >= 0) st->analog = 1; st->prev_paddle = pd; }
  if (st->analog) st->pos = PAD_MIN + (PAD_MAX - PAD_MIN) * pd / 255;
}

static void control_ai(int s) {
  seat_t *st = &seat[s];
  int along = vertical(s) ? by : bx;
  int toward = s == 0 ? bvx < 0 : s == 3 ? bvx > 0 : s == 1 ? bvy < 0 : bvy > 0;
  int target;
  if (serving >= 0) target = (AW / 2) << FP;
  else if (toward) target = along + st->ai_err;
  else target = ((AW / 2) << FP) + (along - ((AW / 2) << FP)) / 2;
  int centre = st->pos + ((PAD_LEN / 2) << FP);
  int d = target - centre;
  int v = clampi(d / 2, -st->ai_speed, st->ai_speed);
  st->pos += v;
}

static void place_paddles(void) {
  for (int s = 0; s < 4; s++) {
    seat[s].pos = clampi(seat[s].pos, PAD_MIN, PAD_MAX);
    int a = PX(seat[s].pos), f = pad_fixed(s) - 1;                  // sprite has a 1-pixel border
    int pal = PAL_PLAYER + s;
    if (seat[s].flash > 0) { seat[s].flash--; if (seat[s].flash & 4) pal = PAL_NEUTRAL; }
    if (vertical(s)) mc_sprite(S_PAD + s, AX + f, AY + a, CELL_PADV, 1, 5, pal, 4);
    else mc_sprite(S_PAD + s, AX + a, AY + f, CELL_PADH, 5, 1, pal, 4);
    mc_sprite_zoom(S_PAD + s, 0x100, 0x100);
  }
}

static void hud(uint32_t t);

static void serve_from(int s) {
  serving = s; serve_timer = seat[s].human ? 300 : 50;
  owner = s; bvx = bvy = 0;
}
static void hold_puck(void) {                    // the puck follows the serving paddle
  int s = serving;
  int a = seat[s].pos + ((PAD_LEN / 2) << FP);
  int f = s == 0 || s == 1 ? (PAD_IN + PAD_T + BALL_R + 1) << FP : (AW - PAD_IN - PAD_T - BALL_R - 1) << FP;
  if (vertical(s)) { bx = f; by = a; } else { bx = a; by = f; }
}
static void launch(int s, int angle) {           // angle: 1024 steps, 0 = straight inwards
  int c = (mc_cos(angle) * bspeed) >> 16, sd = (mc_sin(angle) * bspeed) >> 16;
  switch (s) {
    case 0: bvx = c;  bvy = sd; break;
    case 3: bvx = -c; bvy = sd; break;
    case 1: bvy = c;  bvx = sd; break;
    default: bvy = -c; bvx = sd; break;
  }
}

static bool box_hit(int x0, int y0, int w, int h) {   // puck box against a rectangle (pixels)
  int px = PX(bx) - BALL_R, py = PX(by) - BALL_R;
  return px < x0 + w && x0 < px + 2 * BALL_R && py < y0 + h && y0 < py + 2 * BALL_R;
}
static bool hits_post(void) {
  return box_hit(0, 0, POST, POST) || box_hit(AW - POST, 0, POST, POST) ||
         box_hit(0, AW - POST, POST, POST) || box_hit(AW - POST, AW - POST, POST, POST);
}

static void score_points(int s, int pts) { if (s >= 0) seat[s].score += pts; team_score += pts; }

// virus cells under the puck box: young cells (strength 1) are broken on the way, older ones
// lose one level of strength and make the puck bounce (returns true for a bounce)
static bool hits_virus(void) {
  int px = PX(bx), py = PX(by);
  int x0 = (px - BALL_R) >> 3, x1 = (px + BALL_R - 1) >> 3, y0 = (py - BALL_R) >> 3, y1 = (py + BALL_R - 1) >> 3;
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      if (!in_field(x, y) || !grid[y][x]) continue;
      int s = grid[y][x];
      int sx = AX + x * 8 + 4, sy = AY + y * 8 + 4;
      if (s == 1) {
        heal(x, y);
        for (int k = 0; k < 7; k++) spark(sx, sy, 500);
        sfx_break = 140; score_points(owner, 50 * level);
        continue;                                // no bounce: the puck goes on
      }
      grid[y][x] = (uint8_t)(s - 1); virus_tile(x, y);
      for (int k = 0; k < 3; k++) spark(sx, sy, 300);
      sfx_hit = 8; sfx_hit_f = s == 3 ? 330 : 440; score_points(owner, 10);
      return true;
    }
  return false;
}

static void paddle_bounce(int s) {
  seat_t *st = &seat[s];
  int centre = st->pos + ((PAD_LEN / 2) << FP);
  int along = vertical(s) ? by : bx;
  int off = clampi((along - centre) * 256 / ((PAD_LEN / 2 + BALL_R) << FP), -256, 256);
  bspeed += 10; if (bspeed > 1152) bspeed = 1152;
  launch(s, off * 170 / 256);                    // up to 60 degrees off the normal
  int f = s == 0 || s == 1 ? (PAD_IN + PAD_T + BALL_R) << FP : (AW - PAD_IN - PAD_T - BALL_R) << FP;
  if (vertical(s)) bx = f; else by = f;
  owner = s;
  for (int k = 0; k < 4; k++) seat[k].ai_err = (rnd(2 * AI_ERR + 1) - AI_ERR) << FP;   // console players aim roughly
  sfx_pad = 6; sfx_pad_f = 520 + s * 130;
  score_points(s, 1);
}

static bool paddle_contact(int s) {
  int toward = s == 0 ? bvx < 0 : s == 3 ? bvx > 0 : s == 1 ? bvy < 0 : bvy > 0;
  if (!toward) return false;
  int a = PX(seat[s].pos), f = pad_fixed(s);
  return vertical(s) ? box_hit(f, a, PAD_T, PAD_LEN) : box_hit(a, f, PAD_LEN, PAD_T);
}

// moves the puck one frame; returns the seat it left the arena by, or -1
static int move_puck(void) {
  int steps = (abs(bvx) > abs(bvy) ? abs(bvx) : abs(bvy)) / 192 + 1;
  for (int i = 0; i < steps; i++) {
    int dx = bvx / steps, dy = bvy / steps;
    bx += dx;
    if (hits_post()) { bx -= dx; bvx = -bvx; sfx_pad = 3; sfx_pad_f = 260; }
    else if (hits_virus()) { bx -= dx; bvx = -bvx; }
    by += dy;
    if (hits_post()) { by -= dy; bvy = -bvy; sfx_pad = 3; sfx_pad_f = 260; }
    else if (hits_virus()) { by -= dy; bvy = -bvy; }
    for (int s = 0; s < 4; s++) if (paddle_contact(s)) { paddle_bounce(s); break; }
    int px = PX(bx), py = PX(by);
    if (px < -6) return 0;
    if (px > AW + 6) return 3;
    if (py < -6) return 1;
    if (py > AW + 6) return 2;
  }
  return -1;
}

static void draw_puck(uint32_t t) {
  int pal = owner >= 0 ? PAL_PLAYER + owner : PAL_NEUTRAL;
  mc_sprite(S_BALL, AX + PX(bx) - 4, AY + PX(by) - 4, CELL_BALL, 1, 1, pal, 5);
  mc_sprite_zoom(S_BALL, 0x100, 0x100);
  if (serving >= 0 && (t & 16)) {                // pulsing ring around a puck waiting to be served
    mc_sprite(S_RING, AX + PX(bx) - 8, AY + PX(by) - 8, CELL_RING, 2, 2, PAL_PLAYER + serving, 5);
    mc_sprite_zoom(S_RING, 0x100, 0x100);
  } else mc_sprite_hide(S_RING);
}

// ---------------------------------------------------------------- HUD
static void hud_seat(int s, int col, int row, uint32_t t) {
  char line[16];
  snprintf(line, sizeof line, "J%d %s", s + 1, seat[s].human ? "   " : "IA "); text(col, row, line, PAL_HUD + s);
  snprintf(line, sizeof line, "%06d", seat[s].score % 1000000); text(col, row + 1, line, PAL_HUD + s);
  text(col, row + 2, seat[s].human ? "      " : (t & 32) ? "START " : "      ", PAL_HUD + 6);
}

static void hud(uint32_t t) {
  char line[16];
  hud_seat(1, 0, 1, t);                          // top player: top left
  hud_seat(0, 0, 13, t);                         // left player: middle left
  hud_seat(3, 39, 13, t);                        // right player: middle right
  hud_seat(2, 39, 29, t);                        // bottom player: bottom right
  text(39, 1, "VIRUS", PAL_HUD + 4);
  snprintf(line, sizeof line, "%3d %%", vcount * 100 / F_CELLS); text(39, 2, line, PAL_HUD + 4);
  text(39, 4, "EQUIPE", PAL_HUD + 5);
  snprintf(line, sizeof line, "%06d", team_score % 1000000); text(39, 5, line, PAL_HUD + 5);
  text(39, 7, alarm && (t & 16) ? "ALERTE" : "      ", PAL_HUD);
  text(0, 25, "NIVEAU", PAL_HUD + 6);
  snprintf(line, sizeof line, "  %02d", level); text(0, 26, line, PAL_HUD + 5);
  text(0, 28, "PALETS", PAL_HUD + 6);
  for (int i = 0; i < 6; i++) text(i, 29, i < pucks ? "*" : " ", PAL_HUD + 5);
  text(0, 31, "RECORD", PAL_HUD + 6);
  snprintf(line, sizeof line, "%06d", best % 1000000); text(0, 32, line, PAL_HUD + 6);
}

// ---------------------------------------------------------------- record (NVRAM)
static void save_best(void) {
  memcpy((void *)MC_NVRAM, "VRMD", 4);
  for (int i = 0; i < 4; i++) MC_NVRAM[4 + i] = (uint8_t)(best >> (8 * i));
}
static void load_best(void) {
  best = 2000;
  if (!memcmp((const void *)MC_NVRAM, "VRMD", 4))
    best = MC_NVRAM[4] | (MC_NVRAM[5] << 8) | (MC_NVRAM[6] << 16) | (MC_NVRAM[7] << 24);
}

// ---------------------------------------------------------------- fence colours
static void fence_colours(uint32_t t) {
  if (alarm && (t & 8)) { set_rgb(PAL_BG, TAPE_A, 255, 40, 40); set_rgb(PAL_BG, TAPE_B, 255, 240, 60); }
  else { set_rgb(PAL_BG, TAPE_A, 230, 60, 40); set_rgb(PAL_BG, TAPE_B, 240, 225, 200); }
}

// ---------------------------------------------------------------- screens
static int demo;                                 // attract mode: four console players
static int demo_exit;                            // player who pressed START during the demo

static void hide_play_sprites(void) {
  mc_sprite_hide(S_BALL); mc_sprite_hide(S_RING);
  for (int s = 0; s < 4; s++) mc_sprite_hide(S_PAD + s);
}

static int start_pressed(void) {                 // 1-4, or 0
  for (int p = 1; p <= 4; p++) if (mc_pad_pressed(p) & IN_START) return p;
  return 0;
}

// ---------------------------------------------------------------- title picture (layer 1, 8 bpp)
#define PAL_TITLE 64                             // 8 bpp window: colours 1024-1279

static void title_colours(int k) {               // the picture faded in: k = 0..256
  for (int i = 1; i < 256; i++) {
    int c = title_pal[2 * i] | title_pal[2 * i + 1] << 8;
    int r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
    PALETTE[PAL_TITLE * 16 + i] = (uint16_t)(((r * k >> 8) << 10) | ((g * k >> 8) << 5) | (b * k >> 8));
  }
}
static void show_title_picture(void) {
  title_colours(0);
  mc_layer(1, title_chr, MC_TILE16 | MC_8BPP, 32, 32, 0);
  for (int y = 0; y < (int)title_height; y++)
    for (int x = 0; x < (int)title_width; x++) {
      int v = title_map[2 * (y * title_width + x)] | title_map[2 * (y * title_width + x) + 1] << 8;
      mc_tile(1, x, y, MC_TILE(v, PAL_TITLE, 0));
    }
  mc_layer_scroll(1, ((int)title_width * 16 - SCR_W) / 2, ((int)title_height * 16 - SCR_H) / 2);
  mc_display(DISP_ON | DISP_LAYER(0) | DISP_LAYER(1) | DISP_LAYER(3) | DISP_SPRITES);   // no virus layer
}
static void show_meadow(void) {
  mc_layer(1, CHR(CHR_BG), MC_TILE8 | MC_4BPP, 64, 64, 0);
  mc_tilemap_load(1, meadow_map, 64 * 64);
  mc_layer_scroll(1, (-AX) & 511, (-AY) & 511);
  mc_display(DISP_ON | DISP_LAYER(0) | DISP_LAYER(1) | DISP_LAYER(2) | DISP_LAYER(3) | DISP_SPRITES);
}

static int title_screen(void) {                  // player who pressed START (1-4), 0 = demo
  text_clear(); panel_clear(); hide_play_sprites(); fx_clear();
  alarm = 0;
  show_title_picture();
  music_title();
  char line[48];
  int p = 0;
  for (uint32_t t = 0; t < 1500 && !p; t++) {
    mc_frame();
    title_colours(t < 64 ? (int)t * 4 : 256);    // fade in over one second
    if (t == 64) {
      panel(7, 29, 37, 32);
      snprintf(line, sizeof line, "RECORD %06d", best); text_center(31, line, PAL_HUD + 5);
    }
    if (t >= 64) text_center(30, (t / 30) % 2 ? "                           " : "J1 A J4 : APPUYEZ SUR START", 0);
    p = start_pressed();
  }
  for (int k = 256; k > 0; k -= 32) { mc_frame(); title_colours(k); }   // quick fade out
  panel_clear(); text_clear();
  show_meadow();
  return p;
}

static void lobby(int first) {                   // a few seconds for the others to join
  for (int s = 0; s < 4; s++) { seat[s].human = (s + 1 == first); seat[s].score = 0; }
  text_clear(); panel_clear(); virus_clear();
  panel(4, 3, 40, 31);
  int countdown = 300;
  char line[48];
  text_center(5, "V I R U S   M E A D O W", PAL_HUD + 4);
  text_center(7, "QUI JOUE ?  START POUR REJOINDRE", 0);
  text_center(24, "J1 GAUCHE, J4 DROITE : HAUT / BAS", 0);
  text_center(25, "J2 HAUT, J3 BAS : GAUCHE / DROITE", 0);
  text_center(26, "BOUTON 1 : SERVIR    START : PAUSE", 0);
  text_center(28, "LES PLACES LIBRES SONT JOUEES", PAL_HUD + 6);
  text_center(29, "PAR LA CONSOLE", PAL_HUD + 6);
  for (uint32_t t = 0; countdown > 0; t++) {
    mc_frame();
    virus_colours(t); fence_colours(t);
    countdown--;
    for (int s = 0; s < 4; s++) {
      if (mc_pad_pressed(s + 1) & IN_START) {
        if (!seat[s].human) { seat[s].human = true; seat[s].flash = 30; if (countdown < 150) countdown = 150; sfx_pad = 8; sfx_pad_f = 660 + s * 110; }
        else if (countdown > 30) countdown = 30;   // START again: go now
      }
      snprintf(line, sizeof line, "J%d   %-6s   %-7s", s + 1, seat_name[s], seat[s].human ? "PRET !" : "CONSOLE");
      text(11, 10 + s * 3, line, PAL_HUD + s);
    }
    snprintf(line, sizeof line, "DEPART DANS %d", countdown / 60 + 1); text_center(22, line, PAL_HUD + 5);
    sfx_update();
  }
  text_clear(); panel_clear();
}

static void banner(const char *a, const char *b, int pal, int frames) {
  int w = (int)strlen(a); if (b && (int)strlen(b) > w) w = (int)strlen(b);
  int c0 = (45 - w) / 2 - 2;
  panel(c0, 14, c0 + w + 3, b ? 18 : 16);
  text_center(15, a, pal);
  if (b) text_center(17, b, 0);
  for (int t = 0; t < frames; t++) {
    mc_frame(); virus_colours(mc_frames); fence_colours(mc_frames); update_fx(); sfx_update(); hud(mc_frames);
    if (demo && (demo_exit = start_pressed())) break;
  }
  panel_clear();
  for (int r = 15; r <= 17; r++) text(6, r, "                                 ", 0);
}

static void start_level(void) {
  virus_seed(level);
  grow_period = clampi(96 - level * 8, 20, 96);
  grow_timer = grow_period * 3;                  // a short respite at the start of a level
  bspeed = 576 + level * 24;
  for (int s = 0; s < 4; s++) {
    seat_reset(s);
    seat[s].ai_speed = 768 + level * 16;         // console players: 3 px per frame and up
  }
  int s = rnd(4);
  for (int k = 0; k < 4; k++) if (seat[(s + k) % 4].human) { s = (s + k) % 4; break; }
  serve_from(s);
}

static void check_joins(void) {
  if (demo) { demo_exit = start_pressed(); return; }
  for (int s = 0; s < 4; s++) {
    if (!(mc_pad_pressed(s + 1) & IN_START)) continue;
    if (!seat[s].human) {
      seat[s].human = true; seat[s].flash = 40; sfx_pad = 8; sfx_pad_f = 660 + s * 110;
      if (serving == s) serve_timer = 300;
    } else {
      paused = !paused;
      if (paused) panel(18, 15, 26, 17); else { panel_clear(); text_center(16, "     ", 0); }
    }
  }
}

// one frame of play: 0 = continue, 1 = level cleared, 2 = lost (fence), 3 = lost (no puck), 4 = demo ended
static int play_frame(uint32_t t) {
  check_joins();
  if (demo_exit) return 4;
  if (paused) { text_center(16, (t & 32) ? "PAUSE" : "     ", PAL_HUD + 5); return 0; }
  for (int s = 0; s < 4; s++) if (seat[s].human) control_human(s); else control_ai(s);
  place_paddles();

  if (serving >= 0) {
    hold_puck();
    int s = serving;
    bool go = seat[s].human ? (mc_pad_pressed(s + 1) & (IN_B(1) | IN_B(2))) != 0 : false;
    if (--serve_timer <= 0) go = true;
    if (go) {
      launch(s, rnd(140) - 70 + clampi(seat[s].vel / 8, -60, 60));
      serving = -1; sfx_pad = 6; sfx_pad_f = 880;
    }
  } else {
    int out = move_puck();
    if (out >= 0) {                              // the puck left the field
      pucks--; seat[out].flash = 60; sfx_miss = 50;
      printf("t=%u puck lost by J%d, %d left, virus %d\n", (unsigned)t, out + 1, pucks, vcount);
      for (int k = 0; k < 6; k++) if (virus_grow() == 2) return 2;   // growth surge
      if (pucks <= 0) return 3;
      bspeed = 576 + level * 24;
      serve_from(out);
      mc_sprite_hide(S_BALL); mc_sprite_hide(S_RING);
      banner("PALET PERDU !", NULL, PAL_HUD + out, 50);
      if (demo_exit) return 4;
    }
  }
  if (!vcount) return 1;

  if (--grow_timer <= 0) {                       // the virus grows
    grow_timer = grow_period;
    int r = virus_grow();
    if (r == 2) return 2;
    if (r) sfx_grow = 6;
    if (rnd(4) == 0) virus_age();
  }
  alarm = virus_margin() <= 2;
  if (alarm && !sfx_alarm && (t & 63) == 0) sfx_alarm = 32;
  draw_puck(t);
  if (demo && (t & 32)) text_center(33, "DEMONSTRATION - START POUR JOUER", PAL_HUD + 5);
  else if (demo) text_center(33, "                                ", 0);
  if (t % 1800 == 0) printf("t=%u level %d virus %d pucks %d score %d\n", (unsigned)t, level, vcount, pucks, team_score);
  return 0;
}

static int game(void) {                          // 2 (fence), 3 (no puck) or 4 (demo ended)
  team_score = 0; level = 1; pucks = 3; paused = 0; demo_exit = 0;
  text_clear(); panel_clear(); music_game();
  for (;;) {
    start_level();
    char line[32];
    snprintf(line, sizeof line, "NIVEAU %d", level);
    place_paddles(); hold_puck(); draw_puck(0); hud(0);
    banner(line, level == 1 ? "CONTENEZ LE VIRUS !" : "DE NOUVELLES COLONIES...", PAL_HUD + 5, 120);
    if (demo_exit) return 4;
    int r;
    uint32_t t;
    for (t = 1;; t++) {
      mc_frame();
      virus_colours(mc_frames); fence_colours(mc_frames);
      r = play_frame(t);
      update_fx(); sfx_update(); hud(t);
      if (!demo && team_score > best) best = team_score;
      if (r) break;
    }
    printf("level %d ends: %d after %u frames, pucks %d, score %d\n", level, r, (unsigned)t, pucks, team_score);
    if (r != 1) return r;
    int bonus = 500 * level + 100 * pucks;
    team_score += bonus;
    if (!demo && team_score > best) best = team_score;
    snprintf(line, sizeof line, "BONUS %d", bonus);
    mc_sprite_hide(S_BALL); mc_sprite_hide(S_RING);
    banner("PRAIRIE SAUVEE !", line, PAL_HUD + 3, 150);
    if (demo_exit) return 4;
    level++;
    if (pucks < 6) pucks++;
  }
}

static void game_over(int why) {
  sfx_silence(); music_off();
  alarm = why == 2;
  mc_sprite_hide(S_BALL); mc_sprite_hide(S_RING);
  if (why == 4) { alarm = 0; return; }
  if (why == 2)                                  // the virus floods the meadow
    for (int t = 0; t < 90; t++) {
      mc_frame(); virus_colours(mc_frames); fence_colours(mc_frames);
      for (int k = 0; k < 4; k++) virus_grow();
      if (t % 4 == 0) sfx_break = 120;
      update_fx(); sfx_update(); hud(mc_frames);
    }
  int mvp = 0;
  for (int s = 1; s < 4; s++) if (seat[s].score > seat[mvp].score) mvp = s;
  bool record = !demo && team_score >= best && team_score > 0;
  if (record) save_best();
  char line[48];
  panel(8, 10, 36, 21);
  text_center(12, why == 2 ? "LA PRAIRIE EST CONTAMINEE" : "PLUS DE PALET", PAL_HUD + 4);
  snprintf(line, sizeof line, "SCORE D'EQUIPE %06d", team_score); text_center(15, line, PAL_HUD + 5);
  snprintf(line, sizeof line, "MEILLEUR JOUEUR : J%d", mvp + 1); text_center(17, line, PAL_HUD + mvp);
  if (record) text_center(19, "NOUVEAU RECORD !", PAL_HUD + 6);
  for (int t = 0; t < (demo ? 240 : 600); t++) {
    mc_frame(); virus_colours(mc_frames); fence_colours(mc_frames);
    update_fx(); sfx_update();
    int p = start_pressed();
    if (p && (demo || t > 90)) { if (demo) demo_exit = p; break; }
  }
  alarm = 0;
  panel_clear();
}

int main(void) {
  mc_init();
  mc_video_mode(SCR_W, SCR_H);
  mc_text_layer(0, 3);
  make_palettes();
  mc_background(BG_ENTRY);
  text_center(16, "LA PRAIRIE POUSSE...", PAL_HUD + 3);
  mc_display(DISP_ON | DISP_LAYER(0));
  make_sprites();
  make_virus_tiles();
  make_panel_tiles();
  build_meadow();
  mc_layer_scroll(2, (-AX) & 511, (-AY) & 511);
  mc_sprites_hide_all();
  mc_sprites_used(S_USED);
  for (int s = 0; s < 4; s++) seat[s].prev_paddle = -1;
  mc_srand(0x5EED1234u ^ (uint32_t)mc_cycles());
  load_best();
  show_meadow();
  int first = 0;
  for (;;) {
    if (!first) first = title_screen();
    if (first) {                                 // a real game
      demo = 0; lobby(first); first = 0;
      game_over(game());
    } else {                                     // nobody pressed START: demonstration
      demo = 1;
      for (int s = 0; s < 4; s++) { seat[s].human = false; seat[s].score = 0; }
      game_over(game());
      first = demo_exit; demo = 0; demo_exit = 0;
      text_clear();
    }
  }
}
