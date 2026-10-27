// MONOTRACKER: a music tracker for MONOCORE-1, played with the keyboard.
//
// 16 tracks: 8 on the PSG channels, 8 on the PCM voices (sample bank mtbank.bin, made by
// tools/mtrack/make_bank.py). The music is played by the SDK module mtrack (sdk/mtrack.h),
// the same one the games use. Songs are kept in the NVRAM (8 slots) and exported for the games
// with tools/mtrack/sav2song.py. Memory: the 24 patterns live in the CHR RAM (4 KB each), the
// font and the graphs in its last 32 KB.
#include <stdio.h>
#include <string.h>
#include "mc.h"
#include "mtrack.h"
#include "font640.h"            // the font, rendered by make_font.py

#define COLS 80                // 640 x 240: 80 x 30 cells of 8 x 8 (pixels twice as tall as wide)
#define LINES 30
#define NPAT 24
#define PAT_CELLS (MT_ROWS * MT_TRACKS)
#define PATS ((uint32_t *)CHRRAM_BASE)
#define FONT_OFF 0x18000u
#define G_SOLID 96            // glyphs after the 96 of the font
#define G_VU 97               // 97-104: VU bar of 1 to 8 pixels
#define G_ARROW 105           // small play arrow
#define G_UP 106              // arrows of the spin buttons
#define G_DOWN 107
#define G_LEFT 108
#define G_RIGHT 109
#define G_BEV 110             // 110-118: bevelled panel, 3 x 3 pieces
#define G_PTR 120             // 120-123: mouse pointer, 16 x 16
#define G_GRAPH 128           // two graphs of GW x GH tiles
#define GW 24
#define GH 4
#define G_SCOPE 400           // 32 x 8 tiles of the track meters (two columns of tiles a track)

#ifndef KEY_COMMA
#define KEY_COMMA 0x41
#define KEY_PERIOD 0x49
#define KEY_SLASH 0x4A
#define KEY_SEMICOLON 0x4C
#define KEY_MINUS 0x4E
#define KEY_EQUALS 0x55
#define KEY_KPPLUS 0x79
#define KEY_KPMINUS 0x7B
#endif

// colours: text on the dark pattern 0-7, row bars 8-11, graphs 12, pointer 13, panels 16-19,
// text on the grey panels 20-24, track meters 25
enum { C_WHITE, C_GREY, C_CYAN, C_GREEN, C_ORANGE, C_YELLOW, C_RED, C_BLUE };
enum { B_CURSOR = 8, B_EDIT, B_FIELD, B_BEAT };
#define P_GRAPH 12
#define P_POINTER 13
enum { P_RAISED = 16, P_SUNK, P_PRESSED, P_SCOPE, T_INK, T_BLUE, T_WHITE, T_RED, T_DIM, P_BARS };

// ================================================================ song being edited
static mt_instrument_t ins[MT_INSTRUMENTS];
static uint8_t orders[MT_ORDERS];
static int n_orders = 1, restart, s_speed = 6, s_bpm = 125, s_master = 48;
static char s_name[17];
static int song_slot = -1, dirty;

static uint32_t *pat(int p) { return PATS + p * PAT_CELLS; }
static const uint32_t *src_pattern(void *ctx, int p) { (void)ctx; return p < NPAT ? pat(p) : 0; }

static uint8_t vu[MT_TRACKS];              // track meters (smoothed levels)

// ================================================================ screen
// Three tile layers of 48 x 30 cells (8 x 8): layer 0 the text, layer 1 the row bars of the
// pattern, layer 2 the bevelled panels in the manner of the Amiga trackers. Layer 2 is scrolled
// up by 4 pixels: a panel around text rows r to r2 covers its rows r to r2 + 1, which leaves a
// margin of 4 pixels above and below the text. The mouse pointer is a sprite.
static uint16_t txt[LINES][COLS], txt_old[LINES][COLS];   // glyph | palette << 10
static uint16_t bgr[LINES][COLS], bgr_old[LINES][COLS];
static uint16_t pnl[LINES + 1][COLS], pnl_old[LINES + 1][COLS];
static char line[96];
#define GP(g, pal) ((uint16_t)((g) | ((pal) << 10)))

static void cls(void) {
  memset(txt, 0, sizeof txt); memset(bgr, 0, sizeof bgr); memset(pnl, 0, sizeof pnl);
}
static void putg(int x, int y, int g, int pal) {
  if (x >= 0 && x < COLS && y >= 0 && y < LINES) txt[y][x] = GP(g, pal);
}
static void print(int x, int y, const char *s, int pal) {
  for (; *s; s++, x++) {
    int c = (unsigned char)*s;
    if (c < 32 || c > 127) c = '?';
    putg(x, y, c - 32, pal);
  }
}
static void printn(int x, int y, const char *s, int n, int pal) {   // n characters, padded
  for (int i = 0; i < n; i++) {
    int c = *s ? (unsigned char)*s++ : ' ';
    putg(x + i, y, (c < 32 || c > 127 ? '?' : c) - 32, pal);
  }
}
static void printc(int x0, int x1, int y, const char *s, int pal) {   // centred in x0..x1
  int n = (int)strlen(s);
  print(x0 + (x1 - x0 + 1 - n) / 2, y, s, pal);
}
static void bar(int x0, int x1, int y, int pal) {
  for (int x = x0; x <= x1; x++) if (x >= 0 && x < COLS && y >= 0 && y < LINES) bgr[y][x] = GP(G_SOLID, pal);
}
// a bevelled panel around the text cells x0..x1, rows y0..y1
static void box(int x0, int y0, int x1, int y1, int pal) {
  for (int y = y0; y <= y1 + 1; y++)
    for (int x = x0; x <= x1; x++) {
      int r = y == y0 ? 0 : y == y1 + 1 ? 2 : 1, c = x == x0 ? 0 : x == x1 ? 2 : 1;
      if (y >= 0 && y <= LINES && x >= 0 && x < COLS) pnl[y][x] = GP(G_BEV + r * 3 + c, pal);
    }
}
static void flush(void) {
  volatile uint32_t *m0 = mc_tilemap(0), *m1 = mc_tilemap(1), *m2 = mc_tilemap(2);
  for (int y = 0; y < LINES; y++)
    for (int x = 0; x < COLS; x++) {
      uint16_t t = txt[y][x], b = bgr[y][x];
      if (t != txt_old[y][x]) { txt_old[y][x] = t; m0[y * 128 + x] = TILE(t & 0x3FF, t >> 10); }
      if (b != bgr_old[y][x]) { bgr_old[y][x] = b; m1[y * 128 + x] = TILE(b & 0x3FF, b >> 10); }
    }
  for (int y = 0; y <= LINES; y++)
    for (int x = 0; x < COLS; x++)
      if (pnl[y][x] != pnl_old[y][x]) { pnl_old[y][x] = pnl[y][x]; m2[y * 128 + x] = TILE(pnl[y][x] & 0x3FF, pnl[y][x] >> 10); }
}

// ---------------------------------------------------------------- clickable zones
// Each frame, drawing a control also records its zone; the mouse is tested against the zones of
// the previous frame, the last one drawn on top.
enum { Z_SPIN = 1, Z_BTN, Z_CELL, Z_TRACK, Z_SCOPE, Z_PARAM, Z_ORDER, Z_ORDPAT, Z_SETTING, Z_SLOT, Z_SONGBTN, Z_HELP, Z_ROWS };
typedef struct { uint8_t x0, y0, x1, y1, id; int16_t arg; } zone_t;
static zone_t zones[192], zones_prev[192];
static int nzones, nzones_prev;
static void zone(int x0, int y0, int x1, int y1, int id, int arg) {
  if (nzones < (int)(sizeof zones / sizeof zones[0]))
    zones[nzones++] = (zone_t){ (uint8_t)x0, (uint8_t)y0, (uint8_t)x1, (uint8_t)y1, (uint8_t)id, (int16_t)arg };
}
static const zone_t *zone_at(int x, int y) {
  for (int i = nzones_prev - 1; i >= 0; i--) {
    const zone_t *z = &zones_prev[i];
    if (x >= z->x0 && x <= z->x1 && y >= z->y0 && y <= z->y1) return z;
  }
  return 0;
}

static const uint16_t pal_fg[8] = {
  RGB555(240, 240, 240), RGB555(110, 116, 140), RGB555(90, 210, 240), RGB555(120, 230, 110),
  RGB555(250, 160, 60), RGB555(250, 225, 90), RGB555(250, 80, 80), RGB555(120, 140, 250) };
static const uint16_t pal_bg[4] = { RGB555(40, 60, 140), RGB555(110, 30, 40), RGB555(80, 110, 220), RGB555(26, 30, 54) };
#define FACE   RGB555(160, 164, 176)
#define LIGHT  RGB555(236, 236, 244)
#define SHADOW RGB555(64, 66, 82)
#define NAVY   RGB555(18, 22, 44)

static void tile_px(uint8_t *g, int x, int y, int c) { uint8_t *b = &g[y * 4 + (x >> 1)]; *b = (uint8_t)((x & 1) ? (*b & 0x0F) | (c << 4) : (*b & 0xF0) | c); }
static void upload_glyph(int n, const char *rows) {   // 8 rows of 8 characters: '.' 0, '1'-'3' colours
  static uint8_t g[32];
  memset(g, 0, 32);
  for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) { char c = rows[y * 8 + x]; if (c >= '1' && c <= '9') tile_px(g, x, y, c - '0'); }
  mc_chr_upload(FONT_OFF + n * 32, g, 32);
}
static void set_pal(int p, uint16_t c1, uint16_t c2, uint16_t c3) { PALETTE[p * 16 + 1] = c1; PALETTE[p * 16 + 2] = c2; PALETTE[p * 16 + 3] = c3; }
// the fainter edges of the font (indices 4, 5, 6): the ink blended with the colour behind the text
static void text_blend(int p, uint16_t ink, uint16_t bg) {
  static const int a[3] = { 62, 40, 20 };
  for (int k = 0; k < 3; k++) {
    int r = ((ink & 31) * a[k] + (bg & 31) * (100 - a[k])) / 100, g = (((ink >> 5) & 31) * a[k] + ((bg >> 5) & 31) * (100 - a[k])) / 100,
        b = (((ink >> 10) & 31) * a[k] + ((bg >> 10) & 31) * (100 - a[k])) / 100;
    PALETTE[p * 16 + 4 + k] = (uint16_t)(r | g << 5 | b << 10);
  }
}

static void video_init(void) {
  mc_video_mode(640, 240);                    // hardware version 5: 640 pixels wide, 240 lines
  mc_chr_upload(FONT_OFF, font640, sizeof font640);
  static uint8_t g[32];
  memset(g, 0x33, 32); mc_chr_upload(FONT_OFF + G_SOLID * 32, g, 32);
  for (int k = 1; k <= 8; k++) {
    memset(g, 0, 32);
    for (int y = 8 - k; y < 8; y++) for (int x = 1; x < 4; x++) g[y * 4 + x] = 0x11;
    mc_chr_upload(FONT_OFF + (G_VU + k - 1) * 32, g, 32);
  }
  upload_glyph(G_ARROW, "........" ".11....." ".1211..." ".122211." ".1222211" ".122211." ".1211..." ".11.....");
  upload_glyph(G_UP,    "........" "...1...." "..111..." ".11111.." "1111111." "........" "........" "........");
  upload_glyph(G_DOWN,  "........" "........" "1111111." ".11111.." "..111..." "...1...." "........" "........");
  upload_glyph(G_LEFT,  "....1..." "...11..." "..111..." ".1111..." "..111..." "...11..." "....1..." "........");
  upload_glyph(G_RIGHT, ".1......" ".11....." ".111...." ".1111..." ".111...." ".11....." ".1......" "........");
  // bevel: top-left light (1), bottom-right shadow (2), face (3)
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 3; c++) {
      memset(g, 0x33, 32);
      for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
          int v = 3;
          if (c == 0 && x == 0) v = 1;
          if (r == 0 && y == 0) v = 1;
          if (c == 2 && x == 7) v = (r == 0 && y == 0) ? 2 : 2;
          if (r == 2 && y == 7) v = (c == 0 && x == 0) ? 1 : 2;
          tile_px(g, x, y, v);
        }
      mc_chr_upload(FONT_OFF + (G_BEV + r * 3 + c) * 32, g, 32);
    }
  // mouse pointer, 16 x 16 (cells in sprite order): outline 2, body 1
  static const char *ptr =
    "1..............."
    "11.............."
    "121............."
    "1221............"
    "12221..........."
    "122221.........."
    "1222221........."
    "12222221........"
    "122222221......."
    "1222211111......"
    "12212221........"
    "1211.1221......."
    "11...1221......."
    ".....11221......"
    "......1221......"
    ".......11.......";
  for (int cy = 0; cy < 2; cy++)
    for (int cx = 0; cx < 2; cx++) {
      memset(g, 0, 32);
      for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
        char c = ptr[(cy * 8 + y) * 16 + cx * 8 + x];
        if (c != '.') tile_px(g, x, y, c == '1' ? 2 : 1);
      }
      mc_chr_upload(FONT_OFF + (G_PTR + cy * 2 + cx) * 32, g, 32);
    }
  for (int p = 0; p < 64; p++) for (int i = 0; i < 16; i++) PALETTE[p * 16 + i] = 0;
  for (int p = 0; p < 8; p++) { set_pal(p, pal_fg[p], RGB555(8, 8, 16), 0); text_blend(p, pal_fg[p], NAVY); }
  for (int p = 0; p < 4; p++) PALETTE[(8 + p) * 16 + 3] = pal_bg[p];
  set_pal(P_GRAPH, RGB555(250, 225, 90), RGB555(60, 64, 96), RGB555(110, 90, 40));
  set_pal(P_POINTER, RGB555(255, 255, 255), RGB555(10, 10, 20), 0);
  set_pal(P_RAISED, LIGHT, SHADOW, FACE);
  set_pal(P_SUNK, SHADOW, LIGHT, NAVY);
  set_pal(P_PRESSED, SHADOW, LIGHT, RGB555(104, 120, 172));
  set_pal(P_SCOPE, SHADOW, LIGHT, RGB555(4, 6, 12));
  set_pal(T_INK, RGB555(16, 16, 24), FACE, 0); text_blend(T_INK, RGB555(16, 16, 24), FACE);
  set_pal(T_BLUE, RGB555(20, 50, 160), FACE, 0); text_blend(T_BLUE, RGB555(20, 50, 160), FACE);
  set_pal(T_WHITE, RGB555(255, 255, 255), RGB555(50, 60, 100), 0); text_blend(T_WHITE, RGB555(255, 255, 255), RGB555(104, 120, 172));
  set_pal(T_RED, RGB555(170, 20, 20), FACE, 0); text_blend(T_RED, RGB555(170, 20, 20), FACE);
  set_pal(T_DIM, RGB555(92, 96, 110), FACE, 0); text_blend(T_DIM, RGB555(92, 96, 110), FACE);
  // scope bars: green, yellow, red, peak
  PALETTE[P_BARS * 16 + 1] = RGB555(60, 220, 90); PALETTE[P_BARS * 16 + 2] = RGB555(240, 220, 60);
  PALETTE[P_BARS * 16 + 3] = RGB555(250, 70, 50); PALETTE[P_BARS * 16 + 4] = RGB555(255, 255, 255);
  PALETTE[P_BARS * 16 + 5] = RGB555(30, 40, 70);
  PALETTE[15 * 16 + 1] = RGB555(12, 14, 26);
  mc_background(15 * 16 + 1);
  mc_layer(0, MC_CHR(FONT_OFF), MC_TILE8 | MC_4BPP, 128, 32, 3);   // 1024 x 256 pixels
  mc_layer(1, MC_CHR(FONT_OFF), MC_TILE8 | MC_4BPP, 128, 32, 1);   // 1024 x 256 pixels
  mc_layer(2, MC_CHR(FONT_OFF), MC_TILE8 | MC_4BPP, 128, 32, 0);   // 1024 x 256 pixels
  mc_layer_scroll(2, 0, 4);
  mc_tilemap_fill(0, 0); mc_tilemap_fill(1, 0); mc_tilemap_fill(2, 0);
  memset(txt_old, 0, sizeof txt_old); memset(bgr_old, 0, sizeof bgr_old); memset(pnl_old, 0, sizeof pnl_old);
  mc_sprite_sheet(MC_CHR(FONT_OFF));
  mc_display(DISP_ON | DISP_LAYER(0) | DISP_LAYER(1) | DISP_LAYER(2) | DISP_SPRITES);
}

// ---------------------------------------------------------------- scope: a bar per track
static uint8_t scope[32 * 8 * 32];          // 32 x 8 tiles, two columns of tiles per track
static uint8_t peak[MT_TRACKS], peak_hold[MT_TRACKS];
static void draw_scope(void) {
  memset(scope, 0, sizeof scope);
  for (int t = 0; t < MT_TRACKS; t++) {
    int h = vu[t] * 62 / 255;                 // 0-62 pixels
    if (h >= peak[t]) { peak[t] = (uint8_t)h; peak_hold[t] = 30; }
    else if (peak_hold[t]) peak_hold[t]--; else if (peak[t]) peak[t]--;
    for (int y = 0; y < 64; y++) {
      int fromb = 63 - y, c = 0;
      if (fromb < h) c = fromb > 46 ? 3 : fromb > 30 ? 2 : 1;
      else if (fromb == peak[t] && peak[t]) c = 4;
      else if (!(y & 3)) c = 5;               // faint graduations
      if (mt_muted(t) && c && c != 5) c = 5;
      if (!c) continue;
      uint8_t *b = &scope[((y >> 3) * 32 + 2 * t) * 32 + (y & 7) * 4], *b2 = b + 32;
      uint8_t cc = (uint8_t)(c | (c << 4));
      b[0] = (uint8_t)(c << 4); b[1] = cc; b[2] = cc; b[3] = cc;          // pixels 1 to 14 of the two tiles
      b2[0] = cc; b2[1] = cc; b2[2] = cc; b2[3] = (uint8_t)c;
    }
  }
  mc_chr_upload(FONT_OFF + G_SCOPE * 32, scope, sizeof scope);
}

// ================================================================ graphs (instrument page)
static uint8_t graph[2][GW * GH * 32];
static void gpix(int g, int x, int y, int c) {
  if (x < 0 || y < 0 || x >= GW * 8 || y >= GH * 8) return;
  uint8_t *b = &graph[g][((y >> 3) * GW + (x >> 3)) * 32 + (y & 7) * 4 + ((x & 7) >> 1)];
  if (x & 1) *b = (uint8_t)((*b & 0x0F) | (c << 4)); else *b = (uint8_t)((*b & 0xF0) | c);
}
static void gline(int g, int x0, int y0, int x1, int y1, int c) {
  int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
  for (;;) {
    gpix(g, x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * e;
    if (e2 >= dy) { e += dy; x0 += sx; }
    if (e2 <= dx) { e += dx; y0 += sy; }
  }
}
static void graph_frame(int g) {
  memset(graph[g], 0, sizeof graph[g]);
  for (int x = 0; x < GW * 8; x += 2) { gpix(g, x, 0, 2); gpix(g, x, GH * 8 - 1, 2); gpix(g, x, GH * 4, 2); }
  for (int y = 0; y < GH * 8; y += 2) { gpix(g, 0, y, 2); gpix(g, GW * 8 - 1, y, 2); }
}
static void draw_graphs(const mt_instrument_t *in) {
  const int W = GW * 8 - 4, H = GH * 8, top = 4, bot = H - 4;
  // envelope: attack, decay, sustain (held 16 ticks), release
  graph_frame(0);
  int a = in->attack, d = in->decay, r = in->release, hold = 16, T = a + d + hold + r;
  if (T < 1) T = 1;
  int ys = bot - (bot - top) * in->sustain / 64;
  int x1 = 2 + a * W / T, x2 = 2 + (a + d) * W / T, x3 = 2 + (a + d + hold) * W / T, x4 = 2 + T * W / T;
  int ytop = bot - (bot - top) * in->volume / 64;
  ys = bot - (bot - ys) * in->volume / 64;
  for (int x = x1; x <= x4; x += 3) gpix(0, x, bot, 3);
  gline(0, 2, bot, x1, ytop, 1); gline(0, x1, ytop, x2, ys, 1);
  gline(0, x2, ys, x3, ys, 1); gline(0, x3, ys, x4, bot, 1);
  for (int y = top; y < bot; y += 3) { gpix(0, x2, y, 3); gpix(0, x3, y, 3); }
  // wave: two periods of the PSG wave, or the PCM sample
  graph_frame(1);
  const int mid = H / 2, amp = H / 2 - 5;
  if (in->type == MT_TYPE_PSG) {
    uint32_t lfsr = 1;
    int py = mid;
    for (int x = 0; x < W; x++) {
      int ph = (x * 512 / W) & 255, v;     // 0-255 within a period
      switch (in->wave) {
        case PSG_SQUARE: v = ph < (in->duty ? in->duty : 128) ? amp : -amp; break;
        case PSG_TRIANGLE: v = ph < 128 ? -amp + ph * 2 * amp / 128 : amp - (ph - 128) * 2 * amp / 128; break;
        case PSG_SAW: v = -amp + ph * 2 * amp / 255; break;
        default: if (x % 3 == 0) { uint32_t bit = (lfsr ^ (lfsr >> 1)) & 1; lfsr = (lfsr >> 1) | (bit << 14); } v = (lfsr & 1) ? amp : -amp; break;
      }
      int y = mid - v;
      gline(1, x + 2, x ? py : y, x + 2, y, 1);
      py = y;
    }
  } else {
    const mt_sample_t *s = mt_bank_sample(in->wave);
    if (s) {
      const int16_t *d16 = (const int16_t *)((const uint8_t *)mtbank_file + s->offset);
      uint32_t n = s->length;
      for (int x = 0; x < W; x++) {
        uint32_t i0 = (uint64_t)n * x / W, i1 = (uint64_t)n * (x + 1) / W;
        int lo = 0, hi = 0;
        for (uint32_t i = i0; i < i1; i += 1 + (i1 - i0) / 64) { int v = d16[i]; if (v < lo) lo = v; if (v > hi) hi = v; }
        gline(1, x + 2, mid - hi * amp / 32768, x + 2, mid - lo * amp / 32768, 1);
      }
      if (s->loop < n && in->duty != 1) {
        int lx = 2 + (int)((uint64_t)s->loop * W / n);
        for (int y = 2; y < H - 2; y += 2) gpix(1, lx, y, 3);
      }
    }
  }
  mc_chr_upload(FONT_OFF + G_GRAPH * 32, graph[0], sizeof graph[0]);
  mc_chr_upload(FONT_OFF + (G_GRAPH + GW * GH) * 32, graph[1], sizeof graph[1]);
}
static void show_graph(int g, int x0, int y0) {
  for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) putg(x0 + x, y0 + y, G_GRAPH + g * GW * GH + y * GW + x, P_GRAPH);
}

// ================================================================ keyboard
static int layout;                     // 0 AZERTY, 1 QWERTY (kept in the NVRAM)
static int velo_off;                   // 1: the MIDI velocity does not write the volume (NVRAM)
static uint32_t kdown[16];             // 512 bits: keys down, from the events
static int8_t jam_track[512];          // track sounding for each piano key, -1 none
static int rep_key, rep_timer;

static const uint8_t letter_code[26] = { KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
  KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z };
static const uint8_t digit_code[10] = { KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9 };
static const uint8_t kp_code[10] = { KEY_KP0, KEY_KP1, KEY_KP2, KEY_KP3, KEY_KP4, KEY_KP5, KEY_KP6, KEY_KP7, KEY_KP8, KEY_KP9 };

static int key_char(int k) {           // character printed on the key, for the layout in use
  if (layout == 0) {                   // AZERTY: A/Q, Z/W and M move
    if (k == KEY_Q) return 'A';
    if (k == KEY_A) return 'Q';
    if (k == KEY_W) return 'Z';
    if (k == KEY_Z) return 'W';
    if (k == KEY_SEMICOLON) return 'M';
    if (k == KEY_M) return ',';
  }
  for (int i = 0; i < 26; i++) if (letter_code[i] == k) return 'A' + i;
  for (int i = 0; i < 10; i++) if (digit_code[i] == k || kp_code[i] == k) return '0' + i;
  if (k == KEY_SPACE) return ' ';
  if (k == KEY_KPMINUS || (layout == 1 && k == KEY_MINUS)) return '-';
  if (k == KEY_PERIOD && layout == 1) return '.';
  return 0;
}
static int key_hex(int k) {
  int c = key_char(k);
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
// piano: two octaves and a bit, by key position (the same on every layout)
static const int16_t piano[][2] = {
  { KEY_Z, 0 }, { KEY_S, 1 }, { KEY_X, 2 }, { KEY_D, 3 }, { KEY_C, 4 }, { KEY_V, 5 }, { KEY_G, 6 },
  { KEY_B, 7 }, { KEY_H, 8 }, { KEY_N, 9 }, { KEY_J, 10 }, { KEY_M, 11 }, { KEY_COMMA, 12 },
  { KEY_L, 13 }, { KEY_PERIOD, 14 }, { KEY_SEMICOLON, 15 }, { KEY_SLASH, 16 },
  { KEY_Q, 12 }, { KEY_2, 13 }, { KEY_W, 14 }, { KEY_3, 15 }, { KEY_E, 16 }, { KEY_R, 17 },
  { KEY_5, 18 }, { KEY_T, 19 }, { KEY_6, 20 }, { KEY_Y, 21 }, { KEY_7, 22 }, { KEY_U, 23 },
  { KEY_I, 24 }, { KEY_9, 25 }, { KEY_O, 26 }, { KEY_0, 27 }, { KEY_P, 28 } };
static int piano_key(int k) {
  for (unsigned i = 0; i < sizeof piano / sizeof piano[0]; i++) if (piano[i][0] == k) return piano[i][1];
  return -1;
}
static bool shift(void) { return mc_key(KEY_LSHIFT) || mc_key(KEY_RSHIFT); }
static bool ctrl(void) { return mc_key(KEY_LCTRL) || mc_key(KEY_RCTRL); }
static bool alt(void) { return mc_key(KEY_LALT) || mc_key(KEY_RALT); }
static bool repeatable(int k) {
  return k == KEY_UP || k == KEY_DOWN || k == KEY_LEFT || k == KEY_RIGHT || k == KEY_PGUP || k == KEY_PGDN ||
         k == KEY_DELETE || k == KEY_BACKSPACE || k == KEY_INSERT;
}

// ================================================================ editor state
enum { PG_PAT, PG_INS, PG_SONG, PG_HELP };
static int page;
static int cur_ord, cur_row, cur_track, cur_field, octave = 4, step = 1, edit_mode, follow = 1;
static int ins_psg = 1, ins_pcm = 10;          // instrument used by the notes typed on each kind of track
static int ins_sel = 1, ins_field;               // instrument page
static int song_pane, song_field, slot_sel, ord_scroll;
static int naming;                               // editing a name: 1 instrument, 2 song
static int play_mode;                            // 0 stopped, 1 song, 2 pattern
static int play_frames;                          // time of the song being played, in frames
static uint8_t one_order[1];
static char msg[COLS + 1];
static int msg_timer, confirm_key, confirm_timer;
static uint32_t clip_track[MT_ROWS], clip_pat[PAT_CELLS];
static int clip_track_ok, clip_pat_ok;
static mt_instrument_t clip_ins;
static int clip_ins_ok, graphs_dirty = 1;

static void message(const char *s) { snprintf(msg, sizeof msg, "%s", s); msg_timer = 200; }
static bool confirm(int key, const char *what) {   // the same shortcut twice within 3 s
  if (confirm_key == key && confirm_timer > 0) { confirm_key = 0; return true; }
  confirm_key = key; confirm_timer = 150;
  snprintf(line, sizeof line, "%s : ENCORE UNE FOIS POUR CONFIRMER", what);
  message(line);
  return false;
}
static int cur_pat(void) { return orders[cur_ord]; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int slen(const char *s, int max) { int n = 0; while (n < max && s[n]) n++; return n; }

// ================================================================ NVRAM: 8 songs, packed
// "MTNV", u8 layout, u8 last slot, 10 bytes free, then 8 x {u32 offset, u32 size}, songs from 80
#define NV MC_NVRAM
#define NV_SLOTS 8
#define NV_DATA 80
static uint32_t nv32(uint32_t a) { return NV[a] | (NV[a + 1] << 8) | (NV[a + 2] << 16) | ((uint32_t)NV[a + 3] << 24); }
static void nv32w(uint32_t a, uint32_t v) { for (int i = 0; i < 4; i++) NV[a + i] = (uint8_t)(v >> (8 * i)); }
static uint32_t slot_off(int k) { return nv32(16 + 8 * k); }
static uint32_t slot_size(int k) { return nv32(20 + 8 * k); }
static uint32_t align4(uint32_t v) { return (v + 3) & ~3u; }

static void nv_check(void) {
  if (NV[0] == 'M' && NV[1] == 'T' && NV[2] == 'N' && NV[3] == 'V') {
    for (int k = 0; k < NV_SLOTS; k++)
      if (slot_size(k) && (slot_off(k) + slot_size(k) > MC_NVRAM_SIZE ||
                           mt_song_check((const void *)(NVRAM_BASE + slot_off(k)), slot_size(k)))) { nv32w(16 + 8 * k, 0); nv32w(20 + 8 * k, 0); }
    layout = NV[4] & 1; velo_off = NV[6] & 1;
    return;
  }
  for (uint32_t i = 0; i < NV_DATA; i++) NV[i] = 0;
  NV[0] = 'M'; NV[1] = 'T'; NV[2] = 'N'; NV[3] = 'V'; NV[5] = 0xFF;
}
static uint32_t nv_end(void) {
  uint32_t e = NV_DATA;
  for (int k = 0; k < NV_SLOTS; k++) if (slot_size(k)) { uint32_t f = slot_off(k) + align4(slot_size(k)); if (f > e) e = f; }
  return e;
}
static void nv_delete(int k) {
  uint32_t off = slot_off(k), size = align4(slot_size(k)), end = nv_end();
  if (!slot_size(k)) return;
  for (uint32_t i = off; i + size < end; i++) NV[i] = NV[i + size];
  for (int j = 0; j < NV_SLOTS; j++) if (slot_size(j) && slot_off(j) > off) nv32w(16 + 8 * j, slot_off(j) - size);
  nv32w(16 + 8 * k, 0); nv32w(20 + 8 * k, 0);
}
static uint32_t nv_free(int replacing) {
  return MC_NVRAM_SIZE - nv_end() + (replacing >= 0 ? align4(slot_size(replacing)) : 0);
}

// ================================================================ songs: compact format
static uint8_t songbuf[MC_NVRAM_SIZE];

static bool ins_empty(const mt_instrument_t *in) {
  const uint8_t *b = (const uint8_t *)in;
  for (int i = 0; i < 32; i++) if (b[i]) return false;
  return true;
}
static bool pat_empty(int p) {
  const uint32_t *c = pat(p);
  for (int i = 0; i < PAT_CELLS; i++) if (c[i]) return false;
  return true;
}
static uint32_t song_encode(uint8_t *b, uint32_t max) {   // 0 if it does not fit
  int n_ins = 1, n_pat = 1;
  for (int i = 1; i < MT_INSTRUMENTS; i++) if (!ins_empty(&ins[i])) n_ins = i + 1;
  for (int i = 0; i < n_orders; i++) if (orders[i] + 1 > n_pat) n_pat = orders[i] + 1;
  for (int p = 0; p < NPAT; p++) if (!pat_empty(p) && p + 1 > n_pat) n_pat = p + 1;
  mt_header_t h;
  memset(&h, 0, sizeof h);
  h.magic = MT_MAGIC; h.n_orders = (uint8_t)n_orders; h.n_patterns = (uint8_t)n_pat;
  h.speed = (uint8_t)s_speed; h.bpm = (uint8_t)s_bpm; h.restart = (uint8_t)restart; h.master = (uint8_t)s_master;
  h.n_ins = (uint8_t)n_ins;
  memcpy(h.name, s_name, 16);
  uint32_t p = sizeof h;
  memcpy(b + p, ins, 32 * n_ins); p += 32 * n_ins;
  memcpy(b + p, orders, n_orders); p += n_orders;
  uint32_t offs = p; p += 2 * n_pat;
  for (int k = 0; k < n_pat; k++) {
    if (p + 2 + 16 * (8 + 4 * MT_ROWS) > max) return 0;
    b[offs + 2 * k] = (uint8_t)p; b[offs + 2 * k + 1] = (uint8_t)(p >> 8);
    p += mt_pattern_encode(pat(k), b + p);
    if (p > 0xFFFF) return 0;
  }
  h.size = p;
  memcpy(b, &h, sizeof h);
  return p;
}
static void song_stop(void) { mt_stop(); play_mode = 0; }
static void song_clear_patterns(void) { mc_fill(PATS, 0, NPAT * PAT_CELLS * 4); }
static bool song_load(const void *s, uint32_t max) {
  if (mt_song_check(s, max)) { message("MORCEAU ILLISIBLE"); return false; }
  const mt_header_t *h = s;
  if (h->n_patterns > NPAT) { message("TROP DE MOTIFS"); return false; }
  song_stop();
  mt_song_instruments(s, ins);
  memcpy(orders, (const uint8_t *)s + sizeof(mt_header_t) + 32 * h->n_ins, h->n_orders);
  n_orders = h->n_orders; restart = h->restart;
  s_speed = h->speed ? h->speed : 6; s_bpm = h->bpm >= 32 ? h->bpm : 125; s_master = h->master;
  memset(s_name, 0, sizeof s_name); memcpy(s_name, h->name, 16);
  song_clear_patterns();
  for (int p = 0; p < h->n_patterns; p++) mt_song_decode(s, p, pat(p));
  cur_ord = 0; cur_row = 0; dirty = 0; graphs_dirty = 1;
  return true;
}
static void song_new(void) {
  song_stop();
  mt_song_instruments(demo_file, ins);          // the default instruments are those of the demo
  song_clear_patterns();
  memset(orders, 0, sizeof orders); n_orders = 1; restart = 0;
  s_speed = 6; s_bpm = 125; s_master = 48;
  memset(s_name, 0, sizeof s_name); strcpy(s_name, "SANS NOM");
  cur_ord = 0; cur_row = 0; song_slot = -1; dirty = 0; graphs_dirty = 1;
}
static void song_save(int k) {
  uint32_t n = song_encode(songbuf, sizeof songbuf);
  if (!n) { message("MORCEAU TROP GROS POUR LA NVRAM"); return; }
  if (n > nv_free(k)) { snprintf(line, sizeof line, "NVRAM PLEINE (%u OCTETS A TROUVER)", (unsigned)(n - nv_free(k))); message(line); return; }
  nv_delete(k);
  uint32_t off = nv_end();
  for (uint32_t i = 0; i < n; i++) NV[off + i] = songbuf[i];
  nv32w(16 + 8 * k, off); nv32w(20 + 8 * k, n);
  NV[5] = (uint8_t)k;
  song_slot = k; dirty = 0;
  snprintf(line, sizeof line, "SAUVE EN %d (%u O) - L'OSD L'ECRIT SUR LA SD", k + 1, (unsigned)n);
  message(line);
}

// ================================================================ playback
static void play(int mode) {
  if (mode == 0) { song_stop(); return; }
  mt_source_t s = { .ins = ins, .orders = orders, .n_orders = n_orders, .restart = restart, .pattern = src_pattern,
                    .ctx = 0, .speed = s_speed, .bpm = s_bpm, .master = s_master };
  if (mode == 2) { one_order[0] = (uint8_t)cur_pat(); s.orders = one_order; s.n_orders = 1; s.restart = 0; }
  mt_play_source(&s, mode == 2 ? 0 : cur_ord, mode == 2 ? cur_row : 0, mode == 2);
  play_mode = mode; play_frames = 0;
}
static void jam_vol(int key, int track, int note, int inst, int vol);
static void jam(int key, int track, int note, int inst) { jam_vol(key, track, note, inst, 64); }
static void jam_vol(int key, int track, int note, int inst, int vol) {
  if (note < 1 || note > 96 || inst < 1 || ins[inst].type != mt_track_type(track)) return;
  mt_note(track, note, &ins[inst], s_master * vol / 64);
  jam_track[key & 511] = (int8_t)track;
}
static int free_track(int type) {               // polyphony on the instrument page
  static int rr[2];
  int base = type == MT_TYPE_PCM ? 8 : 0;
  for (int i = 0; i < 8; i++) {
    int t = base + (rr[type] + i) % 8;
    bool used = false;
    for (int k = 0; k < 512; k++) if (jam_track[k] == t) { used = true; break; }
    if (!used) { rr[type] = (t - base + 1) % 8; return t; }
  }
  return base;
}

// ================================================================ common drawing
static const char hexd[] = "0123456789ABCDEF";
static const char *const note_names[12] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };
static const char *const fx_help[16] = {
  "0XY ARPEGE: NOTE, +X, +Y DEMI-TONS",
  "1XX MONTEE DE XX/16 DEMI-TON PAR TICK",
  "2XX DESCENTE DE XX/16 DEMI-TON PAR TICK",
  "3XX PORTAMENTO VERS LA NOTE, VITESSE XX",
  "4XY VIBRATO: VITESSE X, AMPLEUR Y",
  "5 (LIBRE)", "6 (LIBRE)", "7 (LIBRE)",
  "8XX PANORAMIQUE: 00 GAUCHE 80 CENTRE FF DROITE",
  "9XX PSG: RAPPORT CYCLIQUE / PCM: DEBUT XX00",
  "AXY VOLUME: +X ET -Y PAR TICK",
  "BXX SAUT A LA POSITION XX",
  "CXX VOLUME DE LA PISTE (00-40)",
  "DXX FIN DU MOTIF, REPRISE LIGNE XX",
  "E1X E2X FIN  E9X REDECL.  ECX COUPE  EDX RETARD",
  "FXX VITESSE (01-1F) OU BPM (20-FF)" };

static void note_str(int n, char *s) {
  if (n == 0) strcpy(s, "...");
  else if (n == MT_NOTE_OFF) strcpy(s, "===");
  else { strcpy(s, note_names[(n - 1) % 12]); s[2] = (char)('0' + (n - 1) / 12); s[3] = 0; }
}
// ---------------------------------------------------------------- top panel, common to the pages
// spin buttons: POS MOTIF LONG BPM VIT VOL OCT PAS INSTR; buttons: two columns of five
enum { SP_POS, SP_PAT, SP_LEN, SP_BPM, SP_SPEED, SP_VOL, SP_OCT, SP_STEP, SP_INS, SP_COUNT };
enum { BT_PLAY, BT_PATT, BT_STOP, BT_EDIT, BT_FOLLOW, BT_SAVE, BT_PG_PAT, BT_PG_INS, BT_PG_SONG, BT_PG_HELP, BT_COUNT };
static const char *const spin_labels[SP_COUNT] = { "POS", "MOTIF", "LONG", "BPM", "VIT", "VOL", "OCT", "PAS", "INSTR" };
static const char *const btn_labels[BT_COUNT] = { "JOUER", "MOTIF", "STOP", "EDIT", "SUIVI", "SAUVER", "MOTIFS", "SONS", "MORCEAU", "AIDE" };
static int cur_ins(void);
static int spin_value(int k) {
  switch (k) {
    case SP_POS: return cur_ord;
    case SP_PAT: return orders[cur_ord];
    case SP_LEN: return n_orders;
    case SP_BPM: return s_bpm;
    case SP_SPEED: return s_speed;
    case SP_VOL: return s_master;
    case SP_OCT: return octave;
    case SP_STEP: return step;
    default: return cur_ins();
  }
}
static bool button_on(int b) {
  switch (b) {
    case BT_PLAY: return play_mode == 1;
    case BT_PATT: return play_mode == 2;
    case BT_EDIT: return edit_mode;
    case BT_FOLLOW: return follow;
    case BT_PG_PAT: return page == PG_PAT;
    case BT_PG_INS: return page == PG_INS;
    case BT_PG_SONG: return page == PG_SONG;
    case BT_PG_HELP: return page == PG_HELP;
  }
  return false;
}
static void button(int x0, int x1, int y, const char *label, bool on, int id, int arg) {
  box(x0, y, x1, y, on ? P_PRESSED : P_RAISED);
  printc(x0, x1 - 1, y, label, on ? T_WHITE : T_INK);
  zone(x0, y, x1, y, id, arg);
}
static void draw_top(void) {
  char s[24];
  // spin buttons (the panels start at the top of the screen, their top edge just above it)
  box(0, 0, 14, 8, P_RAISED);
  for (int k = 0; k < SP_COUNT; k++) {
    int y = k, v = spin_value(k);
    print(1, y, spin_labels[k], T_INK);
    if (k <= SP_LEN || k == SP_INS) snprintf(s, sizeof s, "%02X", v); else snprintf(s, sizeof s, "%3d", v);
    print(11 - (int)strlen(s), y, s, T_BLUE);
    putg(12, y, G_UP, T_INK); putg(13, y, G_DOWN, T_INK);
    zone(1, y, 11, y, Z_SPIN, k * 4 + 2);        // the value: wheel
    zone(12, y, 12, y, Z_SPIN, k * 4);           // up
    zone(13, y, 13, y, Z_SPIN, k * 4 + 1);       // down
  }
  // buttons
  for (int b = 0; b < BT_COUNT; b++) {
    int x0 = b & 1 ? 23 : 15, y = 2 * (b >> 1);
    button(x0, x0 + 7, y, btn_labels[b], button_on(b), Z_BTN, b);
  }
  // track meters: a bar per track, number below (click: choose the track, right click: mute)
  box(31, 0, 79, 8, P_SCOPE);
  draw_scope();
  for (int t = 0; t < MT_TRACKS; t++) {
    int x = 32 + 3 * t;
    for (int y = 0; y < 8; y++) { putg(x, y, G_SCOPE + y * 32 + 2 * t, P_BARS); putg(x + 1, y, G_SCOPE + y * 32 + 2 * t + 1, P_BARS); }
    char c = t < 8 ? (char)('1' + t) : (char)('A' + t - 8);
    int pal = mt_muted(t) ? C_RED : t == cur_track ? C_WHITE : t < 8 ? C_CYAN : C_GREEN;
    putg(x, 8, c - 32, pal);
    zone(x, 0, x + 1, 8, Z_SCOPE, t);
  }
}
// the information panel under the top one: song, instrument, time, then messages or a hint
static const char *info_hint = "";
static void draw_info(void) {
  char s[24];
  box(0, 10, 79, 11, P_RAISED);
  print(1, 10, "MONOTRACKER", T_BLUE);
  printn(13, 10, s_name, 16, T_INK);
  int in = cur_ins();
  s[0] = hexd[in >> 4]; s[1] = hexd[in & 15]; s[2] = ' '; memcpy(s + 3, ins[in].name, 10); s[13] = 0;
  print(46, 10, s, T_INK);
  int sec = play_frames / 60;
  snprintf(s, sizeof s, "%2d:%02d", sec / 60, sec % 60);
  print(74, 10, s, play_mode ? T_BLUE : T_DIM);
  if (msg_timer > 0) print(1, 11, msg, T_RED);
  else print(1, 11, info_hint, T_DIM);
}

// ================================================================ pattern page
static const int field_x[8] = { 0, 3, 4, 5, 6, 7, 8, 9 };
static const int field_w[8] = { 3, 1, 1, 1, 1, 1, 1, 1 };
#define PAT_Y0 14              // first row of the pattern on the screen
#define PAT_Y1 28
#define PAT_YC 21              // the cursor row
#define TRACK_X(i) (4 + (i) * 12)
#define NVIEW 6                // tracks shown at once
static int track_view(void) { return cur_track < 6 ? 0 : cur_track < 12 ? 6 : 10; }   // 0-5, 6-11, 10-15
static void draw_pattern(void) {
  int p = cur_pat(), view = track_view();
  bool play_here = play_mode && (play_mode == 2 || orders[mt_order()] == p);
  char s[16];
  box(0, 13, 79, 28, P_SUNK);
  // headers of the tracks shown (click: mute, right click: solo)
  snprintf(s, sizeof s, "%02X", p);
  print(1, 13, s, C_YELLOW);
  for (int i = 0; i < NVIEW; i++) {
    int t = view + i, x = TRACK_X(i);
    snprintf(line, sizeof line, "%s %d", t < 8 ? "PSG" : "PCM", (t & 7) + 1);
    print(x, 13, line, mt_muted(t) ? C_RED : t == cur_track ? C_WHITE : t < 8 ? C_CYAN : C_GREEN);
    if (mt_muted(t)) print(x + 6, 13, "MUET", C_RED);
    else { int v = vu[t] * 8 / 256; if (v) putg(x + 6, 13, G_VU + v - 1, vu[t] > 200 ? C_ORANGE : C_GREEN); }
    zone(x, 13, x + 9, 13, Z_TRACK, t);
  }
  if (edit_mode) print(74, 13, "EDIT", C_RED);
  // rows
  const uint32_t *cells = pat(p);
  int prow = play_here ? mt_row() : -1;
  for (int y = PAT_Y0; y <= PAT_Y1; y++) {
    int r = cur_row + y - PAT_YC;
    if (r < 0 || r >= MT_ROWS) continue;
    if (y == PAT_YC) bar(1, 78, y, edit_mode ? B_EDIT : B_CURSOR);
    else if (r == prow) bar(1, 78, y, B_FIELD);
    else if (r % 4 == 0) bar(1, 78, y, B_BEAT);
    s[0] = hexd[r >> 4]; s[1] = hexd[r & 15]; s[2] = 0;
    print(1, y, s, r % 16 == 0 ? C_YELLOW : r % 4 == 0 ? C_WHITE : C_GREY);
    for (int i = 0; i < NVIEW; i++) {
      int t = view + i, x = TRACK_X(i);
      uint32_t c = cells[r * MT_TRACKS + t];
      int n = MT_C_NOTE(c), in = MT_C_INST(c), vo = MT_C_VOL(c), fx = MT_C_FX(c), pa = MT_C_PARAM(c);
      int dim = mt_muted(t);
      note_str(n, s); print(x, y, s, n && !dim ? C_WHITE : C_GREY);
      if (in) { s[0] = hexd[in >> 4]; s[1] = hexd[in & 15]; s[2] = 0; print(x + 3, y, s, dim ? C_GREY : ins[in].type == mt_track_type(t) ? C_CYAN : C_RED); }
      else print(x + 3, y, "..", C_GREY);
      if (vo) { s[0] = hexd[(vo - 1) >> 4]; s[1] = hexd[(vo - 1) & 15]; s[2] = 0; print(x + 5, y, s, dim ? C_GREY : C_GREEN); }
      else print(x + 5, y, "..", C_GREY);
      if (fx) { s[0] = hexd[fx - 1]; s[1] = hexd[pa >> 4]; s[2] = hexd[pa & 15]; s[3] = 0; print(x + 7, y, s, dim ? C_GREY : C_ORANGE); }
      else print(x + 7, y, "...", C_GREY);
      if (i < NVIEW - 1) putg(x + 11, y, '|' - 32, C_GREY);
      if (y == PAT_YC && t == cur_track) bar(x + field_x[cur_field], x + field_x[cur_field] + field_w[cur_field] - 1, y, B_FIELD);
      zone(x, y, x + 9, y, Z_CELL, ((y - PAT_YC + 64) << 4) | i);
    }
  }
  zone(1, PAT_Y0, 2, PAT_Y1, Z_ROWS, 0);
  const char *hint = "ENTREE JOUE  ESPACE EDIT  F4 AIDE";
  uint32_t c = cells[cur_row * MT_TRACKS + cur_track];
  if (cur_field >= 5 && MT_C_FX(c)) hint = fx_help[MT_C_FX(c) - 1];
  else if (cur_field >= 5) hint = "EFFET: 0-F PUIS PARAMETRE (F4: LISTE)";
  else if (cur_field >= 3) hint = "VOLUME DE LA NOTE: 00 A 40";
  else if (cur_field >= 1) hint = "INSTRUMENT (BOUTON SONS POUR LE REGLER)";
  info_hint = hint;
}

static void set_cell(int r, int t, uint32_t c) { pat(cur_pat())[r * MT_TRACKS + t] = c; dirty = 1; }
static uint32_t get_cell(int r, int t) { return pat(cur_pat())[r * MT_TRACKS + t]; }
static void advance(void) { cur_row = (cur_row + step) % MT_ROWS; }

static void pattern_key(int k) {
  int t = cur_track, typ = mt_track_type(t);
  uint32_t c = get_cell(cur_row, t);
  if (ctrl()) {
    int ch = key_char(k);
    if (k == KEY_UP && cur_ord > 0) cur_ord--;
    else if (k == KEY_DOWN && cur_ord < n_orders - 1) cur_ord++;
    else if (k == KEY_LEFT && orders[cur_ord] > 0) { orders[cur_ord]--; dirty = 1; }
    else if (k == KEY_RIGHT && orders[cur_ord] < NPAT - 1) { orders[cur_ord]++; dirty = 1; }
    else if (ch == 'M') mt_mute(t, !mt_muted(t));
    else if (ch == 'O') {                       // solo
      bool solo = true;
      for (int i = 0; i < MT_TRACKS; i++) if (i != t && !mt_muted(i)) solo = false;
      for (int i = 0; i < MT_TRACKS; i++) mt_mute(i, solo ? false : i != t);
    }
    else if (ch == 'F') { follow = !follow; message(follow ? "SUIVI DE LA LECTURE: OUI" : "SUIVI DE LA LECTURE: NON"); }
    else if (ch == 'C' || ch == 'X') {
      for (int r = 0; r < MT_ROWS; r++) clip_track[r] = get_cell(r, t);
      clip_track_ok = 1;
      if (ch == 'X') for (int r = 0; r < MT_ROWS; r++) set_cell(r, t, 0);
      message(ch == 'X' ? "PISTE COUPEE" : "PISTE COPIEE");
    }
    else if (ch == 'V' && clip_track_ok) { for (int r = 0; r < MT_ROWS; r++) set_cell(r, t, clip_track[r]); message("PISTE COLLEE"); }
    return;
  }
  if (alt()) {
    int ch = key_char(k);
    if (ch == 'C' || ch == 'X') {
      memcpy(clip_pat, pat(cur_pat()), sizeof clip_pat); clip_pat_ok = 1;
      if (ch == 'X') { mc_fill(pat(cur_pat()), 0, sizeof clip_pat); dirty = 1; }
      message(ch == 'X' ? "MOTIF COUPE" : "MOTIF COPIE");
    } else if (ch == 'V' && clip_pat_ok) { memcpy(pat(cur_pat()), clip_pat, sizeof clip_pat); dirty = 1; message("MOTIF COLLE"); }
    return;
  }
  switch (k) {
    case KEY_UP: cur_row = (cur_row + MT_ROWS - 1) % MT_ROWS; return;
    case KEY_DOWN: cur_row = (cur_row + 1) % MT_ROWS; return;
    case KEY_PGUP: cur_row = clampi(cur_row - 16, 0, MT_ROWS - 1); return;
    case KEY_PGDN: cur_row = clampi(cur_row + 16, 0, MT_ROWS - 1); return;
    case KEY_HOME: cur_row = 0; return;
    case KEY_END: cur_row = MT_ROWS - 1; return;
    case KEY_LEFT: if (--cur_field < 0) { cur_field = 7; cur_track = (cur_track + 15) % 16; } return;
    case KEY_RIGHT: if (++cur_field > 7) { cur_field = 0; cur_track = (cur_track + 1) % 16; } return;
    case KEY_TAB: cur_track = (cur_track + (shift() ? 15 : 1)) % 16; cur_field = 0; return;
    case KEY_SPACE: edit_mode = !edit_mode; return;
    case KEY_DELETE:
      if (!edit_mode) return;
      if (shift() || cur_field == 0) c = shift() ? 0 : c & ~0xFFFu;
      else if (cur_field <= 2) c &= ~(0x1Fu << 7);
      else if (cur_field <= 4) c &= ~(0x7Fu << 12);
      else c &= 0x7FFFFu;
      set_cell(cur_row, t, c); advance(); return;
    case KEY_INSERT:
      if (!edit_mode) return;
      for (int r = MT_ROWS - 1; r > cur_row; r--) set_cell(r, t, get_cell(r - 1, t));
      set_cell(cur_row, t, 0); return;
    case KEY_BACKSPACE:
      if (!edit_mode) return;
      for (int r = cur_row; r < MT_ROWS - 1; r++) set_cell(r, t, get_cell(r + 1, t));
      set_cell(MT_ROWS - 1, t, 0); return;
  }
  if (cur_field == 0) {
    int pk = piano_key(k);
    if (pk >= 0) {
      int n = clampi(octave * 12 + pk + 1, 1, 96), in = typ == MT_TYPE_PSG ? ins_psg : ins_pcm;
      jam(k, t, n, in);
      if (edit_mode) { set_cell(cur_row, t, (c & ~0xFFFu) | (uint32_t)n | ((uint32_t)in << 7)); advance(); }
      return;
    }
    if (k == KEY_1 && edit_mode) { set_cell(cur_row, t, (c & ~0xFFFu) | MT_NOTE_OFF); advance(); }
    return;
  }
  int h = key_hex(k);
  if (h < 0 || !edit_mode) return;
  if (cur_field <= 2) {
    int v = MT_C_INST(c);
    v = cur_field == 1 ? (h << 4) | (v & 15) : (v & 0xF0) | h;
    if (v > 31) v = 31;
    c = (c & ~(0x1Fu << 7)) | ((uint32_t)v << 7);
    if (v) { if (ins[v].type == MT_TYPE_PSG) ins_psg = v; else ins_pcm = v; }
  } else if (cur_field <= 4) {
    int v = MT_C_VOL(c) ? MT_C_VOL(c) - 1 : 0;
    v = cur_field == 3 ? (h << 4) | (v & 15) : (v & 0xF0) | h;
    if (v > 64) v = 64;
    c = (c & ~(0x7Fu << 12)) | ((uint32_t)(v + 1) << 12);
  } else if (cur_field == 5) {
    c = (c & ~(0x1Fu << 19)) | ((uint32_t)(h + 1) << 19);
  } else {
    int v = MT_C_PARAM(c);
    v = cur_field == 6 ? (h << 4) | (v & 15) : (v & 0xF0) | h;
    c = (c & 0x00FFFFFFu) | ((uint32_t)v << 24);
    if (!MT_C_FX(c)) c |= 1u << 19;             // a parameter alone: effect 0
  }
  set_cell(cur_row, t, c);
  // the digits of a column follow each other, then the cursor goes down to the next row
  static const uint8_t first[8] = { 0, 1, 1, 3, 3, 5, 5, 5 }, last[8] = { 0, 2, 2, 4, 4, 7, 7, 7 };
  if (cur_field < last[cur_field]) cur_field++;
  else { cur_field = first[cur_field]; advance(); }
}

// ================================================================ instrument page
enum { K_NAME, K_TYPE, K_WAVE, K_U8, K_S8, K_FLAG, K_PAN, K_LOOP };
typedef struct { const char *label; uint8_t kind, off, lo, hi; const char *help; } param_t;
#define OFF(f) ((uint8_t)offsetof(mt_instrument_t, f))
static const param_t params[] = {
  { "NOM",          K_NAME, 0, 0, 0,     "ENTREE POUR ECRIRE LE NOM" },
  { "TYPE",         K_TYPE, OFF(type), 0, 1, "PSG (OSCILLATEUR) OU PCM (ECHANTILLON)" },
  { "ONDE",         K_WAVE, OFF(wave), 0, 3, "CARRE, TRIANGLE, DENT DE SCIE, BRUIT" },
  { "RAPPORT",      K_U8, OFF(duty), 1, 255, "RAPPORT CYCLIQUE DU CARRE (128 = 50 %)" },
  { "BALAYAGE",     K_S8, OFF(duty_sweep), 0, 0, "AJOUTE AU RAPPORT A CHAQUE TICK (PWM)" },
  { "REBOND",       K_FLAG, OFF(flags), MT_IF_BOUNCE, 0, "LE BALAYAGE REBONDIT AUX BORNES" },
  { "VOLUME",       K_U8, OFF(volume), 0, 64, "VOLUME DE L'INSTRUMENT (0-64)" },
  { "PANORAMIQUE",  K_PAN, OFF(pan), 0, 255, "POSITION STEREO" },
  { "ATTAQUE",      K_U8, OFF(attack), 0, 255, "TICKS POUR MONTER AU VOLUME MAXIMAL" },
  { "DECLIN",       K_U8, OFF(decay), 0, 255, "TICKS POUR DESCENDRE AU MAINTIEN" },
  { "MAINTIEN",     K_U8, OFF(sustain), 0, 64, "NIVEAU TENU (0 = SON PERCUSSIF)" },
  { "RELACHE",      K_U8, OFF(release), 0, 255, "TICKS POUR S'ETEINDRE APRES === " },
  { "VIB VITESSE",  K_U8, OFF(vib_speed), 0, 63, "VITESSE DU VIBRATO" },
  { "VIB AMPLEUR",  K_U8, OFF(vib_depth), 0, 63, "AMPLEUR DU VIBRATO (1/16 DE DEMI-TON)" },
  { "VIB DELAI",    K_U8, OFF(vib_delay), 0, 255, "TICKS AVANT LE DEBUT DU VIBRATO" },
  { "ARP +1",       K_S8, OFF(arp1), 0, 0, "ARPEGE: 2E NOTE EN DEMI-TONS" },
  { "ARP +2",       K_S8, OFF(arp2), 0, 0, "ARPEGE: 3E NOTE EN DEMI-TONS" },
  { "ARP VITESSE",  K_U8, OFF(arp_speed), 0, 255, "TICKS PAR NOTE D'ARPEGE (0 = SANS)" },
  { "GLISSEMENT",   K_S8, OFF(pitch_sweep), 0, 0, "1/16 DE DEMI-TON PAR TICK (GROSSE CAISSE: -60)" },
  { "ACCORD FIN",   K_S8, OFF(finetune), 0, 0, "1/64 DE DEMI-TON" },
  { "TRANSPOSE",    K_S8, OFF(transpose), 0, 0, "DEMI-TONS AJOUTES A LA NOTE" },
  { "HAUTEUR FIXE", K_FLAG, OFF(flags), MT_IF_FIXED, 0, "JOUE TOUJOURS LA MEME NOTE (BATTERIE)" },
};
#define NPARAMS ((int)(sizeof params / sizeof params[0]))
static const uint8_t psg_list[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21 };
static const uint8_t pcm_list[] = { 0, 1, 2, 3, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21 };
static const char *const wave_names[4] = { "CARRE", "TRIANGLE", "DENT SCIE", "BRUIT" };

static int plist(const uint8_t **l) {
  if (ins[ins_sel].type == MT_TYPE_PCM) { *l = pcm_list; return sizeof pcm_list; }
  *l = psg_list; return sizeof psg_list;
}
static void param_value(const mt_instrument_t *in, int pi, char *s, int n) {
  const param_t *p = &params[pi];
  const uint8_t *b = (const uint8_t *)in;
  switch (p->kind) {
    case K_NAME: snprintf(s, n, "%.10s%s", in->name, naming == 1 ? "_" : ""); break;
    case K_TYPE: snprintf(s, n, "%s", in->type == MT_TYPE_PCM ? "PCM" : "PSG"); break;
    case K_WAVE:
      if (in->type == MT_TYPE_PSG) snprintf(s, n, "%s", wave_names[in->wave & 3]);
      else { const mt_sample_t *sm = mt_bank_sample(in->wave); snprintf(s, n, "%02d %.8s", in->wave, sm ? sm->name : "?"); }
      break;
    case K_U8:
      if (pi == 3 && in->type == MT_TYPE_PCM) snprintf(s, n, "%s", in->duty == 1 ? "NON" : "OUI");
      else snprintf(s, n, "%d", b[p->off]);
      break;
    case K_S8: snprintf(s, n, "%+d", (int8_t)b[p->off]); break;
    case K_FLAG: snprintf(s, n, "%s", (b[p->off] & p->lo) ? "OUI" : "NON"); break;
    case K_PAN: {
      int v = b[p->off];
      if (v == 128) snprintf(s, n, "CENTRE");
      else snprintf(s, n, "%s %d", v < 128 ? "GAUCHE" : "DROITE", v < 128 ? 128 - v : v - 128);
      break;
    }
  }
}
static void param_change(int pi, int d) {
  const param_t *p = &params[pi];
  mt_instrument_t *in = &ins[ins_sel];
  uint8_t *b = (uint8_t *)in;
  switch (p->kind) {
    case K_TYPE:
      in->type = in->type == MT_TYPE_PSG ? MT_TYPE_PCM : MT_TYPE_PSG;
      in->wave = 0; in->duty = in->type == MT_TYPE_PSG ? 128 : 0;
      ins_field = 1;
      break;
    case K_WAVE:
      in->wave = (uint8_t)clampi(in->wave + (d > 0 ? 1 : -1), 0, in->type == MT_TYPE_PSG ? 3 : mt_bank_count() - 1);
      break;
    case K_U8:
      if (pi == 3 && in->type == MT_TYPE_PCM) { in->duty = in->duty == 1 ? 0 : 1; break; }
      b[p->off] = (uint8_t)clampi(b[p->off] + d, p->lo, p->hi);
      break;
    case K_S8: b[p->off] = (uint8_t)(int8_t)clampi((int8_t)b[p->off] + d, -128, 127); break;
    case K_FLAG: b[p->off] ^= p->lo; break;
    case K_PAN: b[p->off] = (uint8_t)clampi(b[p->off] + d, 0, 255); break;
  }
  dirty = 1; graphs_dirty = 1;
}

static void draw_instrument(void) {
  const uint8_t *l;
  int n = plist(&l);
  mt_instrument_t *in = &ins[ins_sel];
  char s[40];
  if (ins_field >= n) ins_field = n - 1;
  box(0, 13, 79, 28, P_SUNK);
  snprintf(line, sizeof line, "SON %02X", ins_sel);
  print(1, 13, line, C_YELLOW);
  print(8, 13, in->name, C_WHITE);
  print(19, 13, in->type == MT_TYPE_PCM ? "PCM" : "PSG", in->type == MT_TYPE_PCM ? C_GREEN : C_CYAN);
  print(62, 13, "CLIC +1 DROIT -1", C_GREY);
  // parameters, in two columns of 11
  for (int i = 0; i < n; i++) {
    int col = i / 11, y = 14 + i % 11, x = col ? 41 : 1, vx = col ? 55 : 15, vw = 12;
    const char *lab = params[l[i]].label;
    if (in->type == MT_TYPE_PCM && l[i] == 2) lab = "ECHANTILLON";
    if (in->type == MT_TYPE_PCM && l[i] == 3) lab = "BOUCLE";
    if (i == ins_field) bar(x, vx + vw - 1, y, naming ? B_EDIT : B_CURSOR);
    print(x, y, lab, i == ins_field ? C_WHITE : C_GREY);
    param_value(in, l[i], s, vw + 1);
    printn(vx, y, s, vw, i == ins_field ? C_YELLOW : C_WHITE);
    zone(x, y, vx - 1, y, Z_PARAM, i);              // the label: chooses the setting
    zone(vx, y, vx + vw - 1, y, Z_PARAM, 0x100 | i);   // the value: +1, right click -1, wheel
  }
  // graphs and help
  if (graphs_dirty) { draw_graphs(in); graphs_dirty = 0; }
  show_graph(0, 1, 25);
  show_graph(1, 26, 25);
  const char *h = params[l[ins_field]].help;
  for (int k = 0; k < 4 && *h; k++) {                 // help, cut into lines of 27 characters
    int len = slen(h, 64), cut = len <= 27 ? len : 27;
    if (len > 27) { while (cut > 0 && h[cut] != ' ') cut--; if (!cut) cut = 27; }
    memcpy(s, h, cut); s[cut] = 0;
    print(51, 25 + k, s, C_GREY);
    h += cut; while (*h == ' ') h++;
  }
  info_hint = "PG.PREC/SUIV: AUTRE SON  PIANO: ECOUTE";
}

static void instrument_key(int k) {
  const uint8_t *l;
  int n = plist(&l);
  mt_instrument_t *in = &ins[ins_sel];
  if (naming == 1) {
    int len = slen(in->name, 10), c = key_char(k);
    if (k == KEY_ENTER || k == KEY_ESC) naming = 0;
    else if (k == KEY_BACKSPACE && len) in->name[len - 1] = 0;
    else if (c && c != ',' && len < 10) { in->name[len] = (char)c; if (len < 9) in->name[len + 1] = 0; }
    dirty = 1;
    return;
  }
  if (ctrl()) {
    int c = key_char(k);
    if (c == 'C') { clip_ins = *in; clip_ins_ok = 1; message("INSTRUMENT COPIE"); }
    else if (c == 'V' && clip_ins_ok) { *in = clip_ins; dirty = 1; graphs_dirty = 1; message("INSTRUMENT COLLE"); }
    else if (c == 'X') { clip_ins = *in; clip_ins_ok = 1; memset(in, 0, sizeof *in); dirty = 1; graphs_dirty = 1; message("INSTRUMENT EFFACE"); }
    return;
  }
  int big = shift() ? 10 : 1;
  switch (k) {
    case KEY_UP: ins_field = (ins_field + n - 1) % n; return;
    case KEY_DOWN: ins_field = (ins_field + 1) % n; return;
    case KEY_PGUP: if (ins_sel > 1) { ins_sel--; graphs_dirty = 1; ins_field = clampi(ins_field, 0, plist(&l) - 1); } return;
    case KEY_PGDN: if (ins_sel < MT_INSTRUMENTS - 1) { ins_sel++; graphs_dirty = 1; ins_field = clampi(ins_field, 0, plist(&l) - 1); } return;
    case KEY_LEFT: if (l[ins_field]) param_change(l[ins_field], -big); return;
    case KEY_RIGHT: if (l[ins_field]) param_change(l[ins_field], big); return;
    case KEY_ENTER:
      if (l[ins_field] == 0) naming = 1;
      else if (l[ins_field] == 1) param_change(1, 1);
      return;
  }
  if (ins_empty(in)) return;
  int pk = piano_key(k);
  if (pk >= 0) {
    if (in->type == MT_TYPE_PSG) ins_psg = ins_sel; else ins_pcm = ins_sel;
    int t = free_track(in->type);
    jam(k, t, clampi(octave * 12 + pk + 1, 1, 96), ins_sel);
  }
}

// ================================================================ song page
enum { SF_NAME, SF_BPM, SF_SPEED, SF_MASTER, SF_RESTART, SF_LAYOUT, SF_VELO, SF_COUNT };
static const char *const song_labels[SF_COUNT] = { "NOM", "BPM", "VITESSE", "VOLUME", "REPRISE", "CLAVIER", "VELOCITE" };
static const char *const song_help[SF_COUNT] = {
  "ENTREE POUR ECRIRE LE NOM DU MORCEAU", "TEMPO: 125 BPM = 50 TICKS PAR SECONDE",
  "TICKS PAR LIGNE (6 = UNE DOUBLE CROCHE)", "VOLUME GENERAL DU MORCEAU (0-64)",
  "POSITION REJOUEE APRES LA DERNIERE", "DISPOSITION DU CLAVIER (POUR LES LETTRES)",
  "CLAVIER MIDI: LA FORCE DE FRAPPE ECRIT LE VOLUME" };

enum { SB_SAVE, SB_LOAD, SB_DELETE, SB_NEW, SB_DEMO, SB_COUNT };
static const char *const songbtn_labels[SB_COUNT] = { "SAUVER", "CHARGER", "EFFACER", "NOUVEAU", "DEMO" };
static const char songbtn_keys[SB_COUNT] = { 'S', 'L', 'D', 'E', 'R' };
static bool song_action(int c);
static void draw_song(void) {
  box(0, 13, 79, 28, P_SUNK);
  static const char *const pane[3] = { "ORDRE", "REGLAGES", "NVRAM" };
  static const int px[3] = { 1, 22, 50 };
  for (int i = 0; i < 3; i++) print(px[i], 13, pane[i], song_pane == i ? C_YELLOW : C_GREY);
  if (dirty) print(32, 13, "MODIFIE", C_ORANGE);
  // order list (click: position; on the pattern number: +1, right -1; wheel: scrolls)
  if (cur_ord < ord_scroll) ord_scroll = cur_ord;
  if (cur_ord >= ord_scroll + 15) ord_scroll = cur_ord - 14;
  if (ord_scroll > n_orders - 1) ord_scroll = n_orders > 15 ? n_orders - 15 : 0;
  for (int i = 0; i < 15; i++) {
    int o = ord_scroll + i, y = 14 + i;
    if (o >= n_orders) break;
    snprintf(line, sizeof line, "%02X MOTIF %02X", o, orders[o]);
    if (o == cur_ord) bar(1, 11, y, song_pane == 0 ? B_CURSOR : B_BEAT);
    print(1, y, line, o == restart ? C_CYAN : C_WHITE);
    if (play_mode == 1 && mt_order() == o) putg(12, y, G_ARROW, C_GREEN);
    zone(1, y, 6, y, Z_ORDER, o);
    zone(7, y, 12, y, Z_ORDPAT, o);
  }
  // settings (click +1, right -1, wheel; the name: click to write it)
  for (int f = 0; f < SF_COUNT; f++) {
    int y = f == SF_NAME ? 14 : 15 + 2 * f;
    bool cur = song_pane == 1 && song_field == f;
    if (cur) { bar(22, 46, y, naming == 2 ? B_EDIT : B_CURSOR); if (f == SF_NAME) bar(22, 46, y + 1, naming == 2 ? B_EDIT : B_CURSOR); }
    print(22, y, song_labels[f], C_GREY);
    switch (f) {
      case SF_NAME: snprintf(line, sizeof line, "%.16s%s", s_name, naming == 2 ? "_" : ""); break;
      case SF_BPM: snprintf(line, sizeof line, "%d", s_bpm); break;
      case SF_SPEED: snprintf(line, sizeof line, "%d", s_speed); break;
      case SF_MASTER: snprintf(line, sizeof line, "%d", s_master); break;
      case SF_RESTART: snprintf(line, sizeof line, "%02X", restart); break;
      case SF_LAYOUT: snprintf(line, sizeof line, "%s", layout ? "QWERTY" : "AZERTY"); break;
      case SF_VELO: snprintf(line, sizeof line, "%s", velo_off ? "NON" : "OUI"); break;
    }
    print(f == SF_NAME ? 22 : 33, f == SF_NAME ? y + 1 : y, line, C_YELLOW);
    zone(22, y, 46, f == SF_NAME ? y + 1 : y, Z_SETTING, f);
  }
  // NVRAM slots and their buttons
  for (int k = 0; k < NV_SLOTS; k++) {
    int y = 14 + k;
    if (slot_sel == k) bar(50, 78, y, song_pane == 2 ? B_CURSOR : B_BEAT);
    snprintf(line, sizeof line, "%d", k + 1);
    print(50, y, line, k == song_slot ? C_YELLOW : C_GREY);
    if (slot_size(k)) {
      const char *nm = mt_song_name((const void *)(NVRAM_BASE + slot_off(k)));
      char s[17]; for (int i = 0; i < 16; i++) s[i] = nm[i]; s[16] = 0;
      printn(52, y, s, 16, C_WHITE);
      snprintf(line, sizeof line, "%2u.%uK", (unsigned)(slot_size(k) / 1024), (unsigned)(slot_size(k) % 1024 * 10 / 1024));
      print(72 + (5 - (int)strlen(line)) + 1, y, line, C_GREY);
    } else print(52, y, "(VIDE)", C_GREY);
    zone(50, y, 78, y, Z_SLOT, k);
  }
  snprintf(line, sizeof line, "%5u O", (unsigned)nv_free(-1));
  print(71, 13, line, C_GREEN);
  for (int b = 0; b < SB_COUNT; b++) {
    int x0 = b & 1 ? 62 : 50, y = 23 + 2 * (b >> 1);
    if (b == SB_DEMO) x0 = 50;
    button(x0, x0 + 10, y, songbtn_labels[b], confirm_key == songbtn_keys[b] && confirm_timer > 0, Z_SONGBTN, b);
  }
  info_hint = (song_pane == 0 ? "G/D: MOTIF  INSER/SUPPR  ENTREE: EDITER" :
               song_pane == 1 ? song_help[song_field] : "ENTREE OU CTRL+L CHARGE, CTRL+S SAUVE ICI");
}

// actions of the song page, from Ctrl+key or from its buttons; those that lose something ask
// for the same action a second time
static bool song_action(int c) {
  if (c == 'S') {
    int k2 = song_pane == 2 ? slot_sel : song_slot >= 0 ? song_slot : slot_sel;
    if (!slot_size(k2) || k2 == song_slot || confirm('S', "REMPLACER CET EMPLACEMENT")) song_save(k2);
    return true;
  }
  if (c == 'L') {
    if (!slot_size(slot_sel)) { message("EMPLACEMENT VIDE"); return true; }
    if (dirty && !confirm('L', "MODIFICATIONS PERDUES")) return true;
    if (song_load((const void *)(NVRAM_BASE + slot_off(slot_sel)), slot_size(slot_sel))) { song_slot = slot_sel; NV[5] = (uint8_t)slot_sel; message("MORCEAU CHARGE"); }
    return true;
  }
  if (c == 'D') {
    if (slot_size(slot_sel) && confirm('D', "EFFACER L'EMPLACEMENT")) { nv_delete(slot_sel); if (song_slot == slot_sel) song_slot = -1; message("EMPLACEMENT EFFACE"); }
    return true;
  }
  // new song: Ctrl+E (Ctrl+N would open a window in the simulator's browser)
  if (c == 'E') { if (!dirty || confirm('E', "MODIFICATIONS PERDUES")) { song_new(); message("NOUVEAU MORCEAU"); } return true; }
  if (c == 'R') { if (!dirty || confirm('R', "MODIFICATIONS PERDUES")) { song_load(demo_file, demo_size); song_slot = -1; message("DEMO CHARGEE"); } return true; }
  return false;
}
static void song_key(int k) {
  if (naming == 2) {
    int len = slen(s_name, 16), c = key_char(k);
    if (k == KEY_ENTER || k == KEY_ESC) naming = 0;
    else if (k == KEY_BACKSPACE && len) s_name[len - 1] = 0;
    else if (c && c != ',' && len < 16) { s_name[len] = (char)c; s_name[len + 1] = 0; }
    dirty = 1;
    return;
  }
  int c = ctrl() ? key_char(k) : 0, big = shift() ? 10 : 1;
  if (c && song_action(c)) return;
  if (song_pane == 2 && k == KEY_ENTER) { song_action('L'); return; }
  if (k == KEY_TAB) { song_pane = (song_pane + (shift() ? 2 : 1)) % 3; return; }
  if (song_pane == 0) {
    switch (k) {
      case KEY_UP: if (cur_ord > 0) cur_ord--; break;
      case KEY_DOWN: if (cur_ord < n_orders - 1) cur_ord++; break;
      case KEY_PGUP: cur_ord = clampi(cur_ord - 16, 0, n_orders - 1); break;
      case KEY_PGDN: cur_ord = clampi(cur_ord + 16, 0, n_orders - 1); break;
      case KEY_LEFT: orders[cur_ord] = (uint8_t)clampi(orders[cur_ord] - big, 0, NPAT - 1); dirty = 1; break;
      case KEY_RIGHT: orders[cur_ord] = (uint8_t)clampi(orders[cur_ord] + big, 0, NPAT - 1); dirty = 1; break;
      case KEY_INSERT:
        if (n_orders < MT_ORDERS) {
          for (int i = n_orders; i > cur_ord + 1; i--) orders[i] = orders[i - 1];
          orders[cur_ord + 1] = orders[cur_ord]; n_orders++; cur_ord++; dirty = 1;
        }
        break;
      case KEY_DELETE:
        if (n_orders > 1) {
          for (int i = cur_ord; i < n_orders - 1; i++) orders[i] = orders[i + 1];
          n_orders--; if (cur_ord >= n_orders) cur_ord = n_orders - 1;
          if (restart >= n_orders) restart = 0;
          dirty = 1;
        }
        break;
      case KEY_ENTER: page = PG_PAT; cur_row = 0; break;
    }
  } else if (song_pane == 1) {
    int d = k == KEY_LEFT ? -big : k == KEY_RIGHT ? big : 0;
    if (k == KEY_UP) song_field = (song_field + SF_COUNT - 1) % SF_COUNT;
    else if (k == KEY_DOWN) song_field = (song_field + 1) % SF_COUNT;
    else if (k == KEY_ENTER && song_field == SF_NAME) naming = 2;
    else if (d) {
      switch (song_field) {
        case SF_BPM: s_bpm = clampi(s_bpm + d, 32, 255); break;
        case SF_SPEED: s_speed = clampi(s_speed + (d > 0 ? 1 : -1), 1, 31); break;
        case SF_MASTER: s_master = clampi(s_master + d, 0, 64); mt_master(s_master); break;
        case SF_RESTART: restart = clampi(restart + (d > 0 ? 1 : -1), 0, n_orders - 1); break;
        case SF_LAYOUT: layout = !layout; NV[4] = (uint8_t)layout; return;
        case SF_VELO: velo_off = !velo_off; NV[6] = (uint8_t)((NV[6] & ~1) | velo_off); return;
      }
      if (song_field == SF_BPM || song_field == SF_SPEED) mt_set_tempo(s_speed, s_bpm);
      dirty = 1;
    }
  } else {
    if (k == KEY_UP) slot_sel = (slot_sel + NV_SLOTS - 1) % NV_SLOTS;
    else if (k == KEY_DOWN) slot_sel = (slot_sel + 1) % NV_SLOTS;
  }
}

// ================================================================ help page
static const char *const help_text[] = {
  "*CLAVIER",
  "F1 F2 F3 F4      MOTIFS/SONS/MORCEAU/AIDE",
  "ENTREE MAJ+ENTREE ECHAP  MORCEAU/MOTIF/STOP",
  "F5 F6  F7 F8     OCTAVE -/+   PAS -/+",
  "F11 F12 PAVE -/+ INSTRUMENT -/+",
  "ESPACE           MODE EDITION (BANDE ROUGE)",
  "PIANO            RANGEES DU BAS ET DU HAUT",
  "& (TOUCHE 1)     NOTE COUPEE ===",
  "0-9 A-F          INSTRUMENT, VOLUME, EFFET",
  "SUPPR INSER RET.AR. EFFACE/INSERE/RETIRE",
  "CTRL+FLECHES     POSITION -/+   MOTIF -/+",
  "CTRL+C X V       PISTE  ALT+C X V  MOTIF",
  "CTRL+M  CTRL+O   MUET / SOLO   CTRL+F SUIVI",
  "CTRL+S           SAUVE (L'OSD ECRIT LA SD)",
  "*SOURIS",
  "FLECHES DES      +/- (CLIC DROIT: INVERSE),",
  "COMPTEURS        MAINTENIR POUR REPETER",
  "VALEURS          CLIC +1, DROIT -1, MOLETTE",
  "VUMETRES         CLIC: PISTE   DROIT: MUET",
  "NOM DE PISTE     CLIC: MUET    DROIT: SOLO",
  "CASE DU MOTIF    CLIC: CURSEUR DROIT: ECOUTE",
  "*EFFETS (COLONNE ORANGE)",
  "0XY ARPEGE  1XX/2XX MONTEE/DESC.  3XX PORTA",
  "4XY VIBRATO  8XX PANO  9XX RAPPORT/DEBUT",
  "AXY VOL +X -Y  BXX SAUT  CXX VOL  DXX FIN",
  "E1X E2X FIN  E9X REDECL. ECX COUPE EDX RETARD",
  "FXX VITESSE (<20) OU BPM",
};
static void draw_help(void) {
  box(0, 2, 79, 28, P_SUNK);
  print(0, 0, "MONOTRACKER - AIDE", C_YELLOW);
  print(61, 0, "CLIC OU F1: RETOUR", C_GREY);
  for (unsigned i = 0; i < sizeof help_text / sizeof help_text[0] && i < 27; i++) {
    const char *s = help_text[i];
    if (*s == '*') print(1, 2 + i, s + 1, C_YELLOW);
    else print(1, 2 + i, s, C_WHITE);
  }
  zone(0, 0, 79, 29, Z_HELP, 0);
}

// ================================================================ main loop
static int help_back;                            // page shown before the help
static int cur_ins(void) {
  if (page == PG_INS) return ins_sel;
  return mt_track_type(cur_track) == MT_TYPE_PSG ? ins_psg : ins_pcm;
}
static void set_cur_ins(int v) {
  v = clampi(v, 1, MT_INSTRUMENTS - 1);
  if (page == PG_INS) { ins_sel = v; graphs_dirty = 1; }
  else if (mt_track_type(cur_track) == MT_TYPE_PSG) ins_psg = v; else ins_pcm = v;
}
static void goto_page(int p) {
  if (p == PG_INS && page != PG_INS) { ins_sel = mt_track_type(cur_track) == MT_TYPE_PSG ? ins_psg : ins_pcm; graphs_dirty = 1; }
  if (p == PG_HELP && page != PG_HELP) help_back = page;
  page = p; naming = 0;
}
static void quick_save(void) {
  int slot = song_slot;
  if (slot < 0) for (int i = 0; i < NV_SLOTS; i++) if (!slot_size(i)) { slot = i; break; }
  if (slot < 0) message("NVRAM: AUCUN EMPLACEMENT LIBRE (MORCEAU)"); else song_save(slot);
}
static void stop_all(void) { play(0); for (int i = 0; i < 512; i++) jam_track[i] = -1; }
static void spin(int k, int d) {
  switch (k) {
    case SP_POS: cur_ord = clampi(cur_ord + d, 0, n_orders - 1); break;
    case SP_PAT: orders[cur_ord] = (uint8_t)clampi(orders[cur_ord] + d, 0, NPAT - 1); dirty = 1; break;
    case SP_LEN:
      n_orders = clampi(n_orders + d, 1, MT_ORDERS);
      if (cur_ord >= n_orders) cur_ord = n_orders - 1;
      if (restart >= n_orders) restart = 0;
      dirty = 1; break;
    case SP_BPM: s_bpm = clampi(s_bpm + d, 32, 255); mt_set_tempo(s_speed, s_bpm); dirty = 1; break;
    case SP_SPEED: s_speed = clampi(s_speed + d, 1, 31); mt_set_tempo(s_speed, s_bpm); dirty = 1; break;
    case SP_VOL: s_master = clampi(s_master + d, 0, 64); mt_master(s_master); dirty = 1; break;
    case SP_OCT: octave = clampi(octave + d, 0, 7); break;
    case SP_STEP: step = clampi(step + d, 0, 16); break;
    case SP_INS: set_cur_ins(cur_ins() + d); break;
  }
}
static void press_button(int b) {
  switch (b) {
    case BT_PLAY: play(play_mode == 1 ? 0 : 1); break;
    case BT_PATT: play(play_mode == 2 ? 0 : 2); break;
    case BT_STOP: stop_all(); break;
    case BT_EDIT: edit_mode = !edit_mode; break;
    case BT_FOLLOW: follow = !follow; break;
    case BT_SAVE: quick_save(); break;
    case BT_PG_PAT: goto_page(PG_PAT); break;
    case BT_PG_INS: goto_page(PG_INS); break;
    case BT_PG_SONG: goto_page(PG_SONG); break;
    case BT_PG_HELP: goto_page(PG_HELP); break;
  }
}
static void setting_change(int f, int d) {
  switch (f) {
    case SF_BPM: s_bpm = clampi(s_bpm + d, 32, 255); mt_set_tempo(s_speed, s_bpm); break;
    case SF_SPEED: s_speed = clampi(s_speed + (d > 0 ? 1 : -1), 1, 31); mt_set_tempo(s_speed, s_bpm); break;
    case SF_MASTER: s_master = clampi(s_master + d, 0, 64); mt_master(s_master); break;
    case SF_RESTART: restart = clampi(restart + (d > 0 ? 1 : -1), 0, n_orders - 1); break;
    case SF_LAYOUT: layout = !layout; NV[4] = (uint8_t)layout; return;
    case SF_VELO: velo_off = !velo_off; NV[6] = (uint8_t)((NV[6] & ~1) | velo_off); return;
    default: return;
  }
  dirty = 1;
}
static void solo(int t) {
  bool only = true;
  for (int i = 0; i < MT_TRACKS; i++) if (i != t && !mt_muted(i)) only = false;
  for (int i = 0; i < MT_TRACKS; i++) mt_mute(i, only ? false : i != t);
}

// a click (button 1 left, 2 right) or a repeat while it is held (rep = true)
static bool zone_repeats(const zone_t *z) {
  return (z->id == Z_SPIN && z->arg % 4 < 2) || (z->id == Z_PARAM && (z->arg & 0x100)) ||
         z->id == Z_ORDPAT || (z->id == Z_SETTING && z->arg != SF_NAME && z->arg != SF_LAYOUT && z->arg != SF_VELO);
}
static void mouse_click(const zone_t *z, int btn, int cx) {
  int big = shift() ? 10 : 1, d = btn == 2 ? -big : big;
  switch (z->id) {
    case Z_SPIN:
      if (z->arg % 4 == 0) spin(z->arg / 4, btn == 2 ? -big : big);        // up (right click: down)
      if (z->arg % 4 == 1) spin(z->arg / 4, btn == 2 ? big : -big);        // down
      break;
    case Z_BTN: if (btn == 1) press_button(z->arg); break;
    case Z_SCOPE:
      if (btn == 2) mt_mute(z->arg, !mt_muted(z->arg));
      else { cur_track = z->arg; cur_field = 0; }
      break;
    case Z_TRACK: if (btn == 2) solo(z->arg); else mt_mute(z->arg, !mt_muted(z->arg)); break;
    case Z_ROWS: break;
    case Z_CELL: {
      int i = z->arg & 15, r = cur_row + (z->arg >> 4) - 64, fx = cx - TRACK_X(i);
      if (r < 0 || r >= MT_ROWS) break;
      cur_track = track_view() + i; cur_row = r; cur_field = fx < 3 ? 0 : fx - 2;
      if (btn == 2) {                                   // right click: hear the note of the cell
        uint32_t c = pat(cur_pat())[r * MT_TRACKS + cur_track];
        int n = MT_C_NOTE(c), in = MT_C_INST(c) ? MT_C_INST(c) : cur_ins();
        if (n >= 1 && n <= 96) { jam(0, cur_track, n, in); jam_track[0] = -1; }
      }
      break;
    }
    case Z_PARAM: {
      const uint8_t *l; int n = plist(&l), i = z->arg & 0xFF;
      if (i >= n) break;
      ins_field = i;
      if (!(z->arg & 0x100)) break;
      if (l[i] == 0) naming = 1;
      else param_change(l[i], d);
      break;
    }
    case Z_ORDER: cur_ord = z->arg; song_pane = 0; break;
    case Z_ORDPAT: cur_ord = z->arg; song_pane = 0; orders[cur_ord] = (uint8_t)clampi(orders[cur_ord] + d, 0, NPAT - 1); dirty = 1; break;
    case Z_SETTING:
      song_pane = 1; song_field = z->arg;
      if (z->arg == SF_NAME) naming = 2; else setting_change(z->arg, d);
      break;
    case Z_SLOT: slot_sel = z->arg; song_pane = 2; break;
    case Z_SONGBTN: if (btn == 1) { if (z->arg != SB_SAVE) song_pane = 2; song_action(songbtn_keys[z->arg]); } break;
    case Z_HELP: goto_page(help_back); break;
  }
}
static void mouse_wheel(const zone_t *z, int w) {
  if (!z) return;
  switch (z->id) {
    case Z_SPIN: spin(z->arg / 4, w); break;
    case Z_CELL: case Z_ROWS: case Z_TRACK: cur_row = clampi(cur_row - w * (shift() ? 4 : 1), 0, MT_ROWS - 1); break;
    case Z_PARAM: { const uint8_t *l; int n = plist(&l), i = z->arg & 0xFF; if (i < n && l[i]) { ins_field = i; param_change(l[i], w); } break; }
    case Z_ORDER: cur_ord = clampi(cur_ord - w, 0, n_orders - 1); break;
    case Z_ORDPAT: orders[z->arg] = (uint8_t)clampi(orders[z->arg] + w, 0, NPAT - 1); dirty = 1; break;
    case Z_SETTING: song_field = z->arg; song_pane = 1; setting_change(z->arg, w); break;
    case Z_SLOT: slot_sel = clampi(slot_sel - w, 0, NV_SLOTS - 1); break;
    case Z_SCOPE: cur_track = clampi(cur_track - w, 0, MT_TRACKS - 1); break;
  }
}
static int hold_btn, hold_timer, hold_cx, hold_cy;
static void mouse_update(void) {
  if (!mc_mouse_present()) { mc_sprite_hide(0); return; }
  int mx = mc_mouse_x(), my = mc_mouse_y(), cx = mx / 8, cy = my / 8;
  mc_sprite(0, mx, my, G_PTR, 2, 2, P_POINTER, 7); mc_sprite_zoom(0, 0x200, 0x100);
  if (naming && (mc_mouse_pressed() & (MOUSE_LEFT | MOUSE_RIGHT))) naming = 0;   // a click ends the name
  const zone_t *z = zone_at(cx, cy);
  uint32_t pr = mc_mouse_pressed();
  int btn = pr & MOUSE_LEFT ? 1 : pr & MOUSE_RIGHT ? 2 : 0;
  if (btn && z) {
    mouse_click(z, btn, cx);
    hold_btn = zone_repeats(z) ? btn : 0; hold_timer = 18; hold_cx = cx; hold_cy = cy;
  } else if (hold_btn) {
    if (!(mc_mouse_buttons() & (hold_btn == 1 ? MOUSE_LEFT : MOUSE_RIGHT))) hold_btn = 0;
    else if (--hold_timer == 0) { hold_timer = 4; const zone_t *h = zone_at(hold_cx, hold_cy); if (h && zone_repeats(h)) mouse_click(h, hold_btn, hold_cx); }
  }
  if (mc_mouse_wheel()) mouse_wheel(z, mc_mouse_wheel());
}

static void global_key(int k) {
  if (naming) {
    if (page == PG_INS) instrument_key(k); else if (page == PG_SONG) song_key(k);
    return;
  }
  switch (k) {
    case KEY_F1: goto_page(PG_PAT); return;
    case KEY_F2: goto_page(PG_INS); return;
    case KEY_F3: goto_page(PG_SONG); return;
    case KEY_F4: goto_page(page == PG_HELP ? help_back : PG_HELP); return;
    case KEY_F5: if (octave > 0) octave--; return;
    case KEY_F6: if (octave < 7) octave++; return;
    case KEY_F7: if (step > 0) step--; return;
    case KEY_F8: if (step < 16) step++; return;
    case KEY_F11: case KEY_F12: case KEY_KPMINUS: case KEY_KPPLUS:
      set_cur_ins(cur_ins() + (k == KEY_F11 || k == KEY_KPMINUS ? -1 : 1));
      return;
    case KEY_ESC: stop_all(); return;
  }
  if (k == KEY_ENTER && !(page == PG_INS && ins_field <= 1) && !(page == PG_SONG && (song_pane == 2 || (song_pane == 0 && !shift()) || (song_pane == 1 && song_field == SF_NAME)))) {
    play(play_mode ? 0 : shift() ? 2 : 1);
    return;
  }
  if (k == KEY_S && ctrl() && page != PG_SONG && key_char(k) == 'S') { quick_save(); return; }
  switch (page) {
    case PG_PAT: pattern_key(k); break;
    case PG_INS: instrument_key(k); break;
    case PG_SONG: song_key(k); break;
  }
}

static void key_down(int k) {
  global_key(k);
  if (repeatable(k)) { rep_key = k; rep_timer = 18; }
}
static void key_up(int k) {
  if (k == rep_key) rep_key = 0;
  int t = jam_track[k & 511];
  if (t >= 0) {
    jam_track[k & 511] = -1;
    bool other = false;                         // the track may now be played by another key
    for (int i = 0; i < 512; i++) if (jam_track[i] == t) other = true;
    if (!other) mt_note_off(t);
  }
}

// ================================================================ MIDI keyboard
// Notes play and write like the piano of the computer keyboard (the velocity gives the volume);
// the eight knobs (CC 21 to 28, as a Novation Launchkey sends them) set eight settings of the sound
// shown on the sound page.
static int8_t midi_track[128];                   // track sounding for each MIDI note, -1 none
static int midi_seen;
static const uint8_t knob_param[8] = { 8, 9, 10, 11, 6, 12, 13, 3 };   // attack, decay, sustain, release, volume, vib x2, duty
static const uint8_t knob_max[8] = { 64, 64, 64, 64, 64, 32, 32, 255 };
static void midi_note_on(int mn, int vel) {
  int n = clampi(mn - 11, 1, 96), vol = (vel * 64 + 63) / 127;
  if (page == PG_INS) {
    mt_instrument_t *in = &ins[ins_sel];
    if (ins_empty(in)) return;
    int t = free_track(in->type);
    jam_vol(0, t, n, ins_sel, vol); jam_track[0] = -1;
    midi_track[mn] = (int8_t)t;
    return;
  }
  int t = cur_track, in = mt_track_type(t) == MT_TYPE_PSG ? ins_psg : ins_pcm;
  jam_vol(0, t, n, in, vol); jam_track[0] = -1;
  midi_track[mn] = (int8_t)t;
  if (page == PG_PAT && edit_mode) {
    uint32_t c = get_cell(cur_row, t) & ~0x7FFFFu;           // keeps the effect
    c |= (uint32_t)n | ((uint32_t)in << 7);
    if (!velo_off) c |= (uint32_t)(vol + 1) << 12;
    else c |= get_cell(cur_row, t) & (0x7Fu << 12);
    set_cell(cur_row, t, c); advance();
  }
}
static void midi_note_off(int mn) {
  int t = midi_track[mn];
  if (t < 0) return;
  midi_track[mn] = -1;
  for (int i = 0; i < 128; i++) if (midi_track[i] == t) return;   // another key still holds the track
  for (int k = 0; k < 512; k++) if (jam_track[k] == t) return;
  mt_note_off(t);
}
static void midi_cc(int cc, int v) {
  if (cc < 21 || cc > 28 || page != PG_INS) return;
  mt_instrument_t *in = &ins[ins_sel];
  int pi = knob_param[cc - 21];
  if (pi == 3 && in->type == MT_TYPE_PCM) pi = 7;            // PCM: panning instead of the duty
  const param_t *p = &params[pi];
  int max = pi == 7 ? 255 : knob_max[cc - 21], lo = pi == 3 ? 1 : 0;
  ((uint8_t *)in)[p->off] = (uint8_t)(lo + (max - lo) * v / 127);
  const uint8_t *l; int n = plist(&l);
  for (int i = 0; i < n; i++) if (l[i] == pi) ins_field = i;
  dirty = 1; graphs_dirty = 1;
  char s[24]; param_value(in, pi, s, sizeof s);
  snprintf(line, sizeof line, "BOUTON %d: %s %s", cc - 20, p->label, s);
  message(line);
}
static void midi_poll(void) {
  int e;
  while ((e = mc_midi_event()) >= 0) {
    if (!midi_seen) { midi_seen = 1; message("CLAVIER MIDI BRANCHE"); }
    int st = MIDI_STATUS(e);
    if (st == MIDI_NOTE_ON) midi_note_on(MIDI_DATA1(e), MIDI_DATA2(e));
    else if (st == MIDI_NOTE_OFF) midi_note_off(MIDI_DATA1(e));
    else if (st == MIDI_CC) midi_cc(MIDI_DATA1(e), MIDI_DATA2(e));
  }
}

int main(void) {
  mc_init();
  video_init();
  if (mc_hw_version() < 5) {                  // an older core has no 640-pixel mode: say so and stop
    mc_video_mode(384, 240);
    cls();
    print(2, 12, "MONOTRACKER DEMANDE LE CORE", C_YELLOW);
    print(2, 14, "MONOCORE VERSION 5 (HAUTE RESOLUTION)", C_WHITE);
    flush();
    for (;;) mc_frame();
  }
  mt_init(mtbank_file);
  for (int i = 0; i < 512; i++) jam_track[i] = -1;
  for (int i = 0; i < 128; i++) midi_track[i] = -1;
  nv_check();
  int last = NV[5];
  if (last < NV_SLOTS && slot_size(last) && song_load((const void *)(NVRAM_BASE + slot_off(last)), slot_size(last))) song_slot = last;
  else { song_load(demo_file, demo_size); song_slot = -1; }
  if (mc_hw_version() < 2) message("CE CORE N'A PAS DE CLAVIER (VERSION 1)");
  else if (mc_hw_version() < 3) message("CORE SANS SOURIS (VERSION 2) - F4 POUR L'AIDE");
  else message("MONOTRACKER - F4 POUR L'AIDE");
  for (;;) {
    mc_frame();
    int e;
    while ((e = mc_key_event()) >= 0) {
      int k = e & 0x1FF;
      uint32_t bit = 1u << (k & 31);
      if (e & 0x200) {
        if (kdown[k >> 5] & bit) continue;         // repeat of the keyboard: ours is used instead
        kdown[k >> 5] |= bit;
        key_down(k);
      } else {
        kdown[k >> 5] &= ~bit;
        key_up(k);
      }
    }
    midi_poll();
    if (rep_key) {
      if (!mc_key(rep_key)) rep_key = 0;
      else if (--rep_timer == 0) { rep_timer = 3; global_key(rep_key); }
    }
    if (msg_timer > 0) msg_timer--;
    if (confirm_timer > 0 && --confirm_timer == 0) confirm_key = 0;
    if (play_mode && !mt_playing()) play_mode = 0;
    if (play_mode && follow) {
      if (play_mode == 1) cur_ord = mt_order();
      cur_row = mt_row();
    }
    for (int t = 0; t < MT_TRACKS; t++) {
      int l = mt_level(t);
      vu[t] = (uint8_t)(l > vu[t] ? l : vu[t] > 10 ? vu[t] - 10 : 0);
    }
    mouse_update();
    if (play_mode) play_frames++;
    cls();
    nzones = 0;
    if (page == PG_HELP) draw_help();
    else {
      draw_top();
      switch (page) {
        case PG_PAT: draw_pattern(); break;
        case PG_INS: draw_instrument(); break;
        default: draw_song(); break;
      }
      draw_info();
    }
    flush();
    memcpy(zones_prev, zones, sizeof(zone_t) * nzones); nzones_prev = nzones;
  }
}
