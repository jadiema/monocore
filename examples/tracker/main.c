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

#define COLS 48
#define LINES 30
#define NPAT 24
#define PAT_CELLS (MT_ROWS * MT_TRACKS)
#define PATS ((uint32_t *)CHRRAM_BASE)
#define FONT_OFF 0x18000u
#define G_SOLID 96            // glyphs after the 96 of the font
#define G_VU 97               // 97-104: VU bar of 1 to 8 pixels
#define G_ARROW 105           // small play arrow
#define G_GRAPH 112           // two graphs of GW x GH tiles
#define GW 13
#define GH 5

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

// colours: text palettes 0-7, backgrounds 8-11, graphs 12
enum { C_WHITE, C_GREY, C_CYAN, C_GREEN, C_ORANGE, C_YELLOW, C_RED, C_BLUE };
enum { B_CURSOR = 8, B_EDIT, B_FIELD, B_BEAT };
#define P_GRAPH 12

// ================================================================ song being edited
static mt_instrument_t ins[MT_INSTRUMENTS];
static uint8_t orders[MT_ORDERS];
static int n_orders = 1, restart, s_speed = 6, s_bpm = 125, s_master = 48;
static char s_name[17];
static int song_slot = -1, dirty;

static uint32_t *pat(int p) { return PATS + p * PAT_CELLS; }
static const uint32_t *src_pattern(void *ctx, int p) { (void)ctx; return p < NPAT ? pat(p) : 0; }

// ================================================================ screen
static uint16_t txt[LINES][COLS], txt_old[LINES][COLS];   // glyph | palette << 12
static uint16_t bgr[LINES][COLS], bgr_old[LINES][COLS];
static char line[96];

static void cls(void) {
  for (int y = 0; y < LINES; y++) for (int x = 0; x < COLS; x++) { txt[y][x] = 0; bgr[y][x] = 0; }
}
static void putg(int x, int y, int g, int pal) {
  if (x >= 0 && x < COLS && y >= 0 && y < LINES) txt[y][x] = (uint16_t)(g | (pal << 12));
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
static void bar(int x0, int x1, int y, int pal) {
  for (int x = x0; x <= x1; x++) if (x >= 0 && x < COLS && y >= 0 && y < LINES) bgr[y][x] = (uint16_t)(G_SOLID | (pal << 12));
}
static void flush(void) {
  volatile uint32_t *m0 = mc_tilemap(0), *m1 = mc_tilemap(1);
  for (int y = 0; y < LINES; y++)
    for (int x = 0; x < COLS; x++) {
      uint16_t t = txt[y][x], b = bgr[y][x];
      if (t != txt_old[y][x]) { txt_old[y][x] = t; m0[y * 64 + x] = TILE(t & 0xFFF, t >> 12); }
      if (b != bgr_old[y][x]) { bgr_old[y][x] = b; m1[y * 64 + x] = TILE(b & 0xFFF, b >> 12); }
    }
}

static const uint16_t pal_fg[8] = {
  RGB555(240, 240, 240), RGB555(110, 116, 140), RGB555(90, 210, 240), RGB555(120, 230, 110),
  RGB555(250, 160, 60), RGB555(250, 225, 90), RGB555(250, 80, 80), RGB555(120, 140, 250) };
static const uint16_t pal_bg[4] = { RGB555(40, 60, 140), RGB555(110, 30, 40), RGB555(80, 110, 220), RGB555(26, 28, 46) };

static void video_init(void) {
  mc_video_mode(384, 240);
  mc_chr_upload(FONT_OFF, font_chr, 96 * 32);
  static uint8_t g[32];
  // solid tile (colour 3) and VU bars (colour 1, from the bottom)
  memset(g, 0x33, 32); mc_chr_upload(FONT_OFF + G_SOLID * 32, g, 32);
  for (int k = 1; k <= 8; k++) {
    memset(g, 0, 32);
    for (int y = 8 - k; y < 8; y++) for (int x = 1; x < 4; x++) g[y * 4 + x] = 0x11;
    mc_chr_upload(FONT_OFF + (G_VU + k - 1) * 32, g, 32);
  }
  memset(g, 0, 32);
  for (int y = 1; y < 7; y++) { int w = y < 4 ? y : 7 - y; for (int x = 0; x <= w * 2 && x < 8; x++) g[y * 4 + x / 2] |= (x & 1) ? 0x10 : 0x01; }
  mc_chr_upload(FONT_OFF + G_ARROW * 32, g, 32);
  for (int p = 0; p < 16; p++) for (int i = 0; i < 16; i++) PALETTE[p * 16 + i] = 0;
  for (int p = 0; p < 8; p++) { PALETTE[p * 16 + 1] = pal_fg[p]; PALETTE[p * 16 + 2] = RGB555(8, 8, 16); }
  for (int p = 0; p < 4; p++) PALETTE[(8 + p) * 16 + 3] = pal_bg[p];
  PALETTE[P_GRAPH * 16 + 1] = RGB555(250, 225, 90);
  PALETTE[P_GRAPH * 16 + 2] = RGB555(60, 64, 96);
  PALETTE[P_GRAPH * 16 + 3] = RGB555(110, 90, 40);
  PALETTE[15 * 16 + 1] = RGB555(12, 14, 26);
  mc_background(15 * 16 + 1);
  mc_layer(0, MC_CHR(FONT_OFF), MC_TILE8 | MC_4BPP, 64, 64, 2);
  mc_layer(1, MC_CHR(FONT_OFF), MC_TILE8 | MC_4BPP, 64, 64, 0);
  mc_tilemap_fill(0, 0); mc_tilemap_fill(1, 0);
  memset(txt_old, 0, sizeof txt_old); memset(bgr_old, 0, sizeof bgr_old);
  mc_display(DISP_ON | DISP_LAYER(0) | DISP_LAYER(1));
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
static int ins_sel = 1, ins_field, ins_scroll;   // instrument page
static int song_pane, song_field, slot_sel, ord_scroll;
static int naming;                               // editing a name: 1 instrument, 2 song
static int play_mode;                            // 0 stopped, 1 song, 2 pattern
static uint8_t one_order[1];
static char msg[COLS + 1];
static int msg_timer, confirm_key, confirm_timer;
static uint8_t vu[MT_TRACKS];
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
    layout = NV[4] & 1;
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
  snprintf(line, sizeof line, "SAUVE EMPLACEMENT %d (%u OCTETS) - OUVREZ L'OSD", k + 1, (unsigned)n);
  message(line);
}

// ================================================================ playback
static void play(int mode) {
  if (mode == 0) { song_stop(); return; }
  mt_source_t s = { .ins = ins, .orders = orders, .n_orders = n_orders, .restart = restart, .pattern = src_pattern,
                    .ctx = 0, .speed = s_speed, .bpm = s_bpm, .master = s_master };
  if (mode == 2) { one_order[0] = (uint8_t)cur_pat(); s.orders = one_order; s.n_orders = 1; s.restart = 0; }
  mt_play_source(&s, mode == 2 ? 0 : cur_ord, mode == 2 ? cur_row : 0, mode == 2);
  play_mode = mode;
}
static void jam(int key, int track, int note, int inst) {
  if (note < 1 || note > 96 || inst < 1 || ins[inst].type != mt_track_type(track)) return;
  mt_note(track, note, &ins[inst], s_master);
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
static void draw_tabs(void) {
  print(0, 0, "MONOTRACKER", C_YELLOW);
  static const char *const tabs[4] = { "F1 MOTIF", "F2 SON", "F3 MORCEAU", "F4 AIDE" };
  int x = 13;
  for (int i = 0; i < 4; i++) {
    int n = (int)strlen(tabs[i]);
    if (page == i) bar(x - 1, x + n, 0, B_CURSOR);
    print(x, 0, tabs[i], page == i ? C_WHITE : C_GREY);
    x += n + 1;
  }
}
static void draw_message(const char *hint) {
  if (msg_timer > 0) print(0, LINES - 1, msg, C_YELLOW);
  else print(0, LINES - 1, hint, C_GREY);
}
static void ins_label(int i, char *s) {   // "0A KICK"
  s[0] = hexd[i >> 4]; s[1] = hexd[i & 15]; s[2] = ' ';
  memcpy(s + 3, ins[i].name, 10); s[13] = 0;
  for (int k = 3; k < 13; k++) if (!s[k]) { s[k] = 0; break; }
}

// ================================================================ pattern page
static const int field_x[8] = { 0, 3, 4, 5, 6, 7, 8, 9 };
static const int field_w[8] = { 3, 1, 1, 1, 1, 1, 1, 1 };

static void draw_pattern(void) {
  int p = cur_pat(), view = cur_track & ~3;
  bool play_here = play_mode && (play_mode == 2 || orders[mt_order()] == p);
  draw_tabs();
  snprintf(line, sizeof line, "POS %02X/%02X  MOTIF %02X  BPM %3d VIT %d", cur_ord, n_orders, p, s_bpm, s_speed);
  print(0, 1, line, C_WHITE);
  if (play_mode) { putg(38, 1, G_ARROW, C_GREEN); print(40, 1, play_mode == 1 ? "MORCEAU" : "MOTIF", C_GREEN); }
  snprintf(line, sizeof line, "OCT %d  PAS %d", octave, step);
  print(0, 2, line, C_WHITE);
  char s[16];
  ins_label(ins_psg, s); print(14, 2, "PSG", C_GREY); print(18, 2, s, mt_track_type(cur_track) == MT_TYPE_PSG ? C_CYAN : C_GREY);
  ins_label(ins_pcm, s); print(29, 2, "PCM", C_GREY); print(33, 2, s, mt_track_type(cur_track) == MT_TYPE_PCM ? C_CYAN : C_GREY);
  if (edit_mode) { bar(44, 47, 2, B_EDIT); print(44, 2, "EDIT", C_WHITE); }
  // the 16 tracks: number, VU meter
  for (int t = 0; t < MT_TRACKS; t++) {
    int x = t * 3;
    if (t >= view && t < view + 4) bar(x, x + 2, 3, B_BEAT);
    char c = t < 8 ? (char)('1' + t) : (char)('A' + t - 8);
    putg(x, 3, c - 32, mt_muted(t) ? C_RED : t < 8 ? C_CYAN : C_GREEN);
    int v = vu[t] * 8 / 256;
    if (v) putg(x + 1, 3, G_VU + v - 1, vu[t] > 200 ? C_ORANGE : C_GREEN);
  }
  // headers of the 4 tracks shown
  for (int i = 0; i < 4; i++) {
    int t = view + i, x = 3 + i * 11;
    snprintf(line, sizeof line, "%s %d", t < 8 ? "PSG" : "PCM", (t & 7) + 1);
    print(x, 4, line, t == cur_track ? C_WHITE : C_GREY);
    if (mt_muted(t)) print(x + 6, 4, "MUET", C_RED);
  }
  // rows
  const uint32_t *cells = pat(p);
  for (int y = 5; y < LINES - 1; y++) {
    int r = cur_row + y - 16;
    if (r < 0 || r >= MT_ROWS) continue;
    int prow = play_here ? mt_row() : -1;
    if (y == 16) bar(0, COLS - 1, y, edit_mode ? B_EDIT : B_CURSOR);
    else if (r == prow) bar(0, COLS - 1, y, B_FIELD);
    else if (r % 4 == 0) bar(0, COLS - 1, y, B_BEAT);
    s[0] = hexd[r >> 4]; s[1] = hexd[r & 15]; s[2] = 0;
    print(0, y, s, r % 16 == 0 ? C_YELLOW : r % 4 == 0 ? C_WHITE : C_GREY);
    for (int i = 0; i < 4; i++) {
      int t = view + i, x = 3 + i * 11;
      uint32_t c = cells[r * MT_TRACKS + t];
      int n = MT_C_NOTE(c), in = MT_C_INST(c), vo = MT_C_VOL(c), fx = MT_C_FX(c), pa = MT_C_PARAM(c);
      note_str(n, s); print(x, y, s, n ? C_WHITE : C_GREY);
      if (in) { s[0] = hexd[in >> 4]; s[1] = hexd[in & 15]; s[2] = 0; print(x + 3, y, s, ins[in].type == mt_track_type(t) ? C_CYAN : C_RED); }
      else print(x + 3, y, "..", C_GREY);
      if (vo) { s[0] = hexd[(vo - 1) >> 4]; s[1] = hexd[(vo - 1) & 15]; s[2] = 0; print(x + 5, y, s, C_GREEN); }
      else print(x + 5, y, "..", C_GREY);
      if (fx) { s[0] = hexd[fx - 1]; s[1] = hexd[pa >> 4]; s[2] = hexd[pa & 15]; s[3] = 0; print(x + 7, y, s, C_ORANGE); }
      else print(x + 7, y, "...", C_GREY);
      if (i < 3) putg(x + 10, y, '|' - 32, C_GREY);
      if (y == 16 && t == cur_track) bar(x + field_x[cur_field], x + field_x[cur_field] + field_w[cur_field] - 1, y, B_FIELD);
    }
  }
  const char *hint = "ENTREE JOUE  ESPACE EDIT  F4 AIDE";
  uint32_t c = cells[cur_row * MT_TRACKS + cur_track];
  if (cur_field >= 5 && MT_C_FX(c)) hint = fx_help[MT_C_FX(c) - 1];
  else if (cur_field >= 5) hint = "EFFET: 0-F PUIS PARAMETRE (F4: LISTE)";
  else if (cur_field >= 3) hint = "VOLUME DE LA NOTE: 00 A 40";
  else if (cur_field >= 1) hint = "INSTRUMENT (F2 POUR LE REGLER)";
  draw_message(hint);
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
static const char *const wave_names[4] = { "CARRE", "TRIANGLE", "DENT DE SCIE", "BRUIT" };

static int plist(const uint8_t **l) {
  if (ins[ins_sel].type == MT_TYPE_PCM) { *l = pcm_list; return sizeof pcm_list; }
  *l = psg_list; return sizeof psg_list;
}
static void param_value(const mt_instrument_t *in, int pi, char *s, int n) {
  const param_t *p = &params[pi];
  const uint8_t *b = (const uint8_t *)in;
  switch (p->kind) {
    case K_NAME: snprintf(s, n, "%.10s%s", in->name, naming == 1 ? "_" : ""); break;
    case K_TYPE: snprintf(s, n, "%s", in->type == MT_TYPE_PCM ? "PCM (ECHANTILLON)" : "PSG (OSCILLATEUR)"); break;
    case K_WAVE:
      if (in->type == MT_TYPE_PSG) snprintf(s, n, "%s", wave_names[in->wave & 3]);
      else { const mt_sample_t *sm = mt_bank_sample(in->wave); snprintf(s, n, "%02d %.12s", in->wave, sm ? sm->name : "?"); }
      break;
    case K_U8:
      if (pi == 3 && in->type == MT_TYPE_PCM) snprintf(s, n, "%s", in->duty == 1 ? "SANS BOUCLE" : "BOUCLE DE LA BANQUE");
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
  draw_tabs();
  snprintf(line, sizeof line, "SON %02X  %.10s", ins_sel, in->name);
  print(0, 1, line, C_WHITE);
  print(22, 1, in->type == MT_TYPE_PCM ? "PCM" : "PSG", in->type == MT_TYPE_PCM ? C_GREEN : C_CYAN);
  print(27, 1, "PIANO = ECOUTE", C_GREY);
  // list of the instruments
  if (ins_sel - 1 < ins_scroll) ins_scroll = ins_sel - 1;
  if (ins_sel - 1 >= ins_scroll + 27) ins_scroll = ins_sel - 27;
  for (int i = 0; i < 27; i++) {
    int k = ins_scroll + i + 1, y = 2 + i;
    if (k >= MT_INSTRUMENTS) break;
    ins_label(k, s);
    if (k == ins_sel) bar(0, 15, y, B_CURSOR);
    print(0, y, s, ins_empty(&ins[k]) ? C_GREY : C_WHITE);
    if (!ins_empty(&ins[k])) putg(15, y, (ins[k].type == MT_TYPE_PCM ? 'C' : 'P') - 32, ins[k].type == MT_TYPE_PCM ? C_GREEN : C_CYAN);
  }
  // parameters
  for (int i = 0; i < n; i++) {
    int y = 2 + i;
    const param_t *p = &params[l[i]];
    const char *lab = p->label;
    if (in->type == MT_TYPE_PCM && l[i] == 2) lab = "ECHANTILLON";
    if (in->type == MT_TYPE_PCM && l[i] == 3) lab = "BOUCLE";
    if (i == ins_field) bar(17, 34, y, naming ? B_EDIT : B_CURSOR);
    print(17, y, lab, i == ins_field ? C_WHITE : C_GREY);
    param_value(in, l[i], s, 19);
    printn(30, y, s, 17, i == ins_field ? C_YELLOW : C_WHITE);
  }
  // graphs, at the right of the short values
  if (graphs_dirty) { draw_graphs(in); graphs_dirty = 0; }
  show_graph(0, 17, LINES - 1 - GH);
  show_graph(1, 31, LINES - 1 - GH);
  draw_message(params[l[ins_field]].help);
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
enum { SF_NAME, SF_BPM, SF_SPEED, SF_MASTER, SF_RESTART, SF_LAYOUT, SF_COUNT };
static const char *const song_labels[SF_COUNT] = { "NOM", "BPM", "VITESSE", "VOLUME", "REPRISE", "CLAVIER" };
static const char *const song_help[SF_COUNT] = {
  "ENTREE POUR ECRIRE LE NOM DU MORCEAU", "TEMPO: 125 BPM = 50 TICKS PAR SECONDE",
  "TICKS PAR LIGNE (6 = UNE DOUBLE CROCHE)", "VOLUME GENERAL DU MORCEAU (0-64)",
  "POSITION REJOUEE APRES LA DERNIERE", "DISPOSITION DU CLAVIER (POUR LES LETTRES)" };

static void draw_song(void) {
  draw_tabs();
  print(0, 1, "MORCEAU", C_WHITE); print(8, 1, s_name, C_YELLOW);
  if (dirty) print(26, 1, "(MODIFIE)", C_ORANGE);
  static const char *const pane[3] = { "ORDRE", "REGLAGES", "NVRAM" };
  static const int px[3] = { 0, 12, 32 };
  for (int i = 0; i < 3; i++) {
    if (song_pane == i) bar(px[i], px[i] + 9, 3, B_CURSOR);
    print(px[i] + 1, 3, pane[i], song_pane == i ? C_WHITE : C_GREY);
  }
  // order list
  if (cur_ord < ord_scroll) ord_scroll = cur_ord;
  if (cur_ord >= ord_scroll + 23) ord_scroll = cur_ord - 22;
  for (int i = 0; i < 23; i++) {
    int o = ord_scroll + i, y = 5 + i;
    if (o >= n_orders) break;
    snprintf(line, sizeof line, "%02X MOTIF %02X", o, orders[o]);
    if (o == cur_ord) bar(0, 10, y, song_pane == 0 ? B_CURSOR : B_BEAT);
    print(0, y, line, o == restart ? C_CYAN : C_WHITE);
    if (play_mode == 1 && mt_order() == o) putg(11, y, G_ARROW, C_GREEN);
  }
  // settings
  for (int f = 0; f < SF_COUNT; f++) {
    int y = f == SF_NAME ? 5 : 6 + 2 * f;
    if (song_pane == 1 && song_field == f) { bar(12, 30, y, naming == 2 ? B_EDIT : B_CURSOR); if (f == SF_NAME) bar(12, 30, y + 1, naming == 2 ? B_EDIT : B_CURSOR); }
    print(13, y, song_labels[f], C_GREY);
    switch (f) {
      case SF_NAME: snprintf(line, sizeof line, "%.16s%s", s_name, naming == 2 ? "_" : ""); break;
      case SF_BPM: snprintf(line, sizeof line, "%d", s_bpm); break;
      case SF_SPEED: snprintf(line, sizeof line, "%d", s_speed); break;
      case SF_MASTER: snprintf(line, sizeof line, "%d", s_master); break;
      case SF_RESTART: snprintf(line, sizeof line, "%02X", restart); break;
      case SF_LAYOUT: snprintf(line, sizeof line, "%s", layout ? "QWERTY" : "AZERTY"); break;
    }
    print(f == SF_NAME ? 13 : 22, f == SF_NAME ? y + 1 : y, line, C_YELLOW);
  }
  // NVRAM slots
  for (int k = 0; k < NV_SLOTS; k++) {
    int y = 5 + 2 * k;
    if (song_pane == 2 && slot_sel == k) bar(32, 47, y, B_CURSOR);
    snprintf(line, sizeof line, "%d", k + 1);
    print(33, y, line, k == song_slot ? C_YELLOW : C_GREY);
    if (slot_size(k)) {
      const char *nm = mt_song_name((const void *)(NVRAM_BASE + slot_off(k)));
      char s[17]; for (int i = 0; i < 16; i++) s[i] = nm[i]; s[16] = 0;
      printn(35, y, s, 12, C_WHITE);
      snprintf(line, sizeof line, "%u OCTETS", (unsigned)slot_size(k));
      print(35, y + 1, line, C_GREY);
    } else print(35, y, "(VIDE)", C_GREY);
  }
  snprintf(line, sizeof line, "LIBRE %u", (unsigned)nv_free(-1));
  print(33, 22, line, C_GREEN);
  print(0, 26, "CTRL+S SAUVER  CTRL+L CHARGER  CTRL+D EFFACER", C_GREY);
  print(0, 27, "CTRL+E NOUVEAU  CTRL+R DEMO  TAB: CADRE", C_GREY);
  draw_message(song_pane == 0 ? "G/D: MOTIF  INSER/SUPPR  ENTREE: EDITER" :
               song_pane == 1 ? song_help[song_field] : "ENTREE OU CTRL+L CHARGE, CTRL+S SAUVE ICI");
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
  if (c == 'S') { int k2 = song_pane == 2 ? slot_sel : song_slot >= 0 ? song_slot : slot_sel;
                  if (!slot_size(k2) || k2 == song_slot || confirm('S', "REMPLACER CET EMPLACEMENT")) song_save(k2); return; }
  if (c == 'L' || (song_pane == 2 && k == KEY_ENTER)) {
    if (!slot_size(slot_sel)) { message("EMPLACEMENT VIDE"); return; }
    if (dirty && !confirm('L', "MODIFICATIONS PERDUES")) return;
    if (song_load((const void *)(NVRAM_BASE + slot_off(slot_sel)), slot_size(slot_sel))) { song_slot = slot_sel; NV[5] = (uint8_t)slot_sel; message("MORCEAU CHARGE"); }
    return;
  }
  if (c == 'D') { if (slot_size(slot_sel) && confirm('D', "EFFACER L'EMPLACEMENT")) { nv_delete(slot_sel); if (song_slot == slot_sel) song_slot = -1; message("EMPLACEMENT EFFACE"); } return; }
  // new song: Ctrl+E (Ctrl+N would open a window in the simulator's browser)
  if (c == 'E') { if (!dirty || confirm('E', "MODIFICATIONS PERDUES")) { song_new(); message("NOUVEAU MORCEAU"); } return; }
  if (c == 'R') { if (!dirty || confirm('R', "MODIFICATIONS PERDUES")) { song_load(demo_file, demo_size); song_slot = -1; message("DEMO CHARGEE"); } return; }
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
  "*PARTOUT",
  "F1 F2 F3 F4      MOTIFS / SONS / MORCEAU / AIDE",
  "ENTREE           JOUE LE MORCEAU (OU ARRETE)",
  "MAJ+ENTREE       JOUE LE MOTIF EN BOUCLE",
  "ECHAP            ARRETE TOUT",
  "F5 F6  F7 F8     OCTAVE -/+   PAS -/+",
  "F11 F12 (PAVE -/+) INSTRUMENT -/+",
  "*MOTIFS",
  "ESPACE           MODE EDITION (BANDE ROUGE)",
  "PIANO            RANGEE DU BAS ET RANGEE DU HAUT",
  "&  (TOUCHE 1)    NOTE COUPEE ===",
  "0-9 A-F          INSTRUMENT, VOLUME, EFFET",
  "SUPPR INSER RET.AR. EFFACE / INSERE / RETIRE",
  "TAB  FLECHES  PG.PREC/SUIV  DEPLACEMENT",
  "CTRL+FLECHES     POSITION -/+   MOTIF -/+",
  "CTRL+C X V       PISTE  ALT+C X V  MOTIF",
  "CTRL+M  CTRL+O   MUET / SOLO   CTRL+F SUIVI",
  "*EFFETS (COLONNE ORANGE)",
  "0XY ARPEGE  1XX/2XX MONTEE/DESC.  3XX PORTA",
  "4XY VIBRATO  8XX PANO  9XX RAPPORT/DEBUT",
  "AXY VOLUME +X -Y  BXX SAUT  CXX VOLUME  DXX FIN",
  "E1X E2X FIN  E9X REDECL.  ECX COUPE  EDX RETARD",
  "FXX VITESSE (<20) OU BPM",
  "*SONS ET MORCEAU",
  "HAUT/BAS CHAMP  GAUCHE/DROITE VALEUR (MAJ: X10)",
  "PG.PREC/SUIV INSTRUMENT  CTRL+C V X COPIE COLLE",
  "CTRL+S SAUVE EN NVRAM, L'OSD L'ECRIT SUR SD",
};
static void draw_help(void) {
  draw_tabs();
  for (unsigned i = 0; i < sizeof help_text / sizeof help_text[0] && i < 28; i++) {
    const char *s = help_text[i];
    if (*s == '*') print(0, 1 + i, s + 1, C_YELLOW);
    else print(1, 1 + i, s, C_WHITE);
  }
}

// ================================================================ main loop
static void global_key(int k) {
  if (naming) {
    if (page == PG_INS) instrument_key(k); else if (page == PG_SONG) song_key(k);
    return;
  }
  switch (k) {
    case KEY_F1: page = PG_PAT; return;
    case KEY_F2: page = PG_INS; graphs_dirty = 1; return;
    case KEY_F3: page = PG_SONG; return;
    case KEY_F4: page = PG_HELP; return;
    case KEY_F5: if (octave > 0) octave--; return;
    case KEY_F6: if (octave < 7) octave++; return;
    case KEY_F7: if (step > 0) step--; return;
    case KEY_F8: if (step < 16) step++; return;
    case KEY_F11: case KEY_F12: case KEY_KPMINUS: case KEY_KPPLUS: {
      int d = k == KEY_F11 || k == KEY_KPMINUS ? -1 : 1;
      if (page == PG_INS) { ins_sel = clampi(ins_sel + d, 1, MT_INSTRUMENTS - 1); graphs_dirty = 1; ins_field = 0; }
      else if (mt_track_type(cur_track) == MT_TYPE_PSG) ins_psg = clampi(ins_psg + d, 1, MT_INSTRUMENTS - 1);
      else ins_pcm = clampi(ins_pcm + d, 1, MT_INSTRUMENTS - 1);
      return;
    }
    case KEY_ESC: play(0); for (int i = 0; i < 512; i++) jam_track[i] = -1; return;
  }
  if (k == KEY_ENTER && !(page == PG_INS && ins_field <= 1) && !(page == PG_SONG && (song_pane == 2 || (song_pane == 0 && !shift()) || (song_pane == 1 && song_field == SF_NAME)))) {
    play(play_mode ? 0 : shift() ? 2 : 1);
    return;
  }
  if (k == KEY_S && ctrl() && page != PG_SONG && key_char(k) == 'S') {   // quick save
    int slot = song_slot;
    if (slot < 0) for (int i = 0; i < NV_SLOTS; i++) if (!slot_size(i)) { slot = i; break; }
    if (slot < 0) message("NVRAM: AUCUN EMPLACEMENT LIBRE (F3)"); else song_save(slot);
    return;
  }
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

int main(void) {
  mc_init();
  video_init();
  mt_init(mtbank_file);
  for (int i = 0; i < 512; i++) jam_track[i] = -1;
  nv_check();
  int last = NV[5];
  if (last < NV_SLOTS && slot_size(last) && song_load((const void *)(NVRAM_BASE + slot_off(last)), slot_size(last))) song_slot = last;
  else { song_load(demo_file, demo_size); song_slot = -1; }
  if (mc_hw_version() < 2) message("CE CORE N'A PAS DE CLAVIER (VERSION 1)");
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
    cls();
    switch (page) {
      case PG_PAT: draw_pattern(); break;
      case PG_INS: draw_instrument(); break;
      case PG_SONG: draw_song(); break;
      default: draw_help(); break;
    }
    flush();
  }
}
