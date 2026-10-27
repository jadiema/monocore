// SOLFEGE: learning to read notes on the staff, with a MIDI keyboard (or the computer keyboard).
// In 640 x 480 interlaced (hardware version 5).
//
// Two modes. "La note attend": notes wait on a staff, the first one at the reading line; play it on
// the keyboard drawn at the bottom, there is no clock. The game starts with three notes (do, re, mi)
// and brings in a new one each time all those already met are well known (three right answers in a
// row each): the 13 natural notes, then 12 sharps and flats. "Lecture deroulante": 40 notes cross the
// reading line at a steady pace; wrong or missed notes only count against the score. Treble clef
// (do 4 to la 5) and bass clef (mi 2 to do 4); the progress of each clef is kept in the NVRAM.
//
// Picture: text in 16 x 16 tiles (the font enlarged with EPX, 40 x 30 characters); the staff in
// 16 x 16 tiles, lines two pixels thick (no flicker when interlaced); notes, clefs, accidentals and
// the names under the notes are sprites; the piano is one 8 bpp picture in which each key has its own
// colour index, so that lighting a key is a single palette write.
#include <stdio.h>
#include <string.h>
#include "mc.h"
#include "mtrack.h"

#define SW 640
#define SH 480
#define COLS 40                   // text: 40 x 30 cells of 16 x 16
#define KEYS 48
#define LOWKEY 25                 // tracker note of the first key (C-2, MIDI 36)
#define Y0 240                    // the bottom line of the staff; 8 pixels a degree
#define DEG 8
#define QX 136                    // the reading line
#define QN 6                      // notes on the staff
#define QSTEP 80
#define NVB ((volatile uint8_t *)NVRAM_BASE)

// CHR RAM: sprite cells (8 x 8, 4 bpp) from 0; text and staff tiles (16 x 16, 4 bpp) from TXT_OFF;
// the piano (16 x 16, 8 bpp) from KEY_OFF
#define TXT_OFF 0x4000
#define KEY_OFF 0x8000
enum { C_FONT = 0, C_HEAD = 96, C_LEDGER = C_HEAD + 6, C_TREBLE = C_LEDGER + 8, C_BASS = C_TREBLE + 10, C_SHARP = C_BASS + 4, C_FLAT = C_SHARP + 2 };
enum { T_PAPER = 96, T_LINE, T_MARK, T_MARKLINE };
enum { P_WHITE, P_YELLOW, P_GREEN, P_RED, P_GREY, P_CYAN, P_INK, P_INK_GREY, P_PAPER = 8, P_NOTE, P_NOTE_CUR, P_NOTE_OK,
       P_NOTE_BAD, P_CLEF, P_KEYS = 16 };                       // P_KEYS: the 8 bpp piano, palettes 16 to 31

#ifndef KEY_COMMA
#define KEY_COMMA 0x41
#define KEY_PERIOD 0x49
#define KEY_SLASH 0x4A
#define KEY_SEMICOLON 0x4C
#endif

// ================================================================ the notes of each clef, in the order they are met
// tracker note (49 = do 4), staff step (0 = bottom line), accidental (+1 sharp, -1 flat)
typedef struct { uint8_t note; int8_t step, acc; } snote_t;
#define NNOTES 25
#define NNATURAL 13
static const snote_t clefs[2][NNOTES] = {
  { {49, -2, 0}, {51, -1, 0}, {53, 0, 0}, {54, 1, 0}, {56, 2, 0}, {58, 3, 0}, {60, 4, 0}, {61, 5, 0}, {63, 6, 0}, {65, 7, 0}, {66, 8, 0}, {68, 9, 0}, {70, 10, 0},
    // then the sharps and flats, a black key at a time, under both its names
    {55, 1, 1}, {59, 4, -1}, {62, 5, 1}, {52, 0, -1}, {57, 2, 1}, {57, 3, -1}, {50, -2, 1}, {62, 6, -1}, {67, 8, 1}, {55, 2, -1}, {59, 3, 1}, {64, 7, -1} },
  { {37, 3, 0}, {39, 4, 0}, {41, 5, 0}, {42, 6, 0}, {44, 7, 0}, {46, 8, 0}, {48, 9, 0}, {49, 10, 0}, {36, 2, 0}, {34, 1, 0}, {32, 0, 0}, {30, -1, 0}, {29, -2, 0},
    {43, 6, 1}, {35, 2, -1}, {38, 3, 1}, {40, 5, -1}, {45, 7, 1}, {45, 8, -1}, {50, 10, 1}, {38, 4, -1}, {31, -1, 1}, {43, 7, -1}, {35, 1, 1}, {28, -2, -1} } };
static const char *const clef_name[2] = { "CLE DE SOL", "CLE DE FA" };
static const char *const black_names[12] = { 0, "DO#/REb", 0, "RE#/MIb", 0, 0, "FA#/SOLb", 0, "SOL#/LAb", 0, "LA#/SIb", 0 };
static const char *const letters[12] = { "DO", 0, "RE", 0, "MI", "FA", 0, "SOL", 0, "LA", 0, "SI" };
static const char *played_name(int n) { int c = (n - 1) % 12; return black_names[c] ? black_names[c] : letters[c]; }
static int octave(int n) { return (n - 1) / 12; }
// the name as written: the letter of the line or space, then # or b
static void sname(const snote_t *n, char *s, int len, bool word) {
  const char *l = letters[(n->note - n->acc - 1) % 12];
  if (!n->acc) snprintf(s, len, "%s", l);
  else if (word) snprintf(s, len, "%s %s", l, n->acc > 0 ? "DIESE" : "BEMOL");
  else snprintf(s, len, "%s%s", l, n->acc > 0 ? "#" : "b");
}
static void where(int step, char *s, int len) {               // where the note sits on the staff
  if (step == -2) snprintf(s, len, "SUR UNE PETITE LIGNE EN DESSOUS");
  else if (step == -1) snprintf(s, len, "SOUS LA PORTEE");
  else if (step == 9) snprintf(s, len, "AU-DESSUS DE LA PORTEE");
  else if (step == 10) snprintf(s, len, "SUR UNE PETITE LIGNE AU-DESSUS");
  else if (step % 2 == 0) snprintf(s, len, "SUR LA %de LIGNE", step / 2 + 1);
  else snprintf(s, len, "DANS LE %de INTERLIGNE", (step + 1) / 2);
}

// ================================================================ text (layer 0, 16 x 16 tiles)
static volatile uint32_t *L0, *L1, *L2;
static void put(volatile uint32_t *m, int x, int y, int t, int pal) { if (x >= 0 && x < 64 && y >= 0 && y < 32) m[y * 64 + x] = TILE(t, pal); }
static void text(int x, int y, const char *s, int pal) {
  for (; *s && x < COLS; s++, x++) { int c = (unsigned char)*s; put(L0, x, y, (c < 32 || c > 127 ? '?' : c) - 32, pal); }
}
static void textc(int y, const char *s, int pal) { text((COLS - (int)strlen(s)) / 2, y, s, pal); }
static void clear_row(int y) { for (int x = 0; x < COLS; x++) L0[y * 64 + x] = 0; }
static void clear_rows(int a, int b) { for (int y = a; y <= b; y++) clear_row(y); }

// ================================================================ graphics
static int font_px(int ch, int x, int y) {                     // a pixel of the 8 x 8 font (4 bpp)
  if (x < 0 || x > 7 || y < 0 || y > 7) return 0;
  uint8_t b = font_chr[ch * 32 + y * 4 + (x >> 1)];
  return (x & 1) ? b >> 4 : b & 15;
}
static void px4(uint8_t *g, int w, int x, int y, int c) {      // 4 bpp pixel in a picture w pixels wide
  uint8_t *b = &g[y * (w / 2) + (x >> 1)];
  *b = (uint8_t)((x & 1) ? (*b & 0x0F) | (c << 4) : (*b & 0xF0) | c);
}
// the font enlarged twice with EPX (smooth diagonals) into 16 x 16 tiles
static void make_font16(void) {
  static uint8_t g[128];
  for (int ch = 0; ch < 96; ch++) {
    memset(g, 0, sizeof g);
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
      int p = font_px(ch, x, y), a = font_px(ch, x, y - 1), b = font_px(ch, x + 1, y), c = font_px(ch, x - 1, y), d = font_px(ch, x, y + 1);
      int e0 = p, e1 = p, e2 = p, e3 = p;
      if (c == a && c != d && a != b) e0 = a;
      if (a == b && a != c && b != d) e1 = b;
      if (d == c && d != b && c != a) e2 = c;
      if (b == d && b != a && d != c) e3 = d;
      px4(g, 16, 2 * x, 2 * y, e0); px4(g, 16, 2 * x + 1, 2 * y, e1); px4(g, 16, 2 * x, 2 * y + 1, e2); px4(g, 16, 2 * x + 1, 2 * y + 1, e3);
    }
    mc_chr_upload(TXT_OFF + ch * 128, g, 128);
  }
}
static void tile16(int n, const char *rows) {                  // a 16 x 16 tile from 16 rows of 16 characters
  static uint8_t g[128];
  memset(g, 0, sizeof g);
  for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) { char c = rows[y * 16 + x]; if (c >= '1' && c <= '9') px4(g, 16, x, y, c - '0'); }
  mc_chr_upload(TXT_OFF + n * 128, g, 128);
}
// sprite cells (8 x 8): a picture w x h cells given as one character per pixel, row major
static void cells(int n, int w, int h, const char *const *rows) {
  static uint8_t g[32];
  for (int cy = 0; cy < h; cy++) for (int cx = 0; cx < w; cx++) {
    memset(g, 0, 32);
    for (int y = 0; y < 8; y++) {
      const char *r = rows[cy * 8 + y];
      for (int x = 0; x < 8; x++) { char c = r[cx * 8 + x]; if (c >= '1' && c <= '9') px4(g, 8, x, y, c - '0'); }
    }
    mc_chr_upload((n + cy * w + cx) * 32, g, 32);
  }
}
static void set_pal(int p, uint16_t a, uint16_t b, uint16_t c) { PALETTE[p * 16 + 1] = a; PALETTE[p * 16 + 2] = b; PALETTE[p * 16 + 3] = c; }

// a sprite with its zoom (the zoom stays with the sprite entry: every use sets it)
static void spr(int n, int x, int y, int cell, int w, int h, int pal, int prio, int zx, int zy) {
  mc_sprite(n, x, y, cell, w, h, pal, prio); mc_sprite_zoom(n, zx, zy);
}

// the whole note, drawn at full resolution: a ring between two ellipses, the inner one tilted
static void make_head(void) {
  static char rows[16][25];
  for (int y = 0; y < 16; y++) {
    for (int x = 0; x < 24; x++) {
      int dx = 2 * x - 23, dy = 2 * y - 15;                    // in half pixels from the centre
      bool out = dx * dx * 225 + dy * dy * 529 <= 225 * 529;   // ellipse 23 x 15 (half pixels)
      int u = (dx * 87 + dy * 50) / 100, v = (dy * 87 - dx * 50) / 100;   // rotated by 30 degrees
      bool in = u * u * 64 + v * v * 196 <= 64 * 196;          // ellipse 14 x 8, tilted
      rows[y][x] = out && !in ? '1' : '.';
    }
    rows[y][24] = 0;
  }
  const char *r[16]; for (int y = 0; y < 16; y++) r[y] = rows[y];
  cells(C_HEAD, 3, 2, r);
}
static const char *const ledger_art[16] = {
  "................................", "................................", "................................", "................................",
  "................................", "................................", "................................", "11111111111111111111111111111111",
  "11111111111111111111111111111111", "................................", "................................", "................................",
  "................................", "................................", "................................", "................................" };
static const char *const treble_art[40] = {
  ".........11.....", "........1111....", "........1..1....", ".......11..1....", ".......1...1....", ".......1..11....",
  ".......1.11.....", ".......111......", "......111.......", ".....111........", "....1111........", "...11.11........",
  "..11..1.........", ".11...11111.....", ".1...111..11....", "11..11.1...11...", "1..11..1....1...", "1..1...11...11..",
  "1..1....1....1..", "1...1...1....1..", "11...1..1...11..", ".11.....1..11...", "..111...1111....", "....11111.......",
  "........1.......", "........1.......", "........11......", ".........1......", ".........1......", ".....11..1......",
  "....1111.1......", "....1111.1......", ".....11.11......", "......111.......", "................", "................",
  "................", "................", "................", "................" };
static const char *const bass_art[16] = {
  "..11111.........", ".11...111.......", "11.....11...11..", "111.....11..11..", "111.....11......", ".1......11......",
  "........11......", ".......11...11..", ".......11...11..", "......11........", ".....11.........", "....11..........",
  "..11............", "11..............", "................", "................" };
static const char *const sharp_art[16] = {
  "........", "........", "........", "..1..1..", "..1..1..", "..1..111", "..1111..", "111..1..",
  "..1..111", "..1111..", "111..1..", "..1..1..", "..1..1..", "........", "........", "........" };
static const char *const flat_art[16] = {
  "........", "..1.....", "..1.....", "..1.....", "..1.....", "..1.....", "..1.11..", "..11..1.",
  "..1...1.", "..1..1..", "..111...", "........", "........", "........", "........", "........" };

// ================================================================ the piano: 28 white keys of 22 pixels, 8 bpp
#define KX0 12
#define KW 22
#define KY 400                     // top of the keyboard (tile row 25), 64 pixels high
static const uint8_t white_semi[7] = { 0, 2, 4, 5, 7, 9, 11 };
static bool black(int n) { int s = (n - 1) % 12; return s == 1 || s == 3 || s == 6 || s == 8 || s == 10; }
static int key_at(int x, int y) {                              // colour index of a pixel of the piano
  if (x < KX0 || x >= KX0 + 28 * KW) return 0;
  int w = (x - KX0) / KW, wx = (x - KX0) % KW;
  int semi = (w / 7) * 12 + white_semi[w % 7];
  if (y < 40) {                                                // the black keys, 14 pixels wide, on the boundaries
    int s = semi % 12;
    if (wx >= KW - 7 && s != 4 && s != 11 && semi + 1 < KEYS) return y >= 38 ? 3 : 16 + semi + 1;
    if (wx < 7 && s != 0 && s != 5) return y >= 38 ? 3 : 16 + semi - 1;
  }
  if (wx == 0) return 2;                                       // the gap between white keys
  if (y >= 61) return 3;                                       // the bottom edge
  return 16 + semi;
}
static void make_piano(void) {
  static uint8_t g[256];
  for (int ty = 0; ty < 4; ty++) for (int tx = 0; tx < 40; tx++) {
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) g[y * 16 + x] = (uint8_t)key_at(tx * 16 + x, ty * 16 + y);
    mc_chr_upload(KEY_OFF + (1 + ty * 40 + tx) * 256, g, 256);      // tile 0 stays empty (the rest of the map)
    L1[(25 + ty) * 64 + tx] = TILE(1 + ty * 40 + tx, P_KEYS);
  }
  memset(g, 0, sizeof g); mc_chr_upload(KEY_OFF, g, 256);
}
static uint8_t key_state[KEYS], key_shown[KEYS];     // 0 up, 1 wrong, 2 right, 3 hint
static void draw_keys(bool all) {
  static const uint16_t on[4][2] = {                           // white key, black key, for each state
    { RGB555(236, 236, 244), RGB555(30, 30, 42) }, { RGB555(230, 90, 80), RGB555(190, 50, 50) },
    { RGB555(110, 220, 120), RGB555(50, 160, 70) }, { RGB555(250, 210, 80), RGB555(200, 150, 30) } };
  for (int k = 0; k < KEYS; k++) {
    if (!all && key_shown[k] == key_state[k]) continue;
    key_shown[k] = key_state[k];
    PALETTE[P_KEYS * 16 + 16 + k] = on[key_state[k]][black(LOWKEY + k)];
  }
}
// the names under the C keys, as sprites: the 8 x 8 font, twice as tall (no flicker)
static int label_sprites(int sp) {
  for (int o = 0; o < 4; o++) {
    int x = KX0 + o * 7 * KW + KW / 2 - 12;
    char s[4] = { 'D', 'O', (char)('2' + o), 0 };
    for (int c = 0; c < 3; c++) spr(sp++, x + 8 * c, KY + 66, s[c] - 32, 1, 1, P_GREY, 4, 0x100, 0x200);
  }
  return sp;
}

static void video_init(void) {
  mc_video_mode(SW, SH);
  mc_chr_upload(0, font_chr, 96 * 32);                        // the font as sprite cells, for the names
  make_font16();
  tile16(T_PAPER, "1111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111");
  tile16(T_LINE,  "2222222222222222222222222222222211111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111");
  tile16(T_MARK,  "3333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333");
  tile16(T_MARKLINE, "2222222222222222222222222222222233333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333");
  make_head();
  cells(C_LEDGER, 4, 2, ledger_art);
  cells(C_TREBLE, 2, 5, treble_art);
  cells(C_BASS, 2, 2, bass_art);
  cells(C_SHARP, 1, 2, sharp_art);
  cells(C_FLAT, 1, 2, flat_art);
  for (int p = 0; p < 32; p++) for (int i = 0; i < 16; i++) PALETTE[p * 16 + i] = 0;
  static const uint16_t fg[8] = { RGB555(240, 240, 240), RGB555(250, 220, 80), RGB555(110, 230, 120), RGB555(250, 90, 80),
                                  RGB555(120, 126, 150), RGB555(90, 210, 240), RGB555(40, 36, 30), RGB555(140, 130, 110) };
  for (int p = 0; p < 8; p++) set_pal(p, fg[p], p >= 6 ? 0 : RGB555(10, 10, 20), 0);
  set_pal(P_PAPER, RGB555(244, 236, 214), RGB555(60, 54, 46), RGB555(252, 238, 170));
  set_pal(P_NOTE, RGB555(40, 36, 30), 0, 0);
  set_pal(P_NOTE_CUR, RGB555(40, 90, 200), 0, 0);
  set_pal(P_NOTE_OK, RGB555(40, 160, 70), 0, 0);
  set_pal(P_NOTE_BAD, RGB555(220, 50, 40), 0, 0);
  set_pal(P_CLEF, RGB555(40, 36, 30), 0, 0);
  PALETTE[P_KEYS * 16 + 2] = RGB555(90, 90, 110);              // gaps between white keys
  PALETTE[P_KEYS * 16 + 3] = RGB555(120, 120, 140);            // edges
  PALETTE[31 * 16 + 1] = RGB555(14, 16, 30);
  mc_background(31 * 16 + 1);
  mc_layer(0, MC_CHR(TXT_OFF), MC_TILE16 | MC_4BPP, 64, 32, 3);
  mc_layer(1, MC_CHR(KEY_OFF), MC_TILE16 | MC_8BPP, 64, 32, 1);
  mc_layer(2, MC_CHR(TXT_OFF), MC_TILE16 | MC_4BPP, 64, 32, 0);
  mc_tilemap_fill(0, 0); mc_tilemap_fill(1, 0); mc_tilemap_fill(2, 0);
  L0 = mc_tilemap(0); L1 = mc_tilemap(1); L2 = mc_tilemap(2);
  make_piano();
  mc_sprite_sheet(MC_CHR(0));
  mc_display(DISP_ON | DISP_LAYER(0) | DISP_LAYER(1) | DISP_LAYER(2) | DISP_SPRITES);
}

// the staff: a sheet of paper (tile rows 7 to 19), five lines from y 176 to y 240, the reading mark
static void draw_staff(bool show) {
  for (int y = 7; y <= 19; y++) for (int x = 0; x < COLS; x++) {
    bool line = y >= 11 && y <= 15 && x >= 1 && x < COLS - 1, mark = x >= 7 && x <= 9 && y >= 8 && y <= 18;
    put(L2, x, y, !show ? 0 : mark ? (line ? T_MARKLINE : T_MARK) : line ? T_LINE : T_PAPER, P_PAPER);
  }
}

// ================================================================ progress (NVRAM)
static int mode, speed_sel = 1;          // mode 0: the note waits, 1: the staff scrolls
static int clef, help_mode = 1;          // help: 0 names always, 1 for the notes still learnt, 2 never
static uint8_t unlocked[2], streak[2][NNOTES];
static void nv_load(void) {
  unlocked[0] = unlocked[1] = 3; memset(streak, 0, sizeof streak);
  if (NVB[0] == 'S' && NVB[1] == 'O' && NVB[2] == 'L' && NVB[3] == 'F') {
    help_mode = NVB[6] % 3; clef = NVB[7] & 1; mode = NVB[5] & 1; speed_sel = NVB[80] % 3;
    int v1 = NVB[4] == 1, stride = v1 ? 16 : 32, n = v1 ? NNATURAL : NNOTES;   // version 1: 13 notes, 16 bytes a clef
    for (int c = 0; c < 2; c++) {
      unlocked[c] = NVB[8 + stride * c];
      if (unlocked[c] < 3 || unlocked[c] > n) unlocked[c] = 3;
      for (int i = 0; i < n; i++) streak[c][i] = NVB[9 + stride * c + i];
    }
  }
}
static void nv_save(void) {
  NVB[0] = 'S'; NVB[1] = 'O'; NVB[2] = 'L'; NVB[3] = 'F'; NVB[4] = 2;
  NVB[6] = (uint8_t)help_mode; NVB[7] = (uint8_t)clef; NVB[5] = (uint8_t)mode; NVB[80] = (uint8_t)speed_sel;
  for (int c = 0; c < 2; c++) {
    NVB[8 + 32 * c] = unlocked[c];
    for (int i = 0; i < NNOTES; i++) NVB[9 + 32 * c + i] = streak[c][i];
  }
}

// ================================================================ drawing a note
// sprites: the head (24 x 16), its small lines, its accidental (zoomed twice), its name
static int sprite_note(int sp, const snote_t *n, int x, int pal, bool name) {
  int y = Y0 - n->step * DEG;
  for (int s = -2; s >= n->step; s -= 2) spr(sp++, x - 16, Y0 - s * DEG - 8, C_LEDGER, 4, 2, P_NOTE, 2, 0x100, 0x100);
  for (int s = 10; s <= n->step; s += 2) spr(sp++, x - 16, Y0 - s * DEG - 8, C_LEDGER, 4, 2, P_NOTE, 2, 0x100, 0x100);
  spr(sp++, x - 12, y - 8, C_HEAD, 3, 2, pal, 3, 0x100, 0x100);
  if (n->acc) spr(sp++, x - 34, y - 16, n->acc > 0 ? C_SHARP : C_FLAT, 1, 2, pal, 3, 0x200, 0x200);
  if (name) {
    char nm[12]; sname(n, nm, sizeof nm, false);
    int len = (int)strlen(nm), nx = x - len * 8;
    for (int c = 0; c < len; c++) spr(sp++, nx + 16 * c, 278, nm[c] - 32, 1, 1, P_INK_GREY, 3, 0x200, 0x200);
  }
  return sp;
}
static int sprite_clef(int sp) {
  if (clef) spr(sp, 24, 182, C_BASS, 2, 2, P_CLEF, 2, 0x200, 0x200);
  else spr(sp, 24, 181, C_TREBLE, 2, 5, P_CLEF, 2, 0x200, 0x260);
  return sp + 1;
}
static void hide_from(int sp) { while (sp < 160) mc_sprite_hide(sp++); }

// ================================================================ the game: the note waits
enum { S_MENU, S_PLAY, S_SCROLL, S_RESULTS };
static int state, menu_item, confirm_reset;
static int queue[QN];                    // indices into clefs[clef]
static int slide, last_pick = -1, errors_here, new_note = -1;
static int score, series, best_series, right, wrong, frames_here;
static int msg_timer;
static int8_t key_down[512];
static mt_instrument_t piano = { "PIANO", MT_TYPE_PCM, 10, 0, 0, 56, 128, 0, 30, 20, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {0, 0} };
static mt_instrument_t buzz = { "BUZZ", MT_TYPE_PSG, PSG_SQUARE, 40, 0, 20, 128, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, -6, 0, 0, 0, {0, 0} };
static mt_instrument_t tick = { "TICK", MT_TYPE_PSG, PSG_NOISE, 128, 0, 30, 128, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, MT_IF_FIXED, {0, 0} };
static int voice;

static int pick(void) {                  // weak notes and the newest come more often
  int n = unlocked[clef], w[NNOTES], tot = 0;
  for (int i = 0; i < n; i++) {
    w[i] = 1 + (streak[clef][i] < 3 ? 3 : 0) + (i == n - 1 ? 3 : 0);
    if (i == last_pick && n > 1) w[i] = 0;
    tot += w[i];
  }
  int r = (int)(mc_rand() % (uint32_t)tot);
  for (int i = 0; i < n; i++) { if (r < w[i]) { last_pick = i; return i; } r -= w[i]; }
  return 0;
}
static bool show_name(int i) { return help_mode == 0 || (help_mode == 1 && streak[clef][i] < 2) || i == new_note; }

static void say(const char *a, int pa, const char *b, int pb, const char *c, int pc) {   // rows 21 to 23
  clear_rows(21, 23);
  if (a) textc(21, a, pa);
  if (b) textc(22, b, pb);
  if (c) textc(23, c, pc);
  msg_timer = 150;
}
static void draw_progress(void) {
  char s[48];
  clear_rows(0, 6);
  text(1, 0, clef_name[clef], P_YELLOW);
  snprintf(s, sizeof s, "SCORE %d", score); text(39 - (int)strlen(s), 0, s, P_WHITE);
  snprintf(s, sizeof s, "NOTES %d/%d", unlocked[clef], NNOTES); text(1, 1, s, P_GREY);
  if (series >= 3) { snprintf(s, sizeof s, "SERIE %d", series); text(39 - (int)strlen(s), 1, s, P_CYAN); }
  // the notes met so far: grey still to learn, white on the way, green known
  int x = 1, y = 3;
  for (int i = 0; i < unlocked[clef]; i++) {
    const snote_t *n = &clefs[clef][i];
    char nm[12]; sname(n, nm, sizeof nm, false);
    snprintf(s, sizeof s, "%s%d", nm, octave(n->note - n->acc));
    if (x + (int)strlen(s) > 39) { x = 1; y++; }
    text(x, y, s, streak[clef][i] >= 3 ? P_GREEN : streak[clef][i] ? P_WHITE : P_GREY);
    x += (int)strlen(s) + 1;
  }
}
static void draw_notes(void) {
  int sp = sprite_clef(0);
  for (int q = 0; q < QN; q++) {
    int x = QX + q * QSTEP + slide;
    if (x > SW - 24) continue;
    int pal = q == 0 ? (errors_here && (mc_frames & 8) && frames_here < 20 ? P_NOTE_BAD : P_NOTE_CUR) : P_NOTE;
    if (q == 0 && slide) pal = P_NOTE_OK;
    sp = sprite_note(sp, &clefs[clef][queue[q]], x, pal, show_name(queue[q]) && !slide);
  }
  sp = label_sprites(sp);
  hide_from(sp);
}

static void new_queue(void) {
  last_pick = -1;
  for (int q = 0; q < QN; q++) queue[q] = pick();
  int nn = unlocked[clef] - 1;                                 // the newest note first if it has never been answered
  if (streak[clef][nn] == 0 && unlocked[clef] > 3) { queue[0] = nn; new_note = nn; }
  slide = 0; errors_here = 0; frames_here = 0;
}
static void introduce(int i) {
  char a[48], b[48], nm[16];
  const snote_t *n = &clefs[clef][i];
  sname(n, nm, sizeof nm, true);
  snprintf(a, sizeof a, "NOUVELLE NOTE : %s", nm);
  where(n->step, b, sizeof b);
  say(a, P_YELLOW, b, P_WHITE, n->acc > 0 ? "LE DIESE MONTE : TOUCHE NOIRE A DROITE" : n->acc < 0 ? "LE BEMOL BAISSE : TOUCHE A GAUCHE" : 0, P_GREY);
  memset(key_state, 0, sizeof key_state);
  if (n->note - LOWKEY >= 0 && n->note - LOWKEY < KEYS) key_state[n->note - LOWKEY] = 3;
}
static void start_game(void) {
  score = series = best_series = right = wrong = 0;
  memset(key_state, 0, sizeof key_state);
  draw_staff(true); clear_rows(0, 29);
  new_queue();
  draw_progress();
  if (new_note >= 0) introduce(new_note);
  else say("JOUEZ LA NOTE BLEUE", P_WHITE, 0, 0, "LA NOTE ATTEND : PRENEZ VOTRE TEMPS", P_GREY);
  state = S_PLAY;
}
static void check_unlock(void) {
  int n = unlocked[clef];
  for (int i = 0; i < n; i++) if (streak[clef][i] < 3) return;
  if (n >= NNOTES) return;
  unlocked[clef]++;
  nv_save();
  queue[1] = unlocked[clef] - 1; new_note = unlocked[clef] - 1;      // it comes next
}
static void sound(int n, int vol) { mt_note(MT_PCM(voice), n, &piano, vol); voice = (voice + 1) % 6; }

static void answer(int n, int vol) {
  if (slide) return;
  int qi = queue[0];
  const snote_t *t = &clefs[clef][qi];
  sound(n, vol);
  int x = n - LOWKEY;
  char a[48], b[48], c[48], tn[16];
  if (n == t->note) {
    if (x >= 0 && x < KEYS) key_state[x] = 2;
    right++; series++; if (series > best_series) best_series = series;
    int fast = frames_here < 90 ? 10 : frames_here < 180 ? 5 : 0;
    score += 10 + fast + (series > 10 ? 10 : series);
    if (!errors_here && streak[clef][qi] < 9) streak[clef][qi]++;
    if (qi == new_note) new_note = -1;
    check_unlock();
    nv_save();
    if (new_note >= 0 && queue[1] == new_note) msg_timer = 0;
    else if (fast == 10 && series >= 5) say(0, 0, "BRAVO !", P_GREEN, 0, 0);
    else clear_rows(21, 23);
    slide = QSTEP;
  } else {
    if (x >= 0 && x < KEYS) key_state[x] = 1;
    wrong++; series = 0; errors_here++; frames_here = 0;
    streak[clef][qi] = 0;
    sname(t, tn, sizeof tn, true);
    if ((n - 1) % 12 == (t->note - 1) % 12) {
      snprintf(a, sizeof a, "C'EST BIEN UN %s,", tn);
      snprintf(b, sizeof b, "MAIS PAS CETTE OCTAVE : DO%d A SI%d", octave(t->note), octave(t->note));
      say(a, P_YELLOW, b, P_WHITE, 0, 0);
    } else {
      snprintf(a, sizeof a, "VOUS AVEZ JOUE %s%d", played_name(n), octave(n));
      if (errors_here >= 2) {
        snprintf(b, sizeof b, "C'EST UN %s,", tn);
        if (t->acc) snprintf(c, sizeof c, "LA TOUCHE %s DU %s", t->acc > 0 ? "A DROITE" : "A GAUCHE", letters[(t->note - t->acc - 1) % 12]);
        else where(t->step, c, sizeof c);
        int tx = t->note - LOWKEY; if (tx >= 0 && tx < KEYS) key_state[tx] = 3;
        say(a, P_RED, b, P_WHITE, c, P_WHITE);
      } else say(a, P_RED, "REGARDEZ BIEN LA NOTE BLEUE", P_WHITE, 0, 0);
    }
    mt_note(MT_PSG(0), 25, &buzz, 24);
    nv_save();
  }
  draw_progress();
}
static void release_key(int n) {
  int x = n - LOWKEY;
  if (x >= 0 && x < KEYS && key_state[x] != 3) key_state[x] = 0;
}
static void play_frame(void) {
  frames_here++;
  if (slide) {
    slide -= 10;
    if (slide <= 0) {
      slide = 0;
      for (int q = 0; q < QN - 1; q++) queue[q] = queue[q + 1];
      queue[QN - 1] = pick();
      errors_here = 0; frames_here = 0;
      for (int x = 0; x < KEYS; x++) if (key_state[x] == 3) key_state[x] = 0;
      if (new_note >= 0 && queue[0] == new_note) introduce(new_note);
    }
  }
  if (msg_timer && --msg_timer == 0) clear_rows(21, 23);
  draw_notes();
}

// ================================================================ scrolling reading
// The notes come from the right at a steady pace and cross the reading line: play each one as it
// passes. Nothing waits: a wrong note or a note let go simply counts against the score. The progress
// of the clef is left as it is; the results say which notes to work on.
#define SN 40                               // notes in a session
#define WIN 30                              // pixels on each side of the line where a note can be played
static const int speeds_q[3] = { 13, 20, 30 };         // 1/16 pixel per frame: 1.9 s, 1.3 s and 0.85 s a note
static const char *const speed_txt[3] = { "LENTE", "MOYENNE", "RAPIDE" };
typedef struct { uint8_t idx, state; int32_t x; } snote_run_t;        // state: 0 to play, 1 right, 2 wrong, 3 let go
static snote_run_t run[SN];
static int run_first, s_right, s_wrong, s_missed, s_perfect, ticked;
static uint8_t s_bad[NNOTES];

static void scroll_hud(void) {
  char s[48];
  clear_rows(0, 2);
  snprintf(s, sizeof s, "%s  %s", clef_name[clef], speed_txt[speed_sel]); text(1, 0, s, P_YELLOW);
  snprintf(s, sizeof s, "SCORE %d", score); text(39 - (int)strlen(s), 0, s, P_WHITE);
  snprintf(s, sizeof s, "JUSTES %d  FAUSSES %d  OUBLIEES %d", s_right, s_wrong, s_missed); text(1, 1, s, P_GREY);
  if (series >= 3) { snprintf(s, sizeof s, "SERIE %d", series); text(39 - (int)strlen(s), 2, s, P_CYAN); }
}
static void start_scroll(void) {
  score = series = best_series = 0; s_right = s_wrong = s_missed = s_perfect = 0;
  memset(s_bad, 0, sizeof s_bad);
  last_pick = -1;
  for (int i = 0; i < SN; i++) { run[i].idx = (uint8_t)pick(); run[i].state = 0; run[i].x = (700 + i * QSTEP) * 16; }
  run_first = 0; ticked = -1;
  memset(key_state, 0, sizeof key_state);
  draw_staff(true); clear_rows(0, 29);
  scroll_hud();
  say("JOUEZ CHAQUE NOTE SUR LA LIGNE JAUNE", P_WHITE, 0, 0, "UNE ERREUR NE BLOQUE RIEN : ELLE COMPTE", P_GREY);
  state = S_SCROLL;
}
static void scroll_answer(int n, int vol) {
  sound(n, vol);
  int best = -1, bd = 1 << 30;                // the note to play: the closest one to the line, inside the window
  for (int i = run_first; i < SN; i++) {
    if (run[i].state) continue;
    int d = run[i].x / 16 - QX; if (d < 0) d = -d;
    if (d <= WIN && d < bd) { bd = d; best = i; }
    if (run[i].x / 16 > QX + WIN) break;
  }
  int x = n - LOWKEY;
  if (best < 0) { if (x >= 0 && x < KEYS) key_state[x] = 1; return; }    // nothing on the line: nothing counts
  snote_run_t *r = &run[best];
  const snote_t *t = &clefs[clef][r->idx];
  if (n == t->note) {
    r->state = 1; s_right++; series++; if (series > best_series) best_series = series;
    int perfect = bd <= 8;
    if (perfect) s_perfect++;
    score += 10 + (perfect ? 10 : 0) + (series > 20 ? 20 : series);
    if (x >= 0 && x < KEYS) key_state[x] = 2;
  } else {
    r->state = 2; s_wrong++; series = 0; s_bad[r->idx]++;
    if (x >= 0 && x < KEYS) key_state[x] = 1;
    char a[48], tn[16]; sname(t, tn, sizeof tn, true);
    snprintf(a, sizeof a, "C'ETAIT UN %s%d (VOUS : %s%d)", tn, octave(t->note - t->acc), played_name(n), octave(n));
    say(0, 0, a, P_RED, 0, 0); msg_timer = 60;
  }
  scroll_hud();
}
static void show_results(void) {
  char s[64];
  hide_from(0);
  draw_staff(false); clear_rows(0, 29);
  int pc = s_right * 100 / SN;
  textc(3, "R E S U L T A T S", P_YELLOW);
  snprintf(s, sizeof s, "%s, VITESSE %s", clef_name[clef], speed_txt[speed_sel]); textc(5, s, P_GREY);
  snprintf(s, sizeof s, "NOTES JUSTES   %2d / %d  (%d %%)", s_right, SN, pc); textc(8, s, P_GREEN);
  snprintf(s, sizeof s, "DONT PILE SUR LA LIGNE  %2d", s_perfect); textc(9, s, P_WHITE);
  snprintf(s, sizeof s, "FAUSSES NOTES  %2d", s_wrong); textc(11, s, P_RED);
  snprintf(s, sizeof s, "OUBLIEES       %2d", s_missed); textc(12, s, P_GREY);
  snprintf(s, sizeof s, "SCORE %d   MEILLEURE SERIE %d", score, best_series); textc(14, s, P_YELLOW);
  char line[64] = "";                         // the notes to work on: the ones most often wrong or let go
  for (int k = 0; k < 4; k++) {
    int bi = -1, bv = 0;
    for (int i = 0; i < NNOTES; i++) if (s_bad[i] > bv) { bv = s_bad[i]; bi = i; }
    if (bi < 0) break;
    char nm[12]; sname(&clefs[clef][bi], nm, sizeof nm, false);
    snprintf(s, sizeof s, "%s%s%d (%d)", line[0] ? "  " : "", nm, octave(clefs[clef][bi].note - clefs[clef][bi].acc), bv);
    if (strlen(line) + strlen(s) < 38) strcat(line, s);
    s_bad[bi] = 0;
  }
  if (line[0]) { textc(17, "A REVOIR :", P_WHITE); textc(18, line, P_CYAN); }
  else textc(17, pc == 100 ? "SANS FAUTE ! ESSAYEZ PLUS VITE." : "", P_GREEN);
  textc(21, "UNE NOTE : REJOUER    ENTREE : MENU", P_GREY);
  hide_from(label_sprites(0));
  state = S_RESULTS;
}
static void scroll_frame(void) {
  int sp = sprite_clef(0);
  bool hud = false;
  for (int i = run_first; i < SN; i++) run[i].x -= speeds_q[speed_sel];      // all the notes move together
  for (int i = run_first; i < SN; i++) {
    snote_run_t *r = &run[i];
    int x = r->x / 16;
    if (!r->state && x < QX - WIN) { r->state = 3; s_missed++; series = 0; s_bad[r->idx]++; hud = true; }
    if (x < 40) { if (i == run_first) run_first++; continue; }
    if (x > SW - 24) break;
    if (i > ticked && x <= QX) { ticked = i; mt_note(MT_PSG(1), 61, &tick, 10); }      // a soft click on the beat
    int pal = r->state == 1 ? P_NOTE_OK : r->state >= 2 ? P_NOTE_BAD : (x <= QX + WIN && x >= QX - WIN) ? P_NOTE_CUR : P_NOTE;
    if (sp < 130) sp = sprite_note(sp, &clefs[clef][r->idx], x, pal, show_name(r->idx));
  }
  sp = label_sprites(sp);
  hide_from(sp);
  if (hud) scroll_hud();
  if (msg_timer && --msg_timer == 0) clear_rows(21, 23);
  if (run_first >= SN) show_results();
}

// ================================================================ menu
static void draw_menu(void) {
  char s[64];
  clear_rows(0, 29);
  hide_from(label_sprites(0));
  draw_staff(false);
  textc(2, "S O L F E G E", P_YELLOW);
  textc(3, "APPRENDRE A LIRE LES NOTES", P_GREY);
  static const char *const help_txt[3] = { "TOUJOURS", "AU DEBUT", "JAMAIS" };
  const char *lab[6] = { "CLE", "NOTES", "MODE", "VITESSE", "NOMS", "RECOMMENCER" };
  for (int i = 0; i < 6; i++) {
    int y = 6 + 2 * i;
    text(4, y, lab[i], menu_item == i ? P_WHITE : P_GREY);
    if (menu_item == i) text(2, y, ">", P_YELLOW);
    if (i == 0) snprintf(s, sizeof s, "< %s >", clef_name[clef]);
    else if (i == 1) snprintf(s, sizeof s, "< %d/%d >  %s", unlocked[clef], NNOTES, unlocked[clef] > NNATURAL ? "AVEC # ET b" : "");
    else if (i == 2) snprintf(s, sizeof s, "< %s >", mode ? "LECTURE DEROULANTE" : "LA NOTE ATTEND");
    else if (i == 3) snprintf(s, sizeof s, mode ? "< %s >" : "(LECTURE DEROULANTE)", speed_txt[speed_sel]);
    else if (i == 4) snprintf(s, sizeof s, "< %s >", help_txt[help_mode]);
    else snprintf(s, sizeof s, confirm_reset ? "ENTREE : TOUT EFFACER ?" : "EFFACER LA PROGRESSION");
    text(16, y, s, menu_item == i ? (i == 5 && confirm_reset ? P_RED : P_YELLOW) : i == 3 && !mode ? P_GREY : P_WHITE);
  }
  textc(19, mode ? "40 NOTES DEFILENT, JOUEZ-LES EN RYTHME" : "LA NOTE ATTEND, PUIS UNE NOUVELLE ARRIVE", P_WHITE);
  textc(21, "JOUEZ UNE NOTE OU APPUYEZ SUR ENTREE", P_YELLOW);
  textc(23, "SANS MIDI : W X C V... ET A Z E R...", P_GREY);
  if (mc_hw_version() < 5) textc(24, "IL FAUT LE CORE VERSION 5 (640 X 480)", P_RED);
}
static void menu_action(int a) {                      // 1 up 2 down 3 left 4 right 5 ok
  if (a != 5 || menu_item != 5) confirm_reset = 0;
  if (a == 1) menu_item = (menu_item + 5) % 6;
  else if (a == 2) menu_item = (menu_item + 1) % 6;
  else if (a == 3 || a == 4) {
    if (menu_item == 0) clef ^= 1;
    else if (menu_item == 1) {                       // choose where to start: 13 = the naturals known, sharps and flats next
      int n = unlocked[clef] + (a == 4 ? 1 : -1);
      if (n >= 3 && n <= NNOTES) unlocked[clef] = (uint8_t)n;
    } else if (menu_item == 2) mode ^= 1;
    else if (menu_item == 3) speed_sel = (speed_sel + (a == 4 ? 1 : 2)) % 3;
    else if (menu_item == 4) help_mode = (help_mode + (a == 4 ? 1 : 2)) % 3;
    nv_save();
  } else if (a == 5) {
    if (menu_item == 5) {
      if (confirm_reset) { unlocked[clef] = 3; memset(streak[clef], 0, NNOTES); nv_save(); confirm_reset = 0; }
      else confirm_reset = 1;
    } else { if (mode) start_scroll(); else start_game(); return; }
  }
  draw_menu();
}

static const int16_t piano_keys[][2] = {
  { KEY_Z, 0 }, { KEY_S, 1 }, { KEY_X, 2 }, { KEY_D, 3 }, { KEY_C, 4 }, { KEY_V, 5 }, { KEY_G, 6 },
  { KEY_B, 7 }, { KEY_H, 8 }, { KEY_N, 9 }, { KEY_J, 10 }, { KEY_M, 11 }, { KEY_COMMA, 12 },
  { KEY_L, 13 }, { KEY_PERIOD, 14 }, { KEY_SEMICOLON, 15 }, { KEY_SLASH, 16 },
  { KEY_Q, 12 }, { KEY_2, 13 }, { KEY_W, 14 }, { KEY_3, 15 }, { KEY_E, 16 }, { KEY_R, 17 },
  { KEY_5, 18 }, { KEY_T, 19 }, { KEY_6, 20 }, { KEY_Y, 21 }, { KEY_7, 22 }, { KEY_U, 23 }, { KEY_I, 24 } };
static int piano_key(int k) { for (unsigned i = 0; i < sizeof piano_keys / sizeof piano_keys[0]; i++) if (piano_keys[i][0] == k) return piano_keys[i][1]; return -1; }

static void to_menu(void) { state = S_MENU; nv_save(); memset(key_state, 0, sizeof key_state); draw_menu(); }

int main(void) {
  mc_init();
  video_init();
  mt_init(mtbank_file);
  for (int t = 0; t < MT_TRACKS; t++) mt_reserve(t, true);
  for (int i = 0; i < 512; i++) key_down[i] = -1;
  nv_load();
  draw_keys(true);
  draw_menu();
  for (;;) {
    mc_frame();
    int e;
    while ((e = mc_midi_event()) >= 0) {
      int st = MIDI_STATUS(e), mn = MIDI_DATA1(e);
      if (st == MIDI_NOTE_ON) {
        int vol = (MIDI_DATA2(e) * 64 + 63) / 127;
        if (state == S_MENU) { if (menu_item != 5) { if (mode) start_scroll(); else start_game(); } }
        else if (state == S_RESULTS) start_scroll();
        else if (state == S_SCROLL) scroll_answer(mn - 11, vol);
        else answer(mn - 11, vol);
      } else if (st == MIDI_NOTE_OFF) release_key(mn - 11);
    }
    while ((e = mc_key_event()) >= 0) {
      int k = e & 0x1FF, pk = piano_key(k), base = clef ? 37 : 49;
      if (e & 0x200) {
        if (state == S_MENU)
          menu_action(k == KEY_UP ? 1 : k == KEY_DOWN ? 2 : k == KEY_LEFT ? 3 : k == KEY_RIGHT ? 4 : k == KEY_ENTER ? 5 : 0);
        else if (k == KEY_ESC || (state == S_RESULTS && k == KEY_ENTER)) to_menu();
        else if (pk >= 0) {
          if (state == S_RESULTS) start_scroll();
          else if (state == S_SCROLL) scroll_answer(base + pk, 50);
          else answer(base + pk, 50);
          key_down[k] = (int8_t)(base + pk);
        }
      } else if (key_down[k] >= 0) { release_key(key_down[k]); key_down[k] = -1; }
    }
    uint32_t p = mc_pad_pressed(1);
    if (state == S_MENU) {
      if (p & IN_UP) menu_action(1);
      if (p & IN_DOWN) menu_action(2);
      if (p & IN_LEFT) menu_action(3);
      if (p & IN_RIGHT) menu_action(4);
      if (p & (IN_B(1) | IN_START)) menu_action(5);
    } else {
      if (p & IN_B(2) || (state == S_RESULTS && p & (IN_B(1) | IN_START))) to_menu();
      else if (state == S_PLAY) play_frame();
      else if (state == S_SCROLL) scroll_frame();
    }
    draw_keys(false);
  }
}
