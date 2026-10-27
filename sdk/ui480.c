// MONOCORE-1 "ui480": the 640 x 480 interface of the editors (see ui480.h and UI480.md).
// The text and panels follow MONOTRACKER (examples/monotracker), whose font is used here.
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "mc.h"
#include "ui480.h"
#include "font480.h"

#define BROWS 31                      // rows of panel tiles (16 x 16, shifted 8 pixels up)
#define BCOLS 40
#define T_PANEL 1                     // panel tiles: 1-16 closed (face drawn), 17-32 open
#define T_OPEN 17
enum { P_RAISED = 16, P_SUNK, P_PRESSED, P_BACK, P_POINTER = 24 };

static uint32_t chr_base;
static uint16_t txt[UI_ROWS][UI_COLS], txt_old[UI_ROWS][UI_COLS];   // glyph | palette << 8
static uint16_t pnl[BROWS][BCOLS], pnl_old[BROWS][BCOLS];           // tile | palette << 8
static ui_zone_t zones[256], zones_prev[256];
static int nzones, nzones_prev, pointer_shape;
int ui_layout;

#define NAVY   RGB555(18, 22, 44)
#define FACE   RGB555(160, 164, 176)
#define LIGHT  RGB555(236, 236, 244)
#define SHADOW RGB555(64, 66, 82)
#define PFACE  RGB555(104, 120, 172)

// ---------------------------------------------------------------- graphics
static void px4(uint8_t *g, int x, int y, int c) {
  uint8_t *b = &g[y * 4 + (x >> 1)];
  *b = (uint8_t)((x & 1) ? (*b & 0x0F) | (c << 4) : (*b & 0xF0) | c);
}
void ui_glyph_tiles(int g, const uint8_t *top, const uint8_t *bot) {
  mc_chr_upload(chr_base + 2 * g * 32, top, 32);
  mc_chr_upload(chr_base + (2 * g + 1) * 32, bot, 32);
}
void ui_glyph_define(int g, const char *rows) {
  static uint8_t a[32], b[32];
  memset(a, 0, 32); memset(b, 0, 32);
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 8; x++) {
      char c = rows[y * 8 + x];
      if (c >= '1' && c <= '9') px4(y < 8 ? a : b, x, y & 7, c - '0');
    }
  ui_glyph_tiles(g, a, b);
}
static void glyph8(int g, const char *rows8) {           // 8 rows, each drawn twice
  static char r[128];
  for (int y = 0; y < 16; y++) memcpy(r + y * 8, rows8 + (y / 2) * 8, 8);
  ui_glyph_define(g, r);
}
static void set_pal(int p, uint16_t c1, uint16_t c2, uint16_t c3) {
  PALETTE[p * 16 + 1] = c1; PALETTE[p * 16 + 2] = c2; PALETTE[p * 16 + 3] = c3;
}
// the fainter edges of the font (indices 4, 5, 6): the ink blended with the colour behind the text
static void text_blend(int p, uint16_t ink, uint16_t bg) {
  static const int a[3] = { 62, 40, 20 };
  for (int k = 0; k < 3; k++) {
    int r = ((ink & 31) * a[k] + (bg & 31) * (100 - a[k])) / 100, g = (((ink >> 5) & 31) * a[k] + ((bg >> 5) & 31) * (100 - a[k])) / 100,
        b = (((ink >> 10) & 31) * a[k] + ((bg >> 10) & 31) * (100 - a[k])) / 100;
    PALETTE[p * 16 + 4 + k] = (uint16_t)(r | g << 5 | b << 10);
  }
}
static const uint16_t fg_dark[8] = {
  RGB555(240, 240, 240), RGB555(110, 116, 140), RGB555(90, 210, 240), RGB555(120, 230, 110),
  RGB555(250, 160, 60), RGB555(250, 225, 90), RGB555(250, 80, 80), RGB555(120, 140, 250) };

static void pointer_upload(int shape) {
  static const char *const shapes[2] = {
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
    ".......11.......",
    "......111......."
    "......121......."
    "......121......."
    "......121......."
    "......121......."
    "......111......."
    "111111...111111."
    "122221...122221."
    "111111...111111."
    "......111......."
    "......121......."
    "......121......."
    "......121......."
    "......121......."
    "......111......."
    "................" };
  static uint8_t g[32];
  const char *p = shapes[shape & 1];
  for (int cy = 0; cy < 2; cy++)
    for (int cx = 0; cx < 2; cx++) {
      memset(g, 0, 32);
      for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
        char c = p[(cy * 8 + y) * 16 + cx * 8 + x];
        if (c != '.') px4(g, x, y, c == '1' ? 2 : 1);
      }
      mc_chr_upload(chr_base + 0x5800 + (cy * 2 + cx) * 32, g, 32);
    }
}
void ui_pointer_shape(int shape) {
  if (shape != pointer_shape) { pointer_shape = shape; pointer_upload(shape); }
}

int ui_ok(void) { return mc_hw_version() >= 5; }

void ui_init(uint32_t off) {
  chr_base = off;
  mc_video_mode(640, 480);
  mc_chr_upload(off, font480, sizeof font480);
  static uint8_t g[32], h[32];
  memset(g, 0x33, 32); ui_glyph_tiles(UI_G_SOLID, g, g);
  glyph8(UI_G_UP,    "........" "...1...." "..111..." ".11111.." "1111111." "........" "........" "........");
  glyph8(UI_G_DOWN,  "........" "........" "1111111." ".11111.." "..111..." "...1...." "........" "........");
  glyph8(UI_G_LEFT,  "....1..." "...11..." "..111..." ".1111..." "..111..." "...11..." "....1..." "........");
  glyph8(UI_G_RIGHT, ".1......" ".11....." ".111...." ".1111..." ".111...." ".11....." ".1......" "........");
  glyph8(UI_G_ARROW, "........" ".11....." ".1211..." ".122211." ".1222211" ".122211." ".1211..." ".11.....");
  glyph8(UI_G_CHECK, "........" "......11" ".....11." "11..11.." ".1111..." "..11...." "........" "........");
  glyph8(UI_G_DOT,   "........" "........" "...11..." "..1111.." "..1111.." "...11..." "........" "........");
  for (int bits = 0; bits < 16; bits++) {                    // frames: lines of colour 1
    memset(g, 0, 32); memset(h, 0, 32);
    for (int y = 0; y < 16; y++)
      for (int x = 0; x < 8; x++) {
        bool on = ((bits & 1) && y < 2) || ((bits & 2) && y >= 14) || ((bits & 4) && x == 0) || ((bits & 8) && x == 7);
        if (on) px4(y < 8 ? g : h, x, y & 7, 1);
      }
    ui_glyph_tiles(UI_G_FRAME + bits, g, h);
  }
  // panel tiles (16 x 16, 128 bytes): edges bits 1 top, 2 bottom, 4 left, 8 right; light 1, shadow 2,
  // face 3 (none for the open ones); the horizontal edges are two lines thick (no flicker)
  static uint8_t t[128];
  memset(t, 0, sizeof t);
  mc_chr_upload(off + 0x4000, t, 128);                       // tile 0: empty
  for (int open = 0; open < 2; open++)
    for (int bits = 0; bits < 16; bits++) {
      for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
          int v = open ? 0 : 3;
          if ((bits & 4) && x == 0) v = 1;
          if ((bits & 1) && y < 2) v = 1;
          if ((bits & 8) && x == 15) v = 2;
          if ((bits & 2) && y >= 14) v = ((bits & 4) && x == 0) ? 1 : 2;
          uint8_t *b = &t[y * 8 + (x >> 1)];
          *b = (uint8_t)((x & 1) ? (*b & 0x0F) | (v << 4) : (*b & 0xF0) | v);
        }
      mc_chr_upload(off + 0x4000 + (uint32_t)((open ? T_OPEN : T_PANEL) + bits) * 128, t, 128);
    }
  pointer_shape = -1; ui_pointer_shape(0);
  for (int p = 0; p < 32; p++) for (int i = 0; i < 16; i++) PALETTE[p * 16 + i] = 0;
  for (int p = 0; p < 8; p++) { set_pal(p, fg_dark[p], RGB555(8, 8, 16), 0); text_blend(p, fg_dark[p], NAVY); }
  static const uint16_t ink[6] = { RGB555(16, 16, 24), RGB555(20, 50, 160), RGB555(170, 20, 20), RGB555(92, 96, 110),
                                   RGB555(255, 255, 255), RGB555(20, 110, 40) };
  for (int i = 0; i < 6; i++) {
    set_pal(UI_INK + i, ink[i], FACE, 0);
    text_blend(UI_INK + i, ink[i], UI_INK + i == UI_INK_WHITE ? PFACE : FACE);
  }
  set_pal(P_RAISED, LIGHT, SHADOW, FACE);
  set_pal(P_SUNK, SHADOW, LIGHT, NAVY);
  set_pal(P_PRESSED, SHADOW, LIGHT, PFACE);
  set_pal(P_POINTER, RGB555(255, 255, 255), RGB555(10, 10, 20), 0);
  set_pal(P_BACK, 0, 0, RGB555(12, 14, 26));
  PALETTE[15 * 16 + 1] = RGB555(12, 14, 26);
  mc_background(15 * 16 + 1);
  for (int l = 0; l < 2; l++) {                  // text: 1024 x 256 pixels each
    mc_layer(l, MC_CHR(off), MC_TILE8 | MC_4BPP, 128, 32, 3);
    mc_layer_scroll(l, 0, l ? 16 : 0);           // layer 1: line 240 shows map row 0
    mc_layer_linescroll(l, true);
    volatile linescroll_t *ls = mc_linescroll(l);
    for (int y = 0; y < 480; y++) {
      bool mine = l ? y >= 240 : y < 240;        // elsewhere: map rows 30-31, always empty
      ls[y].dx = 0;
      ls[y].dy = (int16_t)(mine ? 0 : l ? 224 + (y & 15) - y : 240 + (y & 15) - y);
    }
    mc_tilemap_fill(l, 0);
  }
  mc_layer(2, MC_CHR(off + 0x4000), MC_TILE16 | MC_4BPP, 64, 32, 1);
  mc_layer_scroll(2, 0, 8);
  mc_layer_linescroll(2, false);
  mc_tilemap_fill(2, 0);
  mc_layer(3, MC_CHR(off + 0x4000), MC_TILE16 | MC_4BPP, 64, 32, 0);   // free for the program: empty
  mc_layer_scroll(3, 0, 0);
  mc_layer_linescroll(3, false);
  mc_tilemap_fill(3, 0);
  memset(txt, 0, sizeof txt); memset(txt_old, 0, sizeof txt_old);
  memset(pnl, 0, sizeof pnl); memset(pnl_old, 0, sizeof pnl_old);
  mc_sprite_sheet(MC_CHR(0));
  mc_display(DISP_ON | DISP_LAYER(0) | DISP_LAYER(1) | DISP_LAYER(2) | DISP_LAYER(3) | DISP_SPRITES);
}

// ---------------------------------------------------------------- text and panels
void ui_cls(void) { memset(txt, 0, sizeof txt); memset(pnl, 0, sizeof pnl); }
void ui_put(int x, int y, int g, int pal) {
  if (x >= 0 && x < UI_COLS && y >= 0 && y < UI_ROWS) txt[y][x] = (uint16_t)((g & 0xFF) | (pal << 8));
}
void ui_print(int x, int y, const char *s, int pal) {
  for (; *s; s++, x++) {
    int c = (unsigned char)*s;
    ui_put(x, y, (c < 32 || c > 127 ? '?' : c) - 32, pal);
  }
}
void ui_printn(int x, int y, const char *s, int n, int pal) {
  for (int i = 0; i < n; i++) {
    int c = *s ? (unsigned char)*s++ : ' ';
    ui_put(x + i, y, (c < 32 || c > 127 ? '?' : c) - 32, pal);
  }
}
void ui_printc(int x0, int x1, int y, const char *s, int pal) {
  int n = (int)strlen(s);
  ui_print(x0 + (x1 - x0 + 1 - n) / 2, y, s, pal);
}
void ui_printf(int x, int y, int pal, const char *fmt, ...) {
  char b[UI_COLS + 1];
  va_list ap;
  va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
  ui_print(x, y, b, pal);
}
void ui_frame(int x0, int y0, int x1, int y1, int pal) {
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      int bits = (y == y0 ? 1 : 0) | (y == y1 ? 2 : 0) | (x == x0 ? 4 : 0) | (x == x1 ? 8 : 0);
      if (bits) ui_put(x, y, UI_G_FRAME + bits, pal);
    }
}
void ui_box(int x0, int y0, int x1, int y1, int style) {
  int c0 = x0 >> 1, c1 = x1 >> 1, r0 = y0, r1 = y1 + 1;
  for (int r = r0; r <= r1; r++)
    for (int c = c0; c <= c1; c++) {
      if (r < 0 || r >= BROWS || c < 0 || c >= BCOLS) continue;
      if (style == UI_HOLE) { pnl[r][c] = 0; continue; }
      if (style == UI_BACK) { pnl[r][c] = (uint16_t)(T_PANEL | (P_BACK << 8)); continue; }
      int bits = (r == r0 ? 1 : 0) | (r == r1 ? 2 : 0) | (c == c0 ? 4 : 0) | (c == c1 ? 8 : 0);
      int pal = style == UI_RAISED ? P_RAISED : style == UI_PRESSED ? P_PRESSED : P_SUNK;
      if (style == UI_FRAME && !bits) { pnl[r][c] = 0; continue; }
      pnl[r][c] = (uint16_t)(((style == UI_FRAME ? T_OPEN : T_PANEL) + bits) | (pal << 8));
    }
}

// ---------------------------------------------------------------- zones and widgets
void ui_zone(int x0, int y0, int x1, int y1, int id, int arg) {
  if (nzones < (int)(sizeof zones / sizeof zones[0]))
    zones[nzones++] = (ui_zone_t){ (uint8_t)x0, (uint8_t)y0, (uint8_t)x1, (uint8_t)y1, (uint8_t)id, (int16_t)arg };
}
const ui_zone_t *ui_zone_at(int px, int py) {
  int x = px / 8, y = py / 16;
  for (int i = nzones_prev - 1; i >= 0; i--) {
    const ui_zone_t *z = &zones_prev[i];
    if (x >= z->x0 && x <= z->x1 && y >= z->y0 && y <= z->y1) return z;
  }
  return 0;
}
void ui_button(int x, int y, int w, const char *label, bool on, int id, int arg) {
  ui_box(x, y, x + w - 1, y, on ? UI_PRESSED : UI_RAISED);
  ui_printc(x, x + w - 1, y, label, on ? UI_INK_WHITE : UI_INK);
  ui_zone(x, y, x + w - 1, y, id, arg);
}
void ui_spin(int x, int y, const char *label, int wl, const char *value, int wv, int id, int arg) {
  ui_printn(x, y, label, wl, UI_INK);
  ui_printn(x + wl, y, value, wv, UI_INK_BLUE);
  ui_put(x + wl + wv, y, UI_G_UP, UI_INK);
  ui_put(x + wl + wv + 1, y, UI_G_DOWN, UI_INK);
  ui_zone(x, y, x + wl + wv - 1, y, id, arg * 4 + 2);
  ui_zone(x + wl + wv, y, x + wl + wv, y, id, arg * 4);
  ui_zone(x + wl + wv + 1, y, x + wl + wv + 1, y, id, arg * 4 + 1);
}

// ---------------------------------------------------------------- frames
static char msg[UI_COLS + 1];
static int msg_timer, confirm_key, confirm_timer, rep_done;
void ui_frame_begin(void) {
  ui_cls();
  nzones = 0;
  rep_done = 0;
  if (msg_timer > 0) msg_timer--;
  if (confirm_timer > 0 && --confirm_timer == 0) confirm_key = 0;
}
void ui_frame_end(void) {
  for (int y = 0; y < UI_ROWS; y++) {
    volatile uint32_t *m = TILEMAP(y < 15 ? 0 : 1) + (y % 15) * 2 * 128;
    for (int x = 0; x < UI_COLS; x++) {
      uint16_t t = txt[y][x];
      if (t == txt_old[y][x]) continue;
      txt_old[y][x] = t;
      int g = t & 0xFF;
      m[x] = t ? TILE(2 * g, t >> 8) : 0; m[128 + x] = t ? TILE(2 * g + 1, t >> 8) : 0;
    }
  }
  for (int r = 0; r < BROWS; r++)
    for (int c = 0; c < BCOLS; c++)
      if (pnl[r][c] != pnl_old[r][c]) { pnl_old[r][c] = pnl[r][c]; TILEMAP(2)[r * 64 + c] = TILE(pnl[r][c] & 0xFF, pnl[r][c] >> 8); }
  memcpy(zones_prev, zones, sizeof(ui_zone_t) * nzones); nzones_prev = nzones;
  if (!mc_mouse_present()) { mc_sprite_hide(0); return; }
  int cell = (int)((chr_base + 0x5800) / 32);
  if (pointer_shape == 1) { mc_sprite(0, mc_mouse_x() - 7, mc_mouse_y() - 7, cell, 2, 2, P_POINTER, 7); mc_sprite_zoom(0, 0x100, 0x100); }
  else { mc_sprite(0, mc_mouse_x(), mc_mouse_y(), cell, 2, 2, P_POINTER, 7); mc_sprite_zoom(0, 0x180, 0x180); }
}

// ---------------------------------------------------------------- keyboard
#ifndef KEY_COMMA
#define KEY_COMMA 0x41
#define KEY_PERIOD 0x49
#define KEY_SEMICOLON 0x4C
#define KEY_MINUS 0x4E
#define KEY_KPMINUS 0x7B
#endif
static const uint8_t letter_code[26] = { KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
  KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z };
static const uint8_t digit_code[10] = { KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9 };
static const uint8_t kp_code[10] = { KEY_KP0, KEY_KP1, KEY_KP2, KEY_KP3, KEY_KP4, KEY_KP5, KEY_KP6, KEY_KP7, KEY_KP8, KEY_KP9 };
int ui_char(int k) {
  if (ui_layout == 0) {                // AZERTY: A/Q, Z/W and M move
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
  if (k == KEY_KPMINUS || (ui_layout == 1 && k == KEY_MINUS)) return '-';
  if (k == KEY_PERIOD && ui_layout == 1) return '.';
  return 0;
}
int ui_hex(int k) {
  int c = ui_char(k);
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
bool ui_shift(void) { return mc_key(KEY_LSHIFT) || mc_key(KEY_RSHIFT); }
bool ui_ctrl(void) { return mc_key(KEY_LCTRL) || mc_key(KEY_RCTRL); }
bool ui_alt(void) { return mc_key(KEY_LALT) || mc_key(KEY_RALT); }
static uint32_t kdown[16];
static int rep_key, rep_timer;
static bool repeatable(int k) {
  return k == KEY_UP || k == KEY_DOWN || k == KEY_LEFT || k == KEY_RIGHT || k == KEY_PGUP || k == KEY_PGDN ||
         k == KEY_DELETE || k == KEY_BACKSPACE || k == KEY_INSERT;
}
int ui_key(void) {
  int e;
  while ((e = mc_key_event()) >= 0) {
    int k = e & 0x1FF;
    uint32_t bit = 1u << (k & 31);
    if (e & 0x200) {
      if (kdown[k >> 5] & bit) continue;      // repeat of the keyboard: ours is used instead
      kdown[k >> 5] |= bit;
      if (repeatable(k)) { rep_key = k; rep_timer = 18; } else rep_key = 0;
      return k;
    }
    kdown[k >> 5] &= ~bit;
    if (k == rep_key) rep_key = 0;
  }
  if (!rep_done) {
    rep_done = 1;
    if (rep_key) {
      if (!mc_key(rep_key)) rep_key = 0;
      else if (--rep_timer == 0) { rep_timer = 3; return rep_key; }
    }
  }
  return -1;
}

// ---------------------------------------------------------------- messages
void ui_message(const char *s) { snprintf(msg, sizeof msg, "%s", s); msg_timer = 200; }
const char *ui_message_text(void) { return msg_timer > 0 ? msg : 0; }
bool ui_confirm(int key, const char *what) {
  if (confirm_key == key && confirm_timer > 0) { confirm_key = 0; return true; }
  confirm_key = key; confirm_timer = 150;
  char b[UI_COLS + 1];
  snprintf(b, sizeof b, "%s : ENCORE UNE FOIS POUR CONFIRMER", what);
  ui_message(b);
  return false;
}
