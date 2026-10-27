// SOLFEGE: learning to read notes on the staff, with a MIDI keyboard (or the computer keyboard).
//
// The notes to read wait on a staff, the first one at the reading line; play it on the keyboard drawn
// at the bottom. There is no clock: the note stays until it is played. The game starts with three
// notes (do, re, mi) and brings in a new one each time all those already met are well known (three
// right answers in a row each). Treble clef (do 4 to la 5) and bass clef (mi 2 to do 4); the progress
// of each clef is kept in the NVRAM.
#include <stdio.h>
#include <string.h>
#include "mc.h"
#include "mtrack.h"

#define COLS 48
#define LOWKEY 25                 // tracker note of the first key on screen (C-2: MIDI 36)
#define Y0 120                    // the bottom line of the staff
#define QX 80                     // the reading line
#define QN 6                      // notes on the staff
#define QSTEP 48
#define NVB ((volatile uint8_t *)NVRAM_BASE)

enum { T_PAPER = 96, T_LINE, T_MARK, T_KEY_W, T_KEY_B, T_SPR = 112 };
enum { SP_HEAD = T_SPR, SP_LEDGER = T_SPR + 4, SP_TREBLE = T_SPR + 8, SP_BASS = T_SPR + 18, SP_SHARP = T_SPR + 22, SP_FLAT = T_SPR + 24 };
enum { P_WHITE, P_YELLOW, P_GREEN, P_RED, P_GREY, P_CYAN, P_INK, P_INK_GREY, P_PAPER = 8, P_NOTE, P_NOTE_CUR, P_NOTE_OK,
       P_NOTE_BAD, P_KEY_W, P_KEY_B, P_KEY_ON, P_KEY_OK, P_KEY_HINT, P_CLEF };

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
static const char *const names[12] = { "DO", "DO#", "RE", "RE#", "MI", "FA", "FA#", "SOL", "SOL#", "LA", "LA#", "SI" };
static const char *const black_names[12] = { 0, "DO#/REb", 0, "RE#/MIb", 0, 0, "FA#/SOLb", 0, "SOL#/LAb", 0, "LA#/SIb", 0 };
static const char *played_name(int n) { int c = (n - 1) % 12; return black_names[c] ? black_names[c] : names[c]; }
static const char *const letters[12] = { "DO", 0, "RE", 0, "MI", "FA", 0, "SOL", 0, "LA", 0, "SI" };
// the name as written: the letter of the line or space, then # or b
static void sname(const snote_t *n, char *s, int len, bool word) {
  const char *l = letters[(n->note - n->acc - 1) % 12];
  if (!n->acc) snprintf(s, len, "%s", l);
  else if (word) snprintf(s, len, "%s %s", l, n->acc > 0 ? "DIESE" : "BEMOL");
  else snprintf(s, len, "%s%s", l, n->acc > 0 ? "#" : "b");
}
static int octave(int n) { return (n - 1) / 12; }
static void where(int step, char *s, int len) {               // where the note sits on the staff
  if (step == -2) snprintf(s, len, "SUR UNE PETITE LIGNE EN DESSOUS");
  else if (step == -1) snprintf(s, len, "SOUS LA PORTEE");
  else if (step == 9) snprintf(s, len, "AU-DESSUS DE LA PORTEE");
  else if (step == 10) snprintf(s, len, "SUR UNE PETITE LIGNE AU-DESSUS");
  else if (step % 2 == 0) snprintf(s, len, "SUR LA %de LIGNE", step / 2 + 1);
  else snprintf(s, len, "DANS LE %de INTERLIGNE", (step + 1) / 2);
}

// ================================================================ drawing helpers (as in CASCADE)
static volatile uint32_t *L0, *L1, *L2;
static void put(volatile uint32_t *m, int x, int y, int t, int pal) { if (x >= 0 && x < 64 && y >= 0 && y < 64) m[y * 64 + x] = TILE(t, pal); }
static void text(int x, int y, const char *s, int pal) {
  for (; *s && x < COLS; s++, x++) { int c = (unsigned char)*s; put(L0, x, y, (c < 32 || c > 127 ? '?' : c) - 32, pal); }
}
static void textc(int y, const char *s, int pal) { text((COLS - (int)strlen(s)) / 2, y, s, pal); }
static void clear_row(int y) { for (int x = 0; x < COLS; x++) L0[y * 64 + x] = 0; }
static void clear_rows(int a, int b) { for (int y = a; y <= b; y++) clear_row(y); }
static void tile_px(uint8_t *g, int x, int y, int c) { uint8_t *b = &g[y * 4 + (x >> 1)]; *b = (uint8_t)((x & 1) ? (*b & 0x0F) | (c << 4) : (*b & 0xF0) | c); }
static void make_tile(int n, const char *rows) {
  static uint8_t g[32];
  memset(g, 0, 32);
  for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) { char c = rows[y * 8 + x]; if (c >= '1' && c <= '9') tile_px(g, x, y, c - '0'); }
  mc_chr_upload(n * 32, g, 32);
}
// a drawing of w x h cells (one character per pixel), stored row major from cell n
static void make_big(int n, int w, int h, const char *const *rows) {
  static uint8_t g[32];
  for (int cy = 0; cy < h; cy++) for (int cx = 0; cx < w; cx++) {
    memset(g, 0, 32);
    for (int y = 0; y < 8; y++) {
      const char *r = rows[cy * 8 + y];
      for (int x = 0; x < 8; x++) { char c = r[cx * 8 + x]; if (c >= '1' && c <= '9') tile_px(g, x, y, c - '0'); }
    }
    mc_chr_upload((n + cy * w + cx) * 32, g, 32);
  }
}
static void set_pal(int p, uint16_t a, uint16_t b, uint16_t c) { PALETTE[p * 16 + 1] = a; PALETTE[p * 16 + 2] = b; PALETTE[p * 16 + 3] = c; }

static const char *const head_art[16] = {
  "................", "................", "................", "................",
  "....11111111....", "..111.....1111..", ".111.......1111.", ".111........111.",
  ".111........111.", ".1111.......111.", "..1111.....111..", "....11111111....",
  "................", "................", "................", "................" };
static const char *const ledger_art[16] = {
  "................", "................", "................", "................", "................", "................", "................", "................",
  "1111111111111111", "................", "................", "................", "................", "................", "................", "................" };
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

static void video_init(void) {
  mc_video_mode(384, 240);
  mc_chr_upload(0, font_chr, 96 * 32);
  make_tile(T_PAPER, "11111111" "11111111" "11111111" "11111111" "11111111" "11111111" "11111111" "11111111");
  make_tile(T_LINE,  "22222222" "11111111" "11111111" "11111111" "11111111" "11111111" "11111111" "11111111");
  make_tile(T_MARK,  "33333333" "33333333" "33333333" "33333333" "33333333" "33333333" "33333333" "33333333");
  make_tile(T_KEY_W, "11111113" "11111113" "11111113" "11111113" "11111113" "11111113" "11111113" "11111113");
  make_tile(T_KEY_B, "1111111." "1111111." "1111111." "1111111." "1111111." "1111111." "1111111." "1111111.");
  make_big(SP_HEAD, 2, 2, head_art);
  make_big(SP_LEDGER, 2, 2, ledger_art);
  make_big(SP_TREBLE, 2, 5, treble_art);
  make_big(SP_BASS, 2, 2, bass_art);
  make_big(SP_SHARP, 1, 2, sharp_art);
  make_big(SP_FLAT, 1, 2, flat_art);
  for (int p = 0; p < 32; p++) for (int i = 0; i < 16; i++) PALETTE[p * 16 + i] = 0;
  static const uint16_t fg[8] = { RGB555(240, 240, 240), RGB555(250, 220, 80), RGB555(110, 230, 120), RGB555(250, 90, 80),
                                  RGB555(120, 126, 150), RGB555(90, 210, 240), RGB555(40, 36, 30), RGB555(140, 130, 110) };
  for (int p = 0; p < 8; p++) set_pal(p, fg[p], p >= 6 ? 0 : RGB555(10, 10, 20), 0);
  set_pal(P_PAPER, RGB555(244, 236, 214), RGB555(60, 54, 46), RGB555(252, 238, 170));
  set_pal(P_NOTE, RGB555(40, 36, 30), 0, 0);
  set_pal(P_NOTE_CUR, RGB555(40, 90, 200), 0, 0);
  set_pal(P_NOTE_OK, RGB555(40, 160, 70), 0, 0);
  set_pal(P_NOTE_BAD, RGB555(220, 50, 40), 0, 0);
  set_pal(P_KEY_W, RGB555(230, 230, 238), 0, RGB555(110, 110, 130));
  set_pal(P_KEY_B, RGB555(34, 34, 46), 0, 0);
  set_pal(P_KEY_ON, RGB555(220, 80, 70), 0, RGB555(120, 40, 40));
  set_pal(P_KEY_OK, RGB555(110, 230, 120), 0, RGB555(40, 120, 60));
  set_pal(P_KEY_HINT, RGB555(250, 210, 80), 0, RGB555(150, 120, 40));
  set_pal(P_CLEF, RGB555(40, 36, 30), 0, 0);
  PALETTE[31 * 16 + 1] = RGB555(14, 16, 30);
  mc_background(31 * 16 + 1);
  mc_layer(0, MC_CHR(0), MC_TILE8 | MC_4BPP, 64, 64, 3);
  mc_layer(1, MC_CHR(0), MC_TILE8 | MC_4BPP, 64, 64, 1);
  mc_layer(2, MC_CHR(0), MC_TILE8 | MC_4BPP, 64, 64, 0);
  mc_tilemap_fill(0, 0); mc_tilemap_fill(1, 0); mc_tilemap_fill(2, 0);
  L0 = mc_tilemap(0); L1 = mc_tilemap(1); L2 = mc_tilemap(2);
  mc_sprite_sheet(MC_CHR(0));
  mc_display(DISP_ON | DISP_LAYER(0) | DISP_LAYER(1) | DISP_LAYER(2) | DISP_SPRITES);
}

// the staff: a sheet of paper (rows 7 to 18), five lines from y 88 to y 120, the reading mark
static void draw_staff(bool show) {
  for (int y = 7; y <= 18; y++) for (int x = 0; x < COLS; x++) {
    int t = !show ? 0 : (y >= 11 && y <= 15 && x >= 1 && x < 47) ? T_LINE : T_PAPER;
    put(L2, x, y, t, P_PAPER);
    put(L1, x, y, show && x >= 9 && x <= 11 && y >= 8 && y <= 17 ? T_MARK : 0, P_PAPER);
  }
}

// ================================================================ keyboard on screen
static bool black(int n) { int s = (n - 1) % 12; return s == 1 || s == 3 || s == 6 || s == 8 || s == 10; }
static uint8_t key_state[COLS], key_shown[COLS];     // 0 up, 1 wrong, 2 right, 3 hint
static void draw_keys(bool all) {
  for (int x = 0; x < COLS; x++) {
    if (!all && key_shown[x] == key_state[x]) continue;
    key_shown[x] = key_state[x];
    int n = LOWKEY + x, st = key_state[x];
    int pal = st == 2 ? P_KEY_OK : st == 1 ? P_KEY_ON : st == 3 ? P_KEY_HINT : black(n) ? P_KEY_B : P_KEY_W;
    for (int y = 25; y < 29; y++) {
      bool under = black(n) && y == 28 && !st;
      put(L0, x, y, under || !black(n) ? T_KEY_W : T_KEY_B, under ? P_KEY_W : pal);
    }
  }
  if (all) {
    clear_row(29);
    for (int x = 0; x < COLS; x++) {
      int n = LOWKEY + x;
      if ((n - 1) % 12 == 0) { char s[4] = { 'D', 'O', (char)('0' + octave(n)), 0 }; text(x, 29, s, P_GREY); }
    }
  }
}

// ================================================================ progress (NVRAM)
static int clef, help_mode = 1;          // help: 0 names always, 1 for the notes still learnt, 2 never
static uint8_t unlocked[2], streak[2][NNOTES];
static void nv_load(void) {
  unlocked[0] = unlocked[1] = 3; memset(streak, 0, sizeof streak);
  if (NVB[0] == 'S' && NVB[1] == 'O' && NVB[2] == 'L' && NVB[3] == 'F') {
    help_mode = NVB[6] % 3; clef = NVB[7] & 1;
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
  NVB[6] = (uint8_t)help_mode; NVB[7] = (uint8_t)clef;
  for (int c = 0; c < 2; c++) {
    NVB[8 + 32 * c] = unlocked[c];
    for (int i = 0; i < NNOTES; i++) NVB[9 + 32 * c + i] = streak[c][i];
  }
}

// ================================================================ the game
enum { S_MENU, S_PLAY };
static int state, menu_item, confirm_reset;
static int queue[QN];                    // indices into clefs[clef]
static int slide, last_pick = -1, errors_here, new_note = -1;
static int score, series, best_series, right, wrong, frames_here;
static int msg_timer;
static int8_t key_down[512], midi_down[128];
static mt_instrument_t piano = { "PIANO", MT_TYPE_PCM, 10, 0, 0, 56, 128, 0, 30, 20, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {0, 0} };
static mt_instrument_t buzz = { "BUZZ", MT_TYPE_PSG, PSG_SQUARE, 40, 0, 20, 128, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, -6, 0, 0, 0, {0, 0} };
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

static void say(const char *a, int pa, const char *b, int pb) {
  clear_rows(20, 22);
  if (a) textc(20, a, pa);
  if (b) textc(22, b, pb);
  msg_timer = 150;
}
static void draw_progress(void) {
  char s[48];
  clear_rows(0, 5);
  snprintf(s, sizeof s, "%s", clef_name[clef]); text(1, 0, s, P_YELLOW);
  snprintf(s, sizeof s, "SCORE %d", score); text(47 - (int)strlen(s), 0, s, P_WHITE);
  snprintf(s, sizeof s, "NOTES %d/%d", unlocked[clef], NNOTES); text(1, 1, s, P_GREY);
  if (series >= 3) { snprintf(s, sizeof s, "SERIE %d", series); text(47 - (int)strlen(s), 1, s, P_CYAN); }
  // the notes met so far: grey still to learn, white on the way, green known
  int x = 1, y = 3;
  for (int i = 0; i < unlocked[clef]; i++) {
    const snote_t *n = &clefs[clef][i];
    char nm[12]; sname(n, nm, sizeof nm, false);
    snprintf(s, sizeof s, "%s%d", nm, octave(n->note - n->acc));
    if (x + (int)strlen(s) > 47) { x = 1; y++; }
    text(x, y, s, streak[clef][i] >= 3 ? P_GREEN : streak[clef][i] ? P_WHITE : P_GREY);
    x += (int)strlen(s) + 1;
  }
}
static void draw_notes(void) {
  int sp = 0;
  mc_sprite(sp++, 16, clef ? Y0 - 24 - 5 : Y0 - 8 - 18, clef ? SP_BASS : SP_TREBLE, 2, clef ? 2 : 5, P_CLEF, 2);
  clear_row(17);
  for (int q = 0; q < QN; q++) {
    const snote_t *n = &clefs[clef][queue[q]];
    int x = QX + q * QSTEP + slide, y = Y0 - n->step * 4;
    if (x > 360) continue;
    int pal = q == 0 ? (errors_here && (mc_frames & 8) && frames_here < 20 ? P_NOTE_BAD : P_NOTE_CUR) : P_NOTE;
    if (q == 0 && slide) pal = P_NOTE_OK;
    for (int s = -2; s >= n->step; s -= 2) mc_sprite(sp++, x - 1, Y0 - s * 4 - 8, SP_LEDGER, 2, 2, P_NOTE, 2);
    for (int s = 10; s <= n->step; s += 2) mc_sprite(sp++, x - 1, Y0 - s * 4 - 8, SP_LEDGER, 2, 2, P_NOTE, 2);
    mc_sprite(sp++, x, y - 8, SP_HEAD, 2, 2, pal, 3);
    if (n->acc) mc_sprite(sp++, x - 9, y - 8, n->acc > 0 ? SP_SHARP : SP_FLAT, 1, 2, pal == P_NOTE ? P_NOTE : pal, 3);
    if (show_name(queue[q]) && !slide) {
      char nm[12]; sname(n, nm, sizeof nm, false);
      text((x + 8) / 8 - (int)strlen(nm) / 2, 17, nm, q == 0 ? P_INK : P_INK_GREY);
    }
  }
  while (sp < 32) mc_sprite_hide(sp++);
}

static void new_queue(void) {
  last_pick = -1;
  for (int q = 0; q < QN; q++) queue[q] = pick();
  // the newest note first if it has never been answered
  int nn = unlocked[clef] - 1;
  if (streak[clef][nn] == 0 && unlocked[clef] > 3) { queue[0] = nn; new_note = nn; }
  slide = 0; errors_here = 0; frames_here = 0;
}
static void introduce(int i) {
  char a[48], b[48], w[40];
  const snote_t *n = &clefs[clef][i];
  char nm[16]; sname(n, nm, sizeof nm, true);
  snprintf(a, sizeof a, "NOUVELLE NOTE : %s", nm);
  where(n->step, w, sizeof w); snprintf(b, sizeof b, "%s", w);
  if (n->acc > 0) snprintf(b, sizeof b, "LE DIESE MONTE D'UN DEMI-TON : TOUCHE DE DROITE");
  else if (n->acc < 0) snprintf(b, sizeof b, "LE BEMOL BAISSE D'UN DEMI-TON : TOUCHE DE GAUCHE");
  say(a, P_YELLOW, b, P_WHITE);
  memset(key_state, 0, sizeof key_state);
  if (n->note - LOWKEY >= 0 && n->note - LOWKEY < COLS) key_state[n->note - LOWKEY] = 3;
}

static void start_game(void) {
  score = series = best_series = right = wrong = 0;
  memset(key_state, 0, sizeof key_state);
  draw_staff(true); clear_rows(0, 24);
  new_queue();
  draw_progress();
  if (new_note >= 0) introduce(new_note);
  else say("JOUEZ LA NOTE BLEUE", P_WHITE, "LA NOTE ATTEND : PRENEZ VOTRE TEMPS", P_GREY);
  state = S_PLAY;
}

static void check_unlock(void) {
  int n = unlocked[clef];
  for (int i = 0; i < n; i++) if (streak[clef][i] < 3) return;
  if (n >= NNOTES) return;
  unlocked[clef]++;
  nv_save();
  int nn = unlocked[clef] - 1;
  queue[1] = nn; new_note = nn;                               // it comes next
}

static void sound(int n, int vol) { mt_note(MT_PCM(voice), n, &piano, vol); voice = (voice + 1) % 6; }

static void answer(int n, int vol) {
  if (slide) return;
  int qi = queue[0];
  const snote_t *t = &clefs[clef][qi];
  sound(n, vol);
  int x = n - LOWKEY;
  char a[48], b[48], w[40];
  if (n == t->note) {
    if (x >= 0 && x < COLS) key_state[x] = 2;
    right++; series++; if (series > best_series) best_series = series;
    int fast = frames_here < 90 ? 10 : frames_here < 180 ? 5 : 0;
    score += 10 + fast + (series > 10 ? 10 : series);
    if (!errors_here && streak[clef][qi] < 9) streak[clef][qi]++;
    if (qi == new_note) new_note = -1;
    check_unlock();
    nv_save();
    if (new_note >= 0 && queue[1] == new_note) { msg_timer = 0; }
    else if (fast == 10 && series >= 5) say("BRAVO !", P_GREEN, 0, 0);
    else clear_rows(20, 22);
    slide = QSTEP;
  } else {
    if (x >= 0 && x < COLS) key_state[x] = 1;
    wrong++; series = 0; errors_here++; frames_here = 0;
    streak[clef][qi] = 0;
    char tn[16]; sname(t, tn, sizeof tn, true);
    if ((n - 1) % 12 == (t->note - 1) % 12) {
      snprintf(a, sizeof a, "C'EST BIEN UN %s,", tn);
      snprintf(b, sizeof b, "MAIS PAS CETTE OCTAVE : DO%d A SI%d", octave(t->note), octave(t->note));
      say(a, P_YELLOW, b, P_WHITE);
    } else {
      snprintf(a, sizeof a, "VOUS AVEZ JOUE %s", played_name(n));
      if (errors_here >= 2) {
        if (t->acc) snprintf(b, sizeof b, "C'EST UN %s : LA TOUCHE %s DU %s", tn, t->acc > 0 ? "A DROITE" : "A GAUCHE",
                             letters[(t->note - t->acc - 1) % 12]);
        else { where(t->step, w, sizeof w); snprintf(b, sizeof b, "C'EST UN %s, %s", tn, w); }
        int tx = t->note - LOWKEY; if (tx >= 0 && tx < COLS) key_state[tx] = 3;
      } else snprintf(b, sizeof b, "REGARDEZ BIEN LA NOTE BLEUE");
      say(a, P_RED, b, P_WHITE);
    }
    mt_note(MT_PSG(0), 25, &buzz, 24);
    nv_save();
  }
  draw_progress();
}
static void release_key(int n) {
  int x = n - LOWKEY;
  if (x >= 0 && x < COLS && key_state[x] != 3) key_state[x] = 0;
}

static void play_frame(void) {
  frames_here++;
  if (slide) {
    slide -= 6;
    if (slide <= 0) {
      slide = 0;
      for (int q = 0; q < QN - 1; q++) queue[q] = queue[q + 1];
      queue[QN - 1] = pick();
      errors_here = 0; frames_here = 0;
      for (int x = 0; x < COLS; x++) if (key_state[x] == 3) key_state[x] = 0;
      if (new_note >= 0 && queue[0] == new_note) introduce(new_note);
    }
  }
  if (msg_timer && --msg_timer == 0) clear_rows(20, 22);
  draw_notes();
  draw_keys(false);
}

// ================================================================ menu
static void draw_menu(void) {
  char s[64];
  clear_rows(0, 24);
  for (int i = 0; i < 32; i++) mc_sprite_hide(i);
  draw_staff(false);
  textc(3, "S O L F E G E", P_YELLOW);
  textc(5, "APPRENDRE A LIRE LES NOTES", P_GREY);
  static const char *const help_txt[3] = { "TOUJOURS", "LE TEMPS DE LES APPRENDRE", "JAMAIS" };
  const char *lab[4] = { "CLE", "NOTES", "NOMS", "RECOMMENCER" };
  for (int i = 0; i < 4; i++) {
    int y = 9 + 2 * i;
    text(6, y, lab[i], menu_item == i ? P_WHITE : P_GREY);
    if (menu_item == i) text(4, y, ">", P_YELLOW);
    if (i == 0) snprintf(s, sizeof s, "< %s >", clef_name[clef]);
    else if (i == 1) snprintf(s, sizeof s, "< %d/%d >  %s", unlocked[clef], NNOTES, unlocked[clef] > NNATURAL ? "AVEC # ET b" : "");
    else if (i == 2) snprintf(s, sizeof s, "< %s >", help_txt[help_mode]);
    else snprintf(s, sizeof s, confirm_reset ? "ENTREE ENCORE : TOUT EFFACER ?" : "EFFACER LA PROGRESSION");
    text(18, y, s, menu_item == i ? (i == 3 && confirm_reset ? P_RED : P_YELLOW) : P_WHITE);
  }
  textc(17, "UNE NOTE ATTEND SUR LA PORTEE : JOUEZ-LA.", P_WHITE);
  textc(18, "TOUTES BIEN CONNUES : UNE NOUVELLE NOTE ARRIVE", P_WHITE);
  textc(20, "JOUEZ UNE NOTE OU APPUYEZ SUR ENTREE", P_YELLOW);
  textc(22, clef ? "SANS MIDI : W X C V... = DO3, A Z E R... = DO4" : "SANS MIDI : W X C V... = DO4, A Z E R... = DO5", P_GREY);
  if (mc_hw_version() < 4) textc(23, "CE CORE N'A PAS D'ENTREE MIDI (VERSION 4)", P_RED);
}
static void menu_action(int a) {                      // 1 up 2 down 3 left 4 right 5 ok
  if (a != 5 || menu_item != 3) confirm_reset = 0;
  if (a == 1) menu_item = (menu_item + 3) % 4;
  else if (a == 2) menu_item = (menu_item + 1) % 4;
  else if (a == 3 || a == 4) {
    if (menu_item == 0) clef ^= 1;
    else if (menu_item == 1) {                     // choose where to start: 13 = the naturals known, sharps and flats next
      int n = unlocked[clef] + (a == 4 ? 1 : -1);
      if (n >= 3 && n <= NNOTES) unlocked[clef] = (uint8_t)n;
    } else if (menu_item == 2) help_mode = (help_mode + (a == 4 ? 1 : 2)) % 3;
    nv_save();
  } else if (a == 5) {
    if (menu_item == 3) {
      if (confirm_reset) { unlocked[clef] = 3; memset(streak[clef], 0, NNOTES); nv_save(); confirm_reset = 0; }
      else confirm_reset = 1;
    } else { start_game(); return; }
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

int main(void) {
  mc_init();
  video_init();
  mt_init(mtbank_file);
  for (int t = 0; t < MT_TRACKS; t++) mt_reserve(t, true);
  for (int i = 0; i < 512; i++) key_down[i] = -1;
  for (int i = 0; i < 128; i++) midi_down[i] = -1;
  nv_load();
  draw_keys(true);
  draw_menu();
  for (;;) {
    mc_frame();
    int e;
    while ((e = mc_midi_event()) >= 0) {
      int st = MIDI_STATUS(e), mn = MIDI_DATA1(e);
      if (st == MIDI_NOTE_ON) {
        if (state == S_MENU) { if (menu_item != 3) start_game(); }
        else answer(mn - 11, (MIDI_DATA2(e) * 64 + 63) / 127);
      } else if (st == MIDI_NOTE_OFF) release_key(mn - 11);
    }
    while ((e = mc_key_event()) >= 0) {
      int k = e & 0x1FF, pk = piano_key(k), base = clef ? 37 : 49;
      if (e & 0x200) {
        if (state == S_MENU) {
          menu_action(k == KEY_UP ? 1 : k == KEY_DOWN ? 2 : k == KEY_LEFT ? 3 : k == KEY_RIGHT ? 4 : k == KEY_ENTER ? 5 : 0);
        } else if (k == KEY_ESC) { state = S_MENU; nv_save(); draw_menu(); }
        else if (pk >= 0) { answer(base + pk, 50); key_down[k] = (int8_t)(base + pk); }
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
      if (p & IN_B(2)) { state = S_MENU; nv_save(); draw_menu(); }
      play_frame();
    }
    draw_keys(false);
  }
}
