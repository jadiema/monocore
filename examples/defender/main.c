// PIANO DEFENDER: invaders come down on the keys of a piano; play the note of their column to fire.
// A MIDI keyboard (or the letters of the computer keyboard) is the only weapon: chords fire on several
// columns, hitting hard breaks the armoured ones, the modulation wheel drops a bomb. The keyboard on
// screen covers two octaves, C3 to B4 (MIDI 48 to 71), 16 pixels per key.
#include <stdio.h>
#include <string.h>
#include "mc.h"
#include "mtrack.h"

#define KEYS 24
#define LOW 48                     // MIDI note of the first key (C3)
#define GROUND 204                 // invaders that reach this line hit the town
#define MAXE 40

enum { T_SOLID = 96, T_KEYL, T_KEYR, T_BEAM, T_GROUND, T_STAR1, T_STAR2, T_HEART, T_EBAR, T_SPR = 112 };
enum { SP_A = T_SPR, SP_A2 = T_SPR + 4, SP_B = T_SPR + 8, SP_B2 = T_SPR + 12, SP_C = T_SPR + 16, SP_C2 = T_SPR + 20, SP_X = T_SPR + 24, SP_X2 = T_SPR + 28 };
enum { P_WHITE, P_YELLOW, P_GREEN, P_RED, P_GREY, P_CYAN, P_STARS = 8, P_BEAM, P_BEAM_MISS, P_GROUND, P_KEY_W, P_KEY_B, P_KEY_ON, P_KEY_HIT,
       P_KEY_POOL, P_EN_A, P_EN_B, P_EN_C, P_BOOM, P_FLASH, P_HUD };

#ifndef KEY_COMMA
#define KEY_COMMA 0x41
#define KEY_PERIOD 0x49
#define KEY_SLASH 0x4A
#define KEY_SEMICOLON 0x4C
#endif

static volatile uint32_t *L0, *L1, *L2;
static void put(volatile uint32_t *m, int x, int y, int t, int pal) { if (x >= 0 && x < 64 && y >= 0 && y < 64) m[y * 64 + x] = TILE(t, pal); }
static void text(int x, int y, const char *s, int pal) {
  for (; *s && x < 48; s++, x++) { int c = (unsigned char)*s; put(L0, x, y, (c < 32 || c > 127 ? '?' : c) - 32, pal); }
}
static void textc(int y, const char *s, int pal) { text((48 - (int)strlen(s)) / 2, y, s, pal); }
static void clear_row(int y) { for (int x = 0; x < 48; x++) L0[y * 64 + x] = 0; }
static void clear_text(int y0, int y1) { for (int y = y0; y <= y1; y++) clear_row(y); }

// ================================================================ graphics
static void tile_px(uint8_t *g, int x, int y, int c) { uint8_t *b = &g[y * 4 + (x >> 1)]; *b = (uint8_t)((x & 1) ? (*b & 0x0F) | (c << 4) : (*b & 0xF0) | c); }
static void make_tile(int n, const char *rows) {
  static uint8_t g[32];
  memset(g, 0, 32);
  for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) { char c = rows[y * 8 + x]; if (c >= '1' && c <= '9') tile_px(g, x, y, c - '0'); }
  mc_chr_upload(n * 32, g, 32);
}
// an 8 x 8 drawing, doubled into a 16 x 16 sprite of four cells (row major)
static void make_sprite(int n, const char *rows) {
  static uint8_t g[4][32];
  memset(g, 0, sizeof g);
  for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
    char c = rows[(y / 2) * 8 + x / 2];
    if (c >= '1' && c <= '9') tile_px(g[(y / 8) * 2 + x / 8], x & 7, y & 7, c - '0');
  }
  mc_chr_upload(n * 32, g, sizeof g);
}
static void set_pal(int p, uint16_t a, uint16_t b, uint16_t c) { PALETTE[p * 16 + 1] = a; PALETTE[p * 16 + 2] = b; PALETTE[p * 16 + 3] = c; }

static void video_init(void) {
  mc_video_mode(384, 240);
  mc_chr_upload(0, font_chr, 96 * 32);
  make_tile(T_SOLID, "33333333" "33333333" "33333333" "33333333" "33333333" "33333333" "33333333" "33333333");
  make_tile(T_KEYL,  "31111111" "31111111" "31111111" "31111111" "31111111" "31111111" "31111111" "33333333");
  make_tile(T_KEYR,  "11111113" "11111113" "11111113" "11111113" "11111113" "11111113" "11111113" "33333333");
  make_tile(T_BEAM,  "...12..." "...12..." "..1221.." "...12..." "...12..." "..1221.." "...12..." "...12...");
  make_tile(T_GROUND,"........" "........" "........" "........" "11111111" "22222222" "23222322" "22222222");
  make_tile(T_STAR1, "........" "........" "........" "...1...." "........" "........" "........" "........");
  make_tile(T_STAR2, "........" ".....2.." "........" "........" "........" "........" ".2......" "........");
  make_tile(T_HEART, "........" ".11.11.." "1111111." "1111111." ".11111.." "..111..." "...1...." "........");
  make_tile(T_EBAR,  "........" ".111111." ".111111." ".111111." ".111111." ".111111." ".111111." "........");
  make_sprite(SP_A,  "..1..1.." "...11..." "..1111.." ".121121." "11111111" "1.1111.1" "1.1..1.1" "...11...");
  make_sprite(SP_A2, "..1..1.." "1..11..1" "1.1111.1" "11211211" "11111111" "..1111.." ".1....1." "1......1");
  make_sprite(SP_B,  "..3333.." ".311113." "31211213" "31111113" "33111133" ".3.33.3." "3......3" ".3....3.");
  make_sprite(SP_B2, "..3333.." ".311113." "31211213" "31111113" "33111133" ".3.33.3." ".3....3." "3......3");
  make_sprite(SP_C,  "...11..." "..1111.." ".112211." "11122111" ".111111." "..1..1.." ".1....1." "........");
  make_sprite(SP_C2, "...11..." "..1111.." ".112211." "11122111" ".111111." "..1..1.." "..1..1.." "........");
  make_sprite(SP_X,  "........" "..1.1..." "...1...." ".1121..." "...1...." "..1.1..." "........" "........");
  make_sprite(SP_X2, "1..3..1." ".1.3.1.." "..131..." "3312133." "..131..." ".1.3.1.." "1..3..1." "........");
  for (int p = 0; p < 32; p++) for (int i = 0; i < 16; i++) PALETTE[p * 16 + i] = 0;
  static const uint16_t fg[6] = { RGB555(240, 240, 240), RGB555(250, 220, 80), RGB555(110, 230, 120), RGB555(250, 90, 80),
                                  RGB555(120, 126, 150), RGB555(90, 210, 240) };
  for (int p = 0; p < 6; p++) set_pal(p, fg[p], RGB555(10, 10, 20), 0);
  set_pal(P_STARS, RGB555(200, 210, 255), RGB555(90, 100, 150), 0);
  set_pal(P_BEAM, RGB555(120, 230, 255), RGB555(255, 255, 255), 0);
  set_pal(P_BEAM_MISS, RGB555(60, 80, 120), RGB555(90, 110, 160), 0);
  set_pal(P_GROUND, RGB555(250, 200, 80), RGB555(70, 60, 90), RGB555(110, 100, 140));
  set_pal(P_KEY_W, RGB555(230, 230, 238), 0, RGB555(100, 100, 120));
  set_pal(P_KEY_B, RGB555(34, 34, 46), 0, RGB555(20, 20, 28));
  set_pal(P_KEY_ON, RGB555(90, 170, 250), 0, RGB555(40, 80, 140));
  set_pal(P_KEY_HIT, RGB555(110, 230, 120), 0, RGB555(40, 120, 60));
  set_pal(P_KEY_POOL, RGB555(250, 220, 140), 0, RGB555(150, 120, 60));
  set_pal(P_EN_A, RGB555(110, 230, 120), RGB555(255, 255, 255), RGB555(30, 110, 50));
  set_pal(P_EN_B, RGB555(240, 90, 150), RGB555(255, 255, 255), RGB555(150, 30, 70));
  set_pal(P_EN_C, RGB555(250, 220, 80), RGB555(255, 255, 255), RGB555(220, 130, 40));
  set_pal(P_BOOM, RGB555(250, 160, 60), RGB555(255, 255, 220), RGB555(230, 60, 40));
  set_pal(P_FLASH, RGB555(255, 255, 255), RGB555(255, 255, 255), RGB555(255, 255, 255));
  set_pal(P_HUD, RGB555(250, 90, 100), RGB555(90, 210, 240), 0);
  PALETTE[31 * 16 + 1] = RGB555(6, 6, 18);
  mc_background(31 * 16 + 1);
  mc_layer(0, MC_CHR(0), MC_TILE8 | MC_4BPP, 64, 64, 3);
  mc_layer(1, MC_CHR(0), MC_TILE8 | MC_4BPP, 64, 64, 1);
  mc_layer(2, MC_CHR(0), MC_TILE8 | MC_4BPP, 64, 64, 0);
  mc_tilemap_fill(0, 0); mc_tilemap_fill(1, 0); mc_tilemap_fill(2, 0);
  L0 = mc_tilemap(0); L1 = mc_tilemap(1); L2 = mc_tilemap(2);
  for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {                 // stars, scrolled slowly
    uint32_t r = mc_rand() % 100;
    if (r < 4) put(L2, x, y, T_STAR1, P_STARS); else if (r < 9) put(L2, x, y, T_STAR2, P_STARS);
  }
  mc_sprite_sheet(MC_CHR(0));
  mc_display(DISP_ON | DISP_LAYER(0) | DISP_LAYER(1) | DISP_LAYER(2) | DISP_SPRITES);
}

// ================================================================ keyboard on screen
static bool black(int k) { int s = k % 12; return s == 1 || s == 3 || s == 6 || s == 8 || s == 10; }
static uint8_t key_state[KEYS], key_shown[KEYS], in_pool[KEYS], pool_shown[KEYS];
static void draw_keys(bool all) {
  for (int k = 0; k < KEYS; k++) {
    if (!all && key_shown[k] == key_state[k] && pool_shown[k] == in_pool[k]) continue;
    key_shown[k] = key_state[k]; pool_shown[k] = in_pool[k];
    int pal = key_state[k] == 2 ? P_KEY_HIT : key_state[k] ? P_KEY_ON : in_pool[k] && !black(k) ? P_KEY_POOL : black(k) ? P_KEY_B : P_KEY_W;
    for (int y = 26; y < 30; y++) {
      int pp = pal;
      if (black(k) && y == 29 && !key_state[k]) pp = P_KEY_W;       // the white under a black key
      if (black(k) && in_pool[k] && !key_state[k] && y == 26) pp = P_KEY_POOL;
      put(L0, 2 * k, y, T_KEYL, pp); put(L0, 2 * k + 1, y, T_KEYR, pp);
    }
  }
  if (all) {
    for (int x = 0; x < 48; x++) put(L1, x, 25, T_GROUND, P_GROUND);
    clear_row(25); text(0, 25, "C3", P_GREY); text(24, 25, "C4", P_GREY);
  }
}

// ================================================================ game state
typedef struct { int16_t col, hp, type, life; int32_t y, speed; } enemy_t;   // y in 1/256 pixel
static enemy_t en[MAXE];
static int8_t boom_t[MAXE];        // explosion frames left, at the enemy's place
static int16_t boom_x[MAXE], boom_y[MAXE];
static int beam_t[KEYS], beam_y[KEYS], beam_pal[KEYS];
enum { S_TITLE, S_INTRO, S_PLAY, S_CLEAR, S_OVER };
static int new_record;
static int state, timer, wave, score, best, lives, bombs, combo, energy, shake, flash;
static int to_spawn, spawned, last_row, rows_since, prev_note, midi_seen, mod_high;
static uint8_t pool[KEYS]; static int npool;
static int8_t key_track[512], midi_track[128];
static int next_psg;
static mt_instrument_t laser = { "LASER", MT_TYPE_PSG, PSG_SQUARE, 64, 2, 52, 128, 0, 10, 30, 6, 0, 0, 0, 0, 0, 0, -1, 0, 0, MT_IF_BOUNCE, {0, 0} };
static mt_instrument_t boom = { "BOOM", MT_TYPE_PCM, 5, 1, 0, 56, 128, 0, 0, 64, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {0, 0} };
static mt_instrument_t kill = { "KILL", MT_TYPE_PCM, 1, 1, 0, 50, 128, 0, 0, 64, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {0, 0} };
static mt_instrument_t thud = { "THUD", MT_TYPE_PCM, 6, 1, 0, 64, 128, 0, 0, 64, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {0, 0} };
static mt_instrument_t fizzle = { "FIZZ", MT_TYPE_PSG, PSG_NOISE, 128, 0, 24, 128, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, -20, 0, 0, 0, {0, 0} };

static void set_pool(void) {
  static const uint8_t white[7] = { 0, 2, 4, 5, 7, 9, 11 };
  npool = 0;
  memset(in_pool, 0, sizeof in_pool);
  if (wave == 1) { pool[0] = 12; pool[1] = 14; pool[2] = 16; npool = 3; }               // C4 D4 E4
  else if (wave == 2) { for (int i = 0; i < 5; i++) pool[npool++] = 12 + white[i]; }    // C4 to G4
  else if (wave == 3) { for (int i = 0; i < 7; i++) pool[npool++] = 12 + white[i]; }    // C4 to B4
  else if (wave == 4) { for (int o = 0; o < 2; o++) for (int i = 0; i < 7; i++) pool[npool++] = 12 * o + white[i]; }
  else for (int k = 0; k < KEYS; k++) pool[npool++] = (uint8_t)k;                       // everything
  for (int i = 0; i < npool; i++) in_pool[pool[i]] = 1;
}
static const char *const note_fr[12] = { "DO", "DO#", "RE", "RE#", "MI", "FA", "FA#", "SOL", "SOL#", "LA", "LA#", "SI" };

static void start_wave(void) {
  set_pool();
  to_spawn = 8 + 4 * wave; spawned = 0; rows_since = 0; prev_note = npool / 2;
  state = S_INTRO; timer = 150;
}
static void new_game(void) {
  memset(en, 0, sizeof en); memset(boom_t, 0, sizeof boom_t);
  wave = 1; score = 0; lives = 5; bombs = 1; combo = 0; energy = 20;
  for (int k = 0; k < KEYS; k++) beam_t[k] = 0;
  mt_play(groove_file, 0);
  start_wave();
}

static int spawn_interval(void) { return wave <= 2 ? 8 : wave <= 5 ? 6 : 4; }   // in rows of the groove
static void spawn_one(int idx) {
  for (int i = 0; i < MAXE; i++) {
    if (en[i].life) continue;
    enemy_t *e = &en[i];
    e->col = pool[idx]; e->y = -16 * 256; e->life = 1;
    uint32_t r = mc_rand() % 100;
    e->type = wave >= 3 && r < 18 ? 2 : wave >= 2 && r < 40 ? 1 : 0;
    e->hp = e->type == 1 ? 2 : 1;
    e->speed = 70 + 14 * wave + (e->type == 2 ? 50 + 6 * wave : 0);
    spawned++;
    return;
  }
}
static void spawn(void) {
  // melodic: the next column is close to the previous one in the pool
  int step = (int)(mc_rand() % 5) - 2, idx = prev_note + step;
  if (idx < 0) idx = -idx; if (idx >= npool) idx = 2 * (npool - 1) - idx; if (idx < 0) idx = 0;
  prev_note = idx;
  spawn_one(idx);
  if (wave >= 4 && to_spawn - spawned > 0 && mc_rand() % 4 == 0) {            // a chord: a third or a fifth above
    int j = idx + 2 + (int)(mc_rand() % 2) * 2;
    if (j < npool) spawn_one(j);
  }
}

static void explode(enemy_t *e, int i) {
  boom_t[i] = 16; boom_x[i] = (int16_t)(e->col * 16); boom_y[i] = (int16_t)(e->y >> 8);
  e->life = 0;
  mt_note(MT_PCM(7), 41, &kill, 50);
}

// a key went down: fire in its column
static void fire(int k, int vel, int src) {
  if (k < 0 || k >= KEYS) return;
  if (state != S_PLAY && state != S_INTRO) return;
  if (energy < 2) {                                           // overheated
    mt_note(MT_PSG(4), 30, &fizzle, 40);
    key_state[k] = 1;
    return;
  }
  energy -= 2;
  int t = next_psg; next_psg = (next_psg + 1) % 4;
  mt_note(MT_PSG(t), LOW + k - 11, &laser, vel * 64 / 127 > 20 ? vel * 64 / 127 : 20);
  if (src >= 0) key_track[src] = (int8_t)t; else midi_track[LOW + k] = (int8_t)t;
  int best_i = -1; int32_t by = -1000 * 256;
  for (int i = 0; i < MAXE; i++)
    if (en[i].life && en[i].col == k && en[i].y > by && en[i].y > -12 * 256) { by = en[i].y; best_i = i; }
  beam_t[k] = 8;
  if (best_i < 0) {
    beam_y[k] = 0; beam_pal[k] = P_BEAM_MISS; key_state[k] = 1; combo = 0;
    return;
  }
  enemy_t *e = &en[best_i];
  beam_y[k] = (int)(e->y >> 8) + 16; beam_pal[k] = P_BEAM; key_state[k] = 2;
  e->hp -= vel >= 100 ? 2 : 1;
  if (e->hp > 0) { e->life = 2; return; }                    // armour cracked: it blinks
  combo++;
  int height = GROUND - (int)(e->y >> 8);
  score += (100 + height) * (1 + (combo > 40 ? 40 : combo) / 10) * (e->type ? 2 : 1);
  energy += 3; if (energy > 20) energy = 20;
  explode(e, best_i);
}
static void release_track(int t) { if (t >= 0) mt_note_off(MT_PSG(t)); }

static void bomb(void) {
  if (state != S_PLAY || !bombs) return;
  bombs--; flash = 8;
  for (int i = 0; i < MAXE; i++) if (en[i].life) { score += 50; explode(&en[i], i); }
  mt_note(MT_PCM(7), 37, &boom, 64);
}

// ================================================================ per frame
static void draw_beams(void) {
  for (int k = 0; k < KEYS; k++) {
    if (!beam_t[k]) continue;
    beam_t[k]--;
    int top = beam_y[k] / 8;
    for (int y = 0; y < 25; y++) {
      bool on = beam_t[k] && y >= top;
      put(L1, 2 * k, y, on ? T_BEAM : 0, beam_pal[k]); put(L1, 2 * k + 1, y, on ? T_BEAM : 0, beam_pal[k]);
    }
  }
}
static void draw_sprites(void) {
  int n = 0, anim = (mc_frames >> 4) & 1;
  for (int i = 0; i < MAXE; i++) {
    enemy_t *e = &en[i];
    if (!e->life) continue;
    int base = e->type == 0 ? SP_A : e->type == 1 ? SP_B : SP_C;
    int pal = e->type == 0 ? P_EN_A : e->type == 1 ? P_EN_B : P_EN_C;
    if (e->life == 2 && (mc_frames & 4)) pal = P_FLASH;
    mc_sprite(n++, e->col * 16, (int)(e->y >> 8), base + anim * 4, 2, 2, pal, 4);
  }
  for (int i = 0; i < MAXE; i++)
    if (boom_t[i]) { boom_t[i]--; mc_sprite(n++, boom_x[i], boom_y[i], boom_t[i] > 8 ? SP_X : SP_X2, 2, 2, P_BOOM, 5); }
  while (n < 96) mc_sprite_hide(n++);
}
static void draw_hud(void) {
  char s[48];
  clear_text(0, 1);
  snprintf(s, sizeof s, "SCORE %6d", score); text(1, 0, s, P_WHITE);
  snprintf(s, sizeof s, "VAGUE %d", wave); textc(0, s, P_YELLOW);
  for (int i = 0; i < 5; i++) put(L0, 46 - i, 0, i < lives ? T_HEART : 0, P_HUD);
  for (int i = 0; i < 10; i++) put(L0, 1 + i, 1, T_EBAR, energy >= 2 * (i + 1) ? (energy > 6 ? P_CYAN : P_RED) : P_GREY);
  if (energy < 2) text(12, 1, "SURCHAUFFE", P_RED);
  snprintf(s, sizeof s, "BOMBES %d", bombs); text(39, 1, s, bombs ? P_CYAN : P_GREY);
  if (combo >= 5) { snprintf(s, sizeof s, "SERIE %d", combo); textc(1, s, P_GREEN); }
}

static void play_frame(void) {
  // spawns on the beat of the groove
  int r = mt_row();
  if (r != last_row) {
    last_row = r; rows_since++;
    if (state == S_PLAY && spawned < to_spawn && rows_since >= spawn_interval() && r % 2 == 0) { rows_since = 0; spawn(); }
  }
  if (state == S_PLAY && (mc_frames % 12) == 0 && energy < 20) energy++;
  // invaders come down
  bool any = false;
  for (int i = 0; i < MAXE; i++) {
    enemy_t *e = &en[i];
    if (!e->life) continue;
    any = true;
    e->y += e->speed;
    if (e->life == 2 && (mc_frames & 31) == 0) e->life = 1;
    if ((e->y >> 8) + 16 >= GROUND) {                         // it reached the town
      explode(e, i); lives--; combo = 0; shake = 16;
      mt_note(MT_PCM(7), 30, &thud, 64);
      if (lives <= 0) {
        state = S_OVER; timer = 60; mt_stop();
        for (int j = 0; j < MAXE; j++) if (en[j].life) explode(&en[j], j);
        new_record = score > best; if (new_record) best = score;
        return;
      }
    }
  }
  if (state == S_PLAY && spawned >= to_spawn && !any) {
    state = S_CLEAR; timer = 120; bombs = bombs < 3 ? bombs + 1 : 3;
    score += 500 * wave;
  }
}

static void draw_title(void) {
  char s[48];
  clear_text(0, 24);
  textc(3, "P I A N O   D E F E N D E R", P_YELLOW);
  textc(6, "LES ENVAHISSEURS DESCENDENT SUR LES TOUCHES :", P_WHITE);
  textc(8, "JOUEZ LA NOTE DE LEUR COLONNE POUR TIRER", P_CYAN);
  textc(10, "LES ROSES SONT BLINDES :", P_WHITE);
  textc(11, "FRAPPEZ FORT, OU DEUX FOIS", P_WHITE);
  textc(13, "MOLETTE DE MODULATION OU ESPACE : BOMBE", P_WHITE);
  textc(15, "CHAQUE TIR COUTE DE L'ENERGIE : VISEZ JUSTE", P_GREY);
  textc(18, "DO3 A SI4, OU LES TOUCHES W X C V...", P_GREY);
  if (best) { snprintf(s, sizeof s, "MEILLEUR SCORE %d", best); textc(20, s, P_GREEN); }
  textc(22, (mc_frames & 32) ? "JOUEZ UNE NOTE OU APPUYEZ SUR ENTREE" : "", P_YELLOW);
  if (mc_hw_version() < 4) textc(24, "CE CORE N'A PAS D'ENTREE MIDI (VERSION 4)", P_RED);
  else if (midi_seen) textc(24, "CLAVIER MIDI BRANCHE", P_GREEN);
}

// ================================================================ input
static const int16_t piano[][2] = {
  { KEY_Z, 0 }, { KEY_S, 1 }, { KEY_X, 2 }, { KEY_D, 3 }, { KEY_C, 4 }, { KEY_V, 5 }, { KEY_G, 6 },
  { KEY_B, 7 }, { KEY_H, 8 }, { KEY_N, 9 }, { KEY_J, 10 }, { KEY_M, 11 }, { KEY_COMMA, 12 },
  { KEY_L, 13 }, { KEY_PERIOD, 14 }, { KEY_SEMICOLON, 15 }, { KEY_SLASH, 16 },
  { KEY_Q, 12 }, { KEY_2, 13 }, { KEY_W, 14 }, { KEY_3, 15 }, { KEY_E, 16 }, { KEY_R, 17 },
  { KEY_5, 18 }, { KEY_T, 19 }, { KEY_6, 20 }, { KEY_Y, 21 }, { KEY_7, 22 }, { KEY_U, 23 } };
static int piano_key(int k) { for (unsigned i = 0; i < sizeof piano / sizeof piano[0]; i++) if (piano[i][0] == k) return piano[i][1]; return -1; }

static void start_or_continue(void) {
  if (state == S_TITLE) { clear_text(0, 24); new_game(); }
  else if (state == S_OVER && timer == 0) { state = S_TITLE; memset(in_pool, 0, sizeof in_pool); draw_title(); }
}

int main(void) {
  mc_init();
  video_init();
  mt_init(mtbank_file);
  for (int t = 0; t < 5; t++) mt_reserve(MT_PSG(t), true);
  mt_reserve(MT_PCM(7), true);
  for (int i = 0; i < 512; i++) key_track[i] = -1;
  for (int i = 0; i < 128; i++) midi_track[i] = -1;
  draw_keys(true);
  state = S_TITLE;
  int scroll = 0;
  for (;;) {
    mc_frame();
    int e;
    while ((e = mc_midi_event()) >= 0) {
      midi_seen = 1;
      int st = MIDI_STATUS(e), n = MIDI_DATA1(e), v = MIDI_DATA2(e);
      if (st == MIDI_NOTE_ON) {
        if (state == S_TITLE || state == S_OVER) start_or_continue();
        else fire(n - LOW, v, -1);
      } else if (st == MIDI_NOTE_OFF) {
        if (n >= LOW && n < LOW + KEYS) key_state[n - LOW] = 0;
        if (midi_track[n] >= 0) { release_track(midi_track[n]); midi_track[n] = -1; }
      } else if (st == MIDI_CC && n == 1) {                   // modulation wheel: bomb
        if (v >= 100 && !mod_high) { mod_high = 1; bomb(); }
        if (v < 40) mod_high = 0;
      }
    }
    while ((e = mc_key_event()) >= 0) {
      int k = e & 0x1FF, pk = piano_key(k);
      if (e & 0x200) {
        if (k == KEY_ENTER) start_or_continue();
        else if (k == KEY_SPACE) bomb();
        else if (k == KEY_ESC && state != S_TITLE) { mt_stop(); state = S_TITLE; memset(en, 0, sizeof en); memset(in_pool, 0, sizeof in_pool); draw_title(); }
        else if (pk >= 0) { if (state == S_TITLE || state == S_OVER) start_or_continue(); else fire(pk, 80, k); }
      } else if (pk >= 0) {
        key_state[pk] = 0;
        if (key_track[k] >= 0) { release_track(key_track[k]); key_track[k] = -1; }
      }
    }
    uint32_t p = mc_pad_pressed(1);
    if (p & (IN_START | IN_B(1))) { if (state == S_TITLE || state == S_OVER) start_or_continue(); else bomb(); }
    // states
    char s[48];
    switch (state) {
      case S_TITLE: if ((mc_frames & 31) == 0) draw_title(); break;
      case S_INTRO:
        if (timer == 150) {
          clear_text(2, 24);
          snprintf(s, sizeof s, "VAGUE %d", wave); textc(8, s, P_YELLOW);
          s[0] = 0;
          if (npool <= 7) for (int i = 0; i < npool; i++) { strcat(s, note_fr[pool[i] % 12]); strcat(s, " "); }
          else snprintf(s, sizeof s, npool <= 14 ? "TOUTES LES TOUCHES BLANCHES" : "TOUTES LES TOUCHES");
          textc(11, s, P_WHITE);
          textc(13, "LES TOUCHES EN JAUNE", P_GREY);
        }
        play_frame();
        if (--timer == 0) { clear_text(2, 24); state = S_PLAY; }
        break;
      case S_PLAY: play_frame(); break;
      case S_CLEAR:
        if (timer == 120) { textc(10, "VAGUE TERMINEE !", P_GREEN); snprintf(s, sizeof s, "BONUS %d   +1 BOMBE", 500 * wave); textc(12, s, P_YELLOW); }
        if (--timer == 0) { clear_text(2, 24); wave++; start_wave(); }
        break;
      case S_OVER:
        if (timer == 60) {
          clear_text(2, 24);
          textc(9, "G A M E   O V E R", P_RED);
          snprintf(s, sizeof s, "SCORE %d   VAGUE %d", score, wave); textc(12, s, P_WHITE);
          if (new_record) textc(14, "NOUVEAU RECORD !", P_YELLOW);
          textc(18, "JOUEZ UNE NOTE OU ENTREE", P_GREY);
        }
        if (timer > 0) timer--;
        break;
    }
    if (state != S_TITLE) draw_hud(); else clear_text(0, 1);
    draw_beams();
    draw_sprites();
    draw_keys(false);
    // stars, shaking, bomb flash
    scroll = (scroll + 1) & 2047;
    int sx = shake ? (int)(mc_rand() % 7) - 3 : 0;
    if (shake) shake--;
    mc_layer_scroll(2, 0, (512 - scroll / 4) & 511);
    mc_layer_scroll(0, sx, 0); mc_layer_scroll(1, sx, 0);
    PALETTE[31 * 16 + 1] = flash ? RGB555(120, 120, 160) : RGB555(6, 6, 18);
    if (flash) flash--;
  }
}
