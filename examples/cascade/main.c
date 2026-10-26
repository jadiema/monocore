// CASCADE: the falling notes game of MONOCORE-1, played on a MIDI keyboard (or the computer keyboard).
//
// The notes of one track of a MONOTRACKER song fall towards a keyboard drawn at the bottom of the
// screen; play each one when it reaches the line. The song plays the accompaniment (module mtrack),
// the player plays the melody with the song's own instrument. Songs: the ones built into the ROM,
// and those of MONOTRACKER's NVRAM (import monotracker.sav as cascade.sav).
//
// Drawing: three tile layers. Layer 2: the lanes. Layer 1: the notes, written into a ring of 64 map
// rows (one map row = one row of the song) and scrolled smoothly. Layer 0: text, keyboard, hit line.
#include <stdio.h>
#include <string.h>
#include "mc.h"
#include "mtrack.h"

#define COLS 48
#define HIT_Y 200                 // the hit line: top of the keyboard
#define AHEAD 22                  // song rows written above the line (they appear under the score)
#define LEAD 24                   // rows of lead-in before the song starts
#define Q 4096                    // time in 1/4096 of a song row
#define MAXNOTES 1200

// tiles after the 96 of the font
enum { T_SOLID = 96, T_SINGLE, T_BOTTOM, T_BODY, T_TOP, T_LANE_W, T_LANE_B, T_LANE_C, T_KEY_W, T_KEY_B, T_HIT, T_COUNT };
// palettes
enum { P_WHITE, P_YELLOW, P_GREEN, P_RED, P_GREY, P_CYAN, P_LANES = 8, P_NOTE, P_NOTE_HIT, P_NOTE_PERF, P_NOTE_MISS,
       P_KEY_W, P_KEY_B, P_KEY_OK, P_KEY_ON, P_HIT, P_BAND };

#ifndef KEY_COMMA
#define KEY_COMMA 0x41
#define KEY_PERIOD 0x49
#define KEY_SLASH 0x4A
#define KEY_SEMICOLON 0x4C
#endif

// ================================================================ songs
typedef struct { const uint8_t *data; char name[24]; } song_t;
static song_t songs[12];
static int nsongs;
static void add_song(const void *d, const char *title) {
  if (nsongs >= 12 || mt_song_check(d, 0x10000)) return;
  songs[nsongs].data = d;
  memcpy(songs[nsongs].name, mt_song_name(d), 16); songs[nsongs].name[16] = 0;
  if (title) snprintf(songs[nsongs].name, sizeof songs[nsongs].name, "%s", title);
  nsongs++;
}
static void find_songs(void) {
  add_song(au_clair_file, "AU CLAIR DE LA LUNE"); add_song(frere_jacques_file, 0); add_song(ode_joie_file, 0);
  add_song(demo_file, "DEMO DE MONOTRACKER");
  const volatile uint8_t *nv = (const volatile uint8_t *)NVRAM_BASE;
  if (nv[0] != 'M' || nv[1] != 'T' || nv[2] != 'N' || nv[3] != 'V') return;   // MONOTRACKER's NVRAM
  for (int k = 0; k < 8; k++) {
    uint32_t off = 0, size = 0;
    for (int i = 0; i < 4; i++) { off |= (uint32_t)nv[16 + 8 * k + i] << (8 * i); size |= (uint32_t)nv[20 + 8 * k + i] << (8 * i); }
    if (size && off + size <= MC_NVRAM_SIZE) add_song((const void *)(NVRAM_BASE + off), 0);
  }
}

// ================================================================ the part to play
typedef struct { int16_t row0, row1; uint8_t note, inst, state; } gnote_t;   // state: 0 waiting, 1 good, 2 perfect, 3 missed
static gnote_t notes[MAXNOTES];
static int nnotes, total_rows, part_track, base_note;
static int16_t order_base[MT_ORDERS], order_first[MT_ORDERS];
static uint32_t cells[MT_ROWS * MT_TRACKS];
static mt_instrument_t song_ins[MT_INSTRUMENTS];
static int track_notes[MT_TRACKS];
static const mt_header_t *hdr;

// walks the song once, in order, as mtrack plays it (D breaks the pattern, B ends the walk)
static void scan(const song_t *s, int track) {
  hdr = (const mt_header_t *)s->data;
  const uint8_t *orders = s->data + sizeof(mt_header_t) + 32 * hdr->n_ins;
  mt_song_instruments(s->data, song_ins);
  int g = 0, first = 0, inst = 0, open = -1;
  nnotes = 0;
  for (int t = 0; t < MT_TRACKS; t++) track_notes[t] = 0;
  for (int o = 0; o < hdr->n_orders; o++) {
    order_base[o] = (int16_t)g; order_first[o] = (int16_t)first;
    mt_song_decode(s->data, orders[o], cells);
    int next_first = 0, stop = 0, r;
    for (r = first; r < MT_ROWS; r++, g++) {
      int brk = 0;
      for (int t = 0; t < MT_TRACKS; t++) {
        uint32_t c = cells[r * MT_TRACKS + t];
        int fx = MT_C_FX(c) ? MT_C_FX(c) - 1 : -1;
        if (fx == 13) { brk = 1; next_first = MT_C_PARAM(c) & 63; }
        if (fx == 11) stop = 1;
        int n = MT_C_NOTE(c);
        if (n >= 1 && n <= 96) track_notes[t]++;
        if (t != track) continue;
        if (MT_C_INST(c)) inst = MT_C_INST(c);
        if (n && open >= 0) { notes[open].row1 = (int16_t)g; open = -1; }
        if (n >= 1 && n <= 96 && inst && nnotes < MAXNOTES) {
          notes[nnotes] = (gnote_t){ (int16_t)g, (int16_t)(g + 16), (uint8_t)n, (uint8_t)inst, 0 };
          open = nnotes++;
        }
      }
      if (open >= 0 && notes[open].row1 > g + 16) notes[open].row1 = (int16_t)(g + 16);
      if (brk || stop) { r++; g++; break; }
    }
    first = next_first;
    if (stop) break;
  }
  total_rows = g;
  for (int i = 0; i < nnotes; i++) {                 // no note longer than 16 rows, nor past the end
    if (notes[i].row1 > notes[i].row0 + 16) notes[i].row1 = notes[i].row0 + 16;
    if (notes[i].row1 > total_rows) notes[i].row1 = (int16_t)total_rows;
    if (notes[i].row1 <= notes[i].row0) notes[i].row1 = notes[i].row0 + 1;
  }
  // the 4 octaves shown: from the C under the lowest note
  int lo = 96, hi = 1;
  for (int i = 0; i < nnotes; i++) { if (notes[i].note < lo) lo = notes[i].note; if (notes[i].note > hi) hi = notes[i].note; }
  if (!nnotes) lo = hi = 49;
  base_note = (lo - 1) / 12 * 12 + 1;
  if (hi - base_note >= COLS) base_note = hi - COLS + 1;
  if (base_note > 12 && lo - base_note < 6) base_note -= 12 * (hi - base_note + 12 < COLS);
}
static int col_of(int n) { return n - base_note; }
static bool black(int n) { int s = (n - 1) % 12; return s == 1 || s == 3 || s == 6 || s == 8 || s == 10; }

// ================================================================ drawing
static volatile uint32_t *L0, *L1, *L2;
static void put(int x, int y, int tile, int pal) { if (x >= 0 && x < 64 && y >= 0 && y < 64) L0[y * 64 + x] = TILE(tile, pal); }
static void text(int x, int y, const char *s, int pal) {
  for (; *s && x < COLS; s++, x++) { int ch = (unsigned char)*s; put(x, y, (ch < 32 || ch > 127 ? '?' : ch) - 32, pal); }
}
static void textc(int y, const char *s, int pal) { text((COLS - (int)strlen(s)) / 2, y, s, pal); }
static void clear_row(int y) { for (int x = 0; x < COLS; x++) L0[y * 64 + x] = 0; }
static void clear_text(void) { for (int y = 0; y < 30; y++) clear_row(y); }

static void tile_px(uint8_t *g, int x, int y, int c) { uint8_t *b = &g[y * 4 + (x >> 1)]; *b = (uint8_t)((x & 1) ? (*b & 0x0F) | (c << 4) : (*b & 0xF0) | c); }
static void make_tile(int n, const char *rows) {
  static uint8_t g[32];
  memset(g, 0, 32);
  for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) { char c = rows[y * 8 + x]; if (c >= '1' && c <= '9') tile_px(g, x, y, c - '0'); }
  mc_chr_upload(n * 32, g, 32);
}
static void set_pal(int p, uint16_t a, uint16_t b, uint16_t c) { PALETTE[p * 16 + 1] = a; PALETTE[p * 16 + 2] = b; PALETTE[p * 16 + 3] = c; }

static void video_init(void) {
  mc_video_mode(384, 240);
  mc_chr_upload(0, font_chr, 96 * 32);
  make_tile(T_SOLID, "33333333" "33333333" "33333333" "33333333" "33333333" "33333333" "33333333" "33333333");
  // notes, 6 pixels wide: light top-left edge (2), body (1), shadow (3); the start is at the bottom
  make_tile(T_SINGLE, ".222222." "21111113" "21111113" "21111113" "21111113" "21111113" "21111113" ".333333.");
  make_tile(T_BOTTOM, "21111113" "21111113" "21111113" "21111113" "21111113" "21111113" "21111113" ".333333.");
  make_tile(T_BODY,   "21111113" "21111113" "21111113" "21111113" "21111113" "21111113" "21111113" "21111113");
  make_tile(T_TOP,    ".222222." "21111113" "21111113" "21111113" "21111113" "21111113" "21111113" "21111113");
  make_tile(T_LANE_W, "11111111" "11111111" "11111111" "11111111" "11111111" "11111111" "11111111" "11111111");
  make_tile(T_LANE_B, "22222222" "22222222" "22222222" "22222222" "22222222" "22222222" "22222222" "22222222");
  make_tile(T_LANE_C, "31111111" "31111111" "31111111" "31111111" "31111111" "31111111" "31111111" "31111111");
  make_tile(T_KEY_W,  "11111113" "11111113" "11111113" "11111113" "11111113" "11111113" "11111113" "11111113");
  make_tile(T_KEY_B,  "1111111." "1111111." "1111111." "1111111." "1111111." "1111111." "1111111." "1111111.");
  make_tile(T_HIT,    "........" "........" "........" "........" "........" "........" "11111111" "22222222");
  for (int p = 0; p < 32; p++) for (int i = 0; i < 16; i++) PALETTE[p * 16 + i] = 0;
  static const uint16_t fg[6] = { RGB555(240, 240, 240), RGB555(250, 220, 80), RGB555(110, 230, 120), RGB555(250, 90, 80),
                                  RGB555(120, 126, 150), RGB555(90, 210, 240) };
  for (int p = 0; p < 6; p++) set_pal(p, fg[p], RGB555(10, 10, 20), 0);
  set_pal(P_LANES, RGB555(24, 28, 50), RGB555(16, 18, 34), RGB555(60, 70, 120));
  set_pal(P_NOTE, RGB555(242, 160, 60), RGB555(255, 220, 140), RGB555(150, 80, 30));
  set_pal(P_NOTE_HIT, RGB555(90, 200, 110), RGB555(180, 250, 190), RGB555(30, 110, 50));
  set_pal(P_NOTE_PERF, RGB555(250, 230, 90), RGB555(255, 255, 210), RGB555(170, 140, 30));
  set_pal(P_NOTE_MISS, RGB555(90, 60, 70), RGB555(130, 90, 100), RGB555(50, 30, 40));
  set_pal(P_KEY_W, RGB555(230, 230, 238), 0, RGB555(110, 110, 130));
  set_pal(P_KEY_B, RGB555(34, 34, 46), 0, 0);
  set_pal(P_KEY_OK, RGB555(110, 230, 120), 0, RGB555(40, 120, 60));
  set_pal(P_KEY_ON, RGB555(90, 170, 250), 0, RGB555(40, 80, 140));
  set_pal(P_HIT, RGB555(255, 230, 110), RGB555(160, 120, 40), 0);
  set_pal(P_BAND, 0, 0, RGB555(10, 12, 24));
  PALETTE[31 * 16 + 1] = RGB555(8, 10, 20);
  mc_background(31 * 16 + 1);
  mc_layer(0, MC_CHR(0), MC_TILE8 | MC_4BPP, 64, 64, 3);
  mc_layer(1, MC_CHR(0), MC_TILE8 | MC_4BPP, 64, 64, 2);
  mc_layer(2, MC_CHR(0), MC_TILE8 | MC_4BPP, 64, 64, 1);
  mc_tilemap_fill(0, 0); mc_tilemap_fill(1, 0); mc_tilemap_fill(2, 0);
  L0 = mc_tilemap(0); L1 = mc_tilemap(1); L2 = mc_tilemap(2);
  mc_display(DISP_ON | DISP_LAYER(0) | DISP_LAYER(1) | DISP_LAYER(2));
}

static void draw_lanes(void) {
  for (int y = 0; y < 32; y++)
    for (int x = 0; x < COLS; x++) {
      int n = base_note + x;
      int t = y >= 25 ? 0 : black(n) ? T_LANE_B : (n - 1) % 12 == 0 ? T_LANE_C : T_LANE_W;
      L2[y * 64 + x] = TILE(t, P_LANES);
    }
}
static uint8_t key_state[COLS];        // 0 up, 1 pressed, 2 pressed on a good note
static uint8_t key_shown[COLS];
static void draw_keyboard(bool all) {
  for (int x = 0; x < COLS; x++) {
    if (!all && key_shown[x] == key_state[x]) continue;
    key_shown[x] = key_state[x];
    int n = base_note + x, pal = key_state[x] == 2 ? P_KEY_OK : key_state[x] ? P_KEY_ON : black(n) ? P_KEY_B : P_KEY_W;
    for (int y = 25; y < 29; y++) put(x, y, black(n) && y == 28 && !key_state[x] ? T_KEY_W : black(n) ? T_KEY_B : T_KEY_W,
                                      black(n) && y == 28 && !key_state[x] ? P_KEY_W : pal);
  }
  if (all) {
    for (int x = 0; x < COLS; x++) put(x, 24, T_HIT, P_HIT);
    clear_row(29);
    for (int x = 0; x < COLS; x++) {
      int n = base_note + x;
      if ((n - 1) % 12 == 0) { char s[3] = { 'C', (char)('0' + (n - 1) / 12), 0 }; text(x, 29, s, P_GREY); }
    }
  }
}

// notes ring: song row r -> map row (-r - 1) & 63, so that time goes up the screen
static int written, cleared;           // rows written up to, rows cleared up to
static int map_row(int r) { return (-r - 1) & 63; }
static int note_pal(const gnote_t *n) { return n->state == 1 ? P_NOTE_HIT : n->state == 2 ? P_NOTE_PERF : n->state == 3 ? P_NOTE_MISS : P_NOTE; }
static void draw_note_row(const gnote_t *n, int r) {
  int x = col_of(n->note);
  if (x < 0 || x >= COLS) return;
  int t = n->row1 - n->row0 == 1 ? T_SINGLE : r == n->row0 ? T_BOTTOM : r == n->row1 - 1 ? T_TOP : T_BODY;
  L1[map_row(r) * 64 + x] = TILE(t, note_pal(n));
}
static int lo_note;                    // first note that may still be on screen
static void write_rows(int upto) {
  while (written < upto) {
    int r = ++written;
    volatile uint32_t *m = &L1[map_row(r) * 64];
    for (int x = 0; x < COLS; x++) m[x] = 0;
    for (int i = lo_note; i < nnotes && notes[i].row0 <= r; i++)
      if (notes[i].row1 > r) draw_note_row(&notes[i], r);
  }
}
static void clear_rows(int upto) {
  while (cleared < upto) {
    int r = ++cleared;
    volatile uint32_t *m = &L1[map_row(r) * 64];
    for (int x = 0; x < COLS; x++) m[x] = 0;
    while (lo_note < nnotes && notes[lo_note].row1 <= cleared) lo_note++;
  }
}
static void redraw_note(const gnote_t *n) {
  for (int r = n->row0; r < n->row1; r++) if (r > cleared && r <= written) draw_note_row(n, r);
}

// ================================================================ game
enum { S_MENU, S_PLAY, S_END };
static int state, menu_item, song_sel, part_sel, level, tempo_sel = 2;
static const int tempos[3] = { 50, 75, 100 };
static int32_t now_q, rpf_q;           // time and speed, in 1/4096 rows (per frame)
static int started, last_order, last_row, last_g;
static int score, combo, best_combo, n_perfect, n_good, n_miss, n_wrong;
static int best_score[12];
static int popup_timer, popup_pal, midi_seen;
static const char *popup;
static int8_t key_track[512], midi_down[128];
static int pbase = 37;                 // note of the computer keyboard's lower row
static int piano_base(void);

static int parts[MT_TRACKS], nparts;
static void list_parts(void) {
  scan(&songs[song_sel], -1);
  nparts = 0;
  for (int t = 0; t < MT_TRACKS; t++) if (track_notes[t]) parts[nparts++] = t;
  if (part_sel >= nparts) part_sel = 0;
}

static int window_q(int ms) { return (int)((int64_t)ms * rpf_q * 60 / 1000); }
static int perfect_q, good_q;

static void start_game(void) {
  const song_t *s = &songs[song_sel];
  part_track = nparts ? parts[part_sel] : 0;
  scan(s, part_track);
  int bpm = hdr->bpm * tempos[tempo_sel] / 100, speed = hdr->speed ? hdr->speed : 6;
  if (bpm < 32) bpm = 32;
  rpf_q = (int32_t)((int64_t)bpm * 2 * Q / (5 * speed * 60));        // rows per frame
  perfect_q = window_q(level ? 70 : 110);
  good_q = window_q(level ? 150 : 240);
  now_q = -LEAD * Q; started = 0; last_order = -1;
  score = combo = best_combo = n_perfect = n_good = n_miss = n_wrong = 0;
  popup_timer = 0;
  memset(key_state, 0, sizeof key_state);
  mc_tilemap_fill(1, 0);
  written = -LEAD - 1; cleared = -LEAD - 4; lo_note = 0;
  draw_lanes(); clear_text(); draw_keyboard(true);
  mt_stop();
  for (int t = 0; t < MT_TRACKS; t++) mt_reserve(t, t == part_track);
  pbase = piano_base();
  state = S_PLAY;
}

static void show_popup(const char *s, int pal) { popup = s; popup_pal = pal; popup_timer = 40; }

static void press(int n, int vol, int key) {          // a key of the keyboard went down: note n (1-96)
  if (n < 1 || n > 96) return;
  int x = col_of(n);
  int best = -1, bd = 0x7FFFFFFF;
  for (int i = lo_note; i < nnotes && notes[i].row0 * Q <= now_q + good_q; i++) {
    gnote_t *g = &notes[i];
    if (g->state) continue;
    if (level ? g->note != n : (g->note - n) % 12) continue;
    int d = g->row0 * Q - now_q; if (d < 0) d = -d;
    if (d <= good_q && d < bd) { bd = d; best = i; }
  }
  int play = n, inst = nnotes ? notes[lo_note < nnotes ? lo_note : nnotes - 1].inst : 1;
  if (best >= 0) {
    gnote_t *g = &notes[best];
    g->state = bd <= perfect_q ? 2 : 1;
    combo++; if (combo > best_combo) best_combo = combo;
    int mul = 1 + (combo > 30 ? 30 : combo) / 10;
    score += (g->state == 2 ? 300 : 100) * mul;
    if (g->state == 2) n_perfect++; else n_good++;
    show_popup(g->state == 2 ? "PARFAIT !" : "BIEN", g->state == 2 ? P_YELLOW : P_GREEN);
    redraw_note(g);
    play = g->note; inst = g->inst;
    if (x >= 0 && x < COLS) key_state[x] = 2;
  } else {
    if (level) { combo = 0; n_wrong++; }
    if (x >= 0 && x < COLS) key_state[x] = 1;
  }
  if (state == S_PLAY && started) mt_sfx(part_track, play, inst, vol);
  else if (state == S_PLAY) mt_note(part_track, play, &song_ins[inst], vol);   // before the song starts
  if (key >= 0) key_track[key & 511] = (int8_t)x;
}
static void release(int x) {
  if (x >= 0 && x < COLS) key_state[x] = 0;
  bool any = false;
  for (int i = 0; i < COLS; i++) if (key_state[i]) any = true;
  if (!any) mt_note_off(part_track);
}

static void update_time(void) {
  if (!started) {
    now_q += rpf_q;
    if (now_q >= 0) {
      now_q = 0; started = 1;
      mt_play(songs[song_sel].data, 0);
      mt_set_tempo(hdr->speed, hdr->bpm * tempos[tempo_sel] / 100 < 32 ? 32 : hdr->bpm * tempos[tempo_sel] / 100);
      mt_master(48);
      last_order = 0; last_row = 0; last_g = 0;
    }
    return;
  }
  int o = mt_order(), r = mt_row();
  if (o != last_order || r != last_row) {
    if (o < last_order) { mt_stop(); started = 2; return; }   // the song loops: the end of the walk
    last_order = o; last_row = r;
    int g = order_base[o] + r - order_first[o];
    if (g > last_g) { last_g = g; now_q = g * Q; return; }
  }
  if (now_q + rpf_q < (last_g + 1) * Q) now_q += rpf_q;  // within the row, until the next one comes
}

static void play_frame(void) {
  if (started == 2) now_q += rpf_q;                      // after the end: time keeps going for the last notes
  else update_time();
  int now_row = now_q >= 0 ? now_q / Q : -((-now_q + Q - 1) / Q);
  write_rows(now_row + AHEAD);
  clear_rows(now_row - 4);
  mc_layer_scroll(1, 0, (-HIT_Y - (int)(now_q / (Q / 8))) & 511);
  // missed notes
  for (int i = lo_note; i < nnotes && notes[i].row0 * Q + good_q < now_q; i++)
    if (!notes[i].state) {
      notes[i].state = 3; n_miss++; combo = 0;
      show_popup("RATE", P_RED); redraw_note(&notes[i]);
    }
  // heads-up display
  char s[48];
  clear_row(0); clear_row(1);
  text(1, 0, songs[song_sel].name, P_WHITE);
  snprintf(s, sizeof s, "%7d", score); text(40, 0, s, P_YELLOW);
  int judged = n_perfect + n_good + n_miss;
  if (judged) { snprintf(s, sizeof s, "PRECISION %3d%%", (n_perfect * 100 + n_good * 60) / judged); text(1, 1, s, P_GREY); }
  if (combo >= 2) { snprintf(s, sizeof s, "COMBO %d", combo); text(40 - (combo >= 10) - (combo >= 100), 1, s, P_CYAN); }
  clear_row(20);
  if (popup_timer > 0) { popup_timer--; textc(20, popup, popup_pal); }
  else if (!started && now_q < -4 * Q) {
    snprintf(s, sizeof s, "%d", (int)(-now_q / Q / 6) + 1); textc(12, s, P_YELLOW);
  } else clear_row(12);
  draw_keyboard(false);
  // the end
  int last = nnotes ? notes[nnotes - 1].row1 : 0;
  if ((started == 2 || now_q / Q > total_rows) && now_q / Q > last + 8) {
    mt_stop();
    if (score > best_score[song_sel]) best_score[song_sel] = score;
    state = S_END;
  }
}

// ================================================================ menu and results
static void draw_menu(void) {
  char s[64];
  clear_text();
  textc(3, "C A S C A D E", P_YELLOW);
  textc(5, "LE JEU DES NOTES QUI TOMBENT", P_GREY);
  const char *lab[4] = { "MORCEAU", "PARTIE", "NIVEAU", "TEMPO" };
  for (int i = 0; i < 4; i++) {
    int y = 9 + 2 * i;
    text(6, y, lab[i], menu_item == i ? P_WHITE : P_GREY);
    if (menu_item == i) text(4, y, ">", P_YELLOW);
    switch (i) {
      case 0: snprintf(s, sizeof s, "< %s >", songs[song_sel].name); break;
      case 1:
        if (nparts) {
          int t = parts[part_sel], inst = 0;
          scan(&songs[song_sel], t);
          if (nnotes) inst = notes[0].inst;
          snprintf(s, sizeof s, "< %s %d %.10s (%d) >", t < 8 ? "PSG" : "PCM", (t & 7) + 1, inst ? song_ins[inst].name : "", nnotes);
        } else snprintf(s, sizeof s, "(AUCUNE NOTE)");
        break;
      case 2: snprintf(s, sizeof s, "< %s >", level ? "NORMAL" : "FACILE"); break;
      default: snprintf(s, sizeof s, "< %d %% >", tempos[tempo_sel]); break;
    }
    text(16, y, s, menu_item == i ? P_YELLOW : P_WHITE);
  }
  textc(19, level ? "LA BONNE NOTE, A LA BONNE OCTAVE" : "FACILE: TOUTES LES OCTAVES COMPTENT", P_GREY);
  if (best_score[song_sel]) { snprintf(s, sizeof s, "MEILLEUR SCORE %d", best_score[song_sel]); textc(21, s, P_CYAN); }
  textc(24, "ENTREE OU BOUTON 1 : JOUER", P_WHITE);
  textc(26, midi_seen ? "CLAVIER MIDI BRANCHE" : "CLAVIER MIDI, OU TOUCHES W X C V...", midi_seen ? P_GREEN : P_GREY);
  textc(28, mc_hw_version() < 4 ? "CE CORE N'A PAS D'ENTREE MIDI (VERSION 4)" : "", P_RED);
}
static void draw_end(void) {
  char s[48];
  clear_text();
  int total = n_perfect + n_good + n_miss, acc = total ? (n_perfect * 100 + n_good * 60) / total : 0;
  const char *grade = acc >= 95 ? "S" : acc >= 85 ? "A" : acc >= 70 ? "B" : acc >= 50 ? "C" : "D";
  textc(3, songs[song_sel].name, P_WHITE);
  snprintf(s, sizeof s, "NOTE  %s", grade); textc(6, s, P_YELLOW);
  snprintf(s, sizeof s, "SCORE      %7d", score); textc(9, s, P_WHITE);
  snprintf(s, sizeof s, "PRECISION      %3d %%", acc); textc(11, s, P_WHITE);
  snprintf(s, sizeof s, "PARFAIT        %4d", n_perfect); textc(13, s, P_YELLOW);
  snprintf(s, sizeof s, "BIEN           %4d", n_good); textc(14, s, P_GREEN);
  snprintf(s, sizeof s, "RATE           %4d", n_miss); textc(15, s, P_RED);
  if (level) { snprintf(s, sizeof s, "FAUSSES NOTES  %4d", n_wrong); textc(16, s, P_GREY); }
  snprintf(s, sizeof s, "MEILLEUR COMBO %4d", best_combo); textc(18, s, P_CYAN);
  if (score >= best_score[song_sel] && score) textc(21, "NOUVEAU RECORD !", P_YELLOW);
  textc(25, "ENTREE OU BOUTON 1 : RETOUR", P_GREY);
}

// ================================================================ input
static const int16_t piano[][2] = {
  { KEY_Z, 0 }, { KEY_S, 1 }, { KEY_X, 2 }, { KEY_D, 3 }, { KEY_C, 4 }, { KEY_V, 5 }, { KEY_G, 6 },
  { KEY_B, 7 }, { KEY_H, 8 }, { KEY_N, 9 }, { KEY_J, 10 }, { KEY_M, 11 }, { KEY_COMMA, 12 },
  { KEY_L, 13 }, { KEY_PERIOD, 14 }, { KEY_SEMICOLON, 15 }, { KEY_SLASH, 16 },
  { KEY_Q, 12 }, { KEY_2, 13 }, { KEY_W, 14 }, { KEY_3, 15 }, { KEY_E, 16 }, { KEY_R, 17 },
  { KEY_5, 18 }, { KEY_T, 19 }, { KEY_6, 20 }, { KEY_Y, 21 }, { KEY_7, 22 }, { KEY_U, 23 },
  { KEY_I, 24 }, { KEY_9, 25 }, { KEY_O, 26 }, { KEY_0, 27 }, { KEY_P, 28 } };
static int piano_key(int k) { for (unsigned i = 0; i < sizeof piano / sizeof piano[0]; i++) if (piano[i][0] == k) return piano[i][1]; return -1; }
static int piano_base(void) {                         // the lower row starts at the C of the lowest notes
  int lo = 96;
  for (int i = 0; i < nnotes; i++) if (notes[i].note < lo) lo = notes[i].note;
  return nnotes ? (lo - 1) / 12 * 12 + 1 : 37;
}

static void menu_move(int d) {
  switch (menu_item) {
    case 0: song_sel = (song_sel + nsongs + d) % nsongs; part_sel = 0; list_parts(); break;
    case 1: if (nparts) part_sel = (part_sel + nparts + d) % nparts; break;
    case 2: level = !level; break;
    case 3: tempo_sel = (tempo_sel + 3 + d) % 3; break;
  }
}
static void action(int a) {                          // 1 up, 2 down, 3 left, 4 right, 5 ok, 6 back
  if (state == S_MENU) {
    if (a == 1) menu_item = (menu_item + 3) % 4;
    else if (a == 2) menu_item = (menu_item + 1) % 4;
    else if (a == 3) menu_move(-1);
    else if (a == 4) menu_move(1);
    else if (a == 5 && nparts) { start_game(); return; }
    if (a) draw_menu();
  } else if (state == S_PLAY) {
    if (a == 6) { mt_stop(); state = S_MENU; mc_tilemap_fill(1, 0); list_parts(); draw_lanes(); draw_menu(); }
  } else if (a == 5 || a == 6) { state = S_MENU; mc_tilemap_fill(1, 0); list_parts(); draw_menu(); }
}

int main(void) {
  mc_init();
  video_init();
  mt_init(mtbank_file);
  find_songs();
  for (int i = 0; i < 512; i++) key_track[i] = -1;
  for (int i = 0; i < 128; i++) midi_down[i] = -1;
  list_parts();
  for (int t = 0; t < nparts; t++) if (parts[t] == 0) part_sel = t;
  draw_lanes();
  draw_menu();
  for (;;) {
    mc_frame();
    int e;
    // MIDI keyboard
    while ((e = mc_midi_event()) >= 0) {
      if (!midi_seen) { midi_seen = 1; if (state == S_MENU) draw_menu(); }
      int st = MIDI_STATUS(e), mn = MIDI_DATA1(e);
      if (st == MIDI_NOTE_ON) {
        if (state == S_PLAY) { press(mn - 11, (MIDI_DATA2(e) * 64 + 63) / 127, -1); midi_down[mn] = (int8_t)col_of(mn - 11); }
      } else if (st == MIDI_NOTE_OFF && midi_down[mn] != -1) { release(midi_down[mn]); midi_down[mn] = -1; }
    }
    // computer keyboard
    while ((e = mc_key_event()) >= 0) {
      int k = e & 0x1FF;
      if (e & 0x200) {
        int pk = piano_key(k);
        if (state == S_PLAY && pk >= 0) { press(pbase + pk, 56, k); continue; }
        action(k == KEY_UP ? 1 : k == KEY_DOWN ? 2 : k == KEY_LEFT ? 3 : k == KEY_RIGHT ? 4 :
               k == KEY_ENTER || k == KEY_SPACE ? 5 : k == KEY_ESC ? 6 : 0);
      } else if (key_track[k] != -1) { release(key_track[k]); key_track[k] = -1; }
    }
    // pads (menus)
    uint32_t p = mc_pad_pressed(1);
    if (p & IN_UP) action(1);
    if (p & IN_DOWN) action(2);
    if (p & IN_LEFT) action(3);
    if (p & IN_RIGHT) action(4);
    if (p & (IN_B(1) | IN_START)) action(state == S_PLAY ? 0 : 5);
    if (p & IN_B(2)) action(6);
    if (state == S_PLAY) {
      play_frame();
      if (state == S_END) draw_end();
    }
  }
}
