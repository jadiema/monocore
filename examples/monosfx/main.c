// MONOSFX: the sound effect editor of MONOCORE-1 (with MONOTRACKER and MONOPIXEL), mouse and keyboard.
//
// A bank of the msfx module (sdk/msfx.h): 32 effects (jump, coin, explosion...) of one PSG channel,
// computed 60 times a second, in the manner of sfxr. Each change is heard at once; generators make
// a sound of a kind (SAUT, PIECE...) at random; the graph shows the pitch and the volume over time.
// Banks are kept in the NVRAM (8 slots) and exported for the games with tools/msfx/msfx.py.
// The interface is the ui480 module (640 x 480 interlaced).
#include <stdio.h>
#include <string.h>
#include "mc.h"
#include "msfx.h"
#include "ui480.h"

#define GRAPH_OFF 0x10000u            // the graph: 32 x 12 cells (256 x 96 pixels), shown by sprite 1
#define UI_OFF    0x1A000u
#define GRAPH_X 208
#define GRAPH_Y 48
#define P_GRAPH 32

static char bank_name[17];
static ms_effect_t fx[MS_EFFECTS];               // empty when name[0] == 0
static int sel, scroll, dirty, bank_slot = -1, slot_sel, naming, page, help_back, auto_play = 1, graph_dirty = 1;
static ms_effect_t clip;
static int clip_ok;
static uint32_t frame_count;
static char line[UI_COLS + 1];

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int slen(const char *s, int max) { int n = 0; while (n < max && s[n]) n++; return n; }
static int rnd(int lo, int hi) { return lo + (int)(mc_rand() % (uint32_t)(hi - lo + 1)); }

// ================================================================ NVRAM: 8 banks
// "MSNV", u8 layout, u8 last slot, 10 bytes, then 8 x {u32 offset, u32 size}, banks from 80
#define NV MC_NVRAM
#define NV_SLOTS 8
#define NV_DATA 80
static uint32_t nv32(uint32_t a) { return NV[a] | (NV[a + 1] << 8) | (NV[a + 2] << 16) | ((uint32_t)NV[a + 3] << 24); }
static void nv32w(uint32_t a, uint32_t v) { for (int i = 0; i < 4; i++) NV[a + i] = (uint8_t)(v >> (8 * i)); }
static uint32_t slot_off(int k) { return nv32(16 + 8 * k); }
static uint32_t slot_size(int k) { return nv32(20 + 8 * k); }
static uint32_t align4(uint32_t v) { return (v + 3) & ~3u; }
static const void *slot_data(int k) { return (const void *)(NVRAM_BASE + slot_off(k)); }
static void nv_check(void) {
  if (NV[0] == 'M' && NV[1] == 'S' && NV[2] == 'N' && NV[3] == 'V') {
    for (int k = 0; k < NV_SLOTS; k++)
      if (slot_size(k) && (slot_off(k) + slot_size(k) > MC_NVRAM_SIZE || ms_check(slot_data(k), slot_size(k)))) { nv32w(16 + 8 * k, 0); nv32w(20 + 8 * k, 0); }
    ui_layout = NV[4] & 1;
    return;
  }
  for (uint32_t i = 0; i < NV_DATA; i++) NV[i] = 0;
  NV[0] = 'M'; NV[1] = 'S'; NV[2] = 'N'; NV[3] = 'V'; NV[5] = 0xFF;
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
static uint32_t nv_free(int replacing) { return MC_NVRAM_SIZE - nv_end() + (replacing >= 0 ? align4(slot_size(replacing)) : 0); }

// ================================================================ the bank
static int used(void) { int n = 0; for (int i = 0; i < MS_EFFECTS; i++) if (fx[i].name[0]) n = i + 1; return n; }
static uint32_t bank_encode(uint8_t *dst) {
  int n = used();
  ms_header_t h;
  memset(&h, 0, sizeof h);
  h.magic = MS_MAGIC; h.count = (uint8_t)n; h.version = 1; h.size = sizeof h + n * sizeof(ms_effect_t);
  memcpy(h.name, bank_name, 16);
  if (dst) { memcpy(dst, &h, sizeof h); memcpy(dst + sizeof h, fx, n * sizeof(ms_effect_t)); }
  return h.size;
}
static void bank_clear(void) { memset(fx, 0, sizeof fx); memset(bank_name, 0, sizeof bank_name); sel = scroll = 0; graph_dirty = 1; }
static bool bank_load(const void *data, uint32_t max) {
  ms_bank_t b;
  if (ms_open(&b, data, max)) { ui_message("BANQUE ILLISIBLE"); return false; }
  bank_clear();
  memcpy(fx, b.fx, ms_count(&b) * sizeof(ms_effect_t));
  memcpy(bank_name, b.h->name, 16);
  dirty = 0;
  return true;
}
static void bank_save(int k) {
  uint32_t n = bank_encode(0);
  if (n > nv_free(k)) { ui_message("NVRAM PLEINE"); return; }
  nv_delete(k);
  uint32_t off = nv_end();
  bank_encode((uint8_t *)(NVRAM_BASE + off));
  nv32w(16 + 8 * k, off); nv32w(20 + 8 * k, n);
  NV[5] = (uint8_t)k; bank_slot = k; dirty = 0;
  snprintf(line, sizeof line, "SAUVE EN %d (%u O) - L'OSD L'ECRIT SUR LA SD", k + 1, (unsigned)n);
  ui_message(line);
}
static void quick_save(void) {
  int k = bank_slot;
  if (k < 0) for (int i = 0; i < NV_SLOTS; i++) if (!slot_size(i)) { k = i; break; }
  if (k < 0) ui_message("NVRAM : AUCUN EMPLACEMENT LIBRE (PAGE BANQUE)"); else bank_save(k);
}

// ================================================================ sound
static void play(void) { if (fx[sel].name[0]) { ms_play_on(&fx[sel], 0); } }
static void changed(void) { dirty = 1; graph_dirty = 1; if (auto_play) play(); }
static void default_effect(ms_effect_t *e, int n) {
  memset(e, 0, sizeof *e);
  snprintf(e->name, 8, "BRUIT%02d", n);
  e->wave = PSG_SQUARE; e->freq = 440; e->volume = 160; e->sustain = 6; e->decay = 12; e->duty = 128;
}
// generators: a sound of a kind, with some chance in it
enum { G_JUMP, G_COIN, G_SHOT, G_BOOM, G_BONUS, G_HURT, G_RANDOM, G_MUTATE, G_COUNT };
static const char *const gen_name[G_COUNT] = { "SAUT", "PIECE", "TIR", "BOUM", "BONUS", "AIE", "HASARD", "MUTER" };
static void generate(int g) {
  ms_effect_t *e = &fx[sel];
  char name[8];
  memcpy(name, e->name, 8);
  mc_srand(mc_rand() ^ frame_count ^ (uint32_t)(mc_mouse_x() << 16));   // a different sound each time
  if (g == G_MUTATE) {                           // small changes of what is there
    if (!e->name[0]) return;
    e->freq = (uint16_t)clampi(e->freq * rnd(85, 115) / 100, 30, 9000);
    e->slide = (int16_t)(e->slide + rnd(-40, 40));
    e->decay = (uint8_t)clampi(e->decay + rnd(-3, 3), 0, 255);
    e->duty = (uint8_t)clampi(e->duty + rnd(-24, 24), 8, 248);
    if (e->vib_speed) e->vib_depth = (uint8_t)clampi(e->vib_depth + rnd(-4, 4), 0, 255);
    changed();
    return;
  }
  memset(e, 0, sizeof *e);
  e->volume = (uint8_t)rnd(100, 160); e->duty = (uint8_t)rnd(48, 128);
  switch (g) {
    case G_JUMP:  e->wave = PSG_SQUARE; e->freq = (uint16_t)rnd(180, 360); e->slide = (int16_t)rnd(300, 640); e->accel = (int16_t)rnd(-12, 0);
                  e->sustain = (uint8_t)rnd(3, 6); e->decay = (uint8_t)rnd(8, 14); break;
    case G_COIN:  e->wave = PSG_SQUARE; e->freq = (uint16_t)rnd(800, 1300); e->sustain = (uint8_t)rnd(2, 5); e->decay = (uint8_t)rnd(10, 18);
                  e->arp = (int8_t)(rnd(0, 1) ? 5 : 7); e->arp_time = (uint8_t)rnd(3, 5); break;
    case G_SHOT:  e->wave = (uint8_t)(rnd(0, 1) ? PSG_SAW : PSG_SQUARE); e->freq = (uint16_t)rnd(1200, 2400); e->slide = (int16_t)-rnd(900, 2000);
                  e->sustain = (uint8_t)rnd(1, 3); e->decay = (uint8_t)rnd(6, 12); e->duty_sweep = (int8_t)rnd(-6, 6); break;
    case G_BOOM:  e->wave = PSG_NOISE; e->freq = (uint16_t)rnd(800, 2400); e->slide = (int16_t)-rnd(60, 240); e->volume = (uint8_t)rnd(170, 230);
                  e->sustain = (uint8_t)rnd(3, 8); e->decay = (uint8_t)rnd(25, 50); break;
    case G_BONUS: e->wave = PSG_SQUARE; e->freq = (uint16_t)rnd(400, 700); e->slide = (int16_t)rnd(300, 900); e->repeat = (uint8_t)rnd(4, 8);
                  e->sustain = (uint8_t)rnd(14, 24); e->decay = (uint8_t)rnd(8, 16); e->duty_sweep = (int8_t)rnd(-3, 0); break;
    case G_HURT:  e->wave = (uint8_t)(rnd(0, 1) ? PSG_SQUARE : PSG_SAW); e->freq = (uint16_t)rnd(300, 600); e->slide = (int16_t)-rnd(120, 320);
                  e->sustain = (uint8_t)rnd(6, 12); e->decay = (uint8_t)rnd(12, 24); e->duty = (uint8_t)rnd(16, 48);
                  e->vib_depth = (uint8_t)rnd(15, 40); e->vib_speed = (uint8_t)rnd(3, 6); break;
    default:      e->wave = (uint8_t)rnd(0, 3); e->freq = (uint16_t)rnd(80, 3000); e->slide = (int16_t)rnd(-1200, 1200); e->accel = (int16_t)rnd(-20, 20);
                  e->attack = (uint8_t)rnd(0, 6); e->sustain = (uint8_t)rnd(1, 20); e->decay = (uint8_t)rnd(4, 40);
                  if (rnd(0, 2) == 0) { e->vib_depth = (uint8_t)rnd(4, 60); e->vib_speed = (uint8_t)rnd(2, 12); }
                  if (rnd(0, 2) == 0) { e->arp = (int8_t)rnd(-12, 12); e->arp_time = (uint8_t)rnd(1, 15); }
                  if (rnd(0, 3) == 0) e->repeat = (uint8_t)rnd(3, 12);
                  e->duty_sweep = (int8_t)rnd(-4, 4); break;
  }
  if (name[0]) memcpy(e->name, name, 8);
  else snprintf(e->name, 8, "%s%02d", g == G_RANDOM ? "BRUIT" : gen_name[g], sel);
  changed();
}

// ================================================================ the graph (software, into the CHR RAM)
static void gpx(int x, int y, int c) {
  if (x < 0 || y < 0 || x >= 256 || y >= 96) return;
  volatile uint8_t *p = MC_CHRRAM + GRAPH_OFF + ((y >> 3) * 32 + (x >> 3)) * 32 + (y & 7) * 4 + ((x & 7) >> 1);
  *p = (uint8_t)((x & 1) ? (*p & 0x0F) | (c << 4) : (*p & 0xF0) | c);
}
static int log_y(uint32_t f16) {             // 40 Hz at the bottom, 10 kHz at the top: 8 octaves, 12 pixels each
  uint32_t f = f16 / 16, o = 0;
  if (f < 40) f = 40;
  while ((f >> o) >= 80) o++;               // f in [40 << o, 80 << o)
  int frac = (int)(((f - (40u << o)) * 12) / (40u << o));
  int y = 95 - (int)o * 12 - frac;
  return y < 0 ? 0 : y;
}
static void draw_graph(void) {
  mc_fill(MC_CHRRAM + GRAPH_OFF, 0, 32 * 12 * 32);
  for (int o = 0; o < 8; o++) for (int x = 0; x < 256; x += 4) gpx(x, 95 - o * 12, 3);   // octaves
  const ms_effect_t *e = &fx[sel];
  if (!e->name[0]) return;
  int len = ms_length(e), px = 256 / len;                // pixels per frame: the whole sound
  if (px < 1) px = 1;
  if (px > 12) px = 12;
  ms_voice_t v;
  uint32_t f; int vl, d, t = 0, last = -1;
  ms_voice_start(&v, e);
  while (ms_voice_step(&v, &f, &vl, &d) && t * px < 256) {
    int h = vl * 95 / 255;                   // the volume: bars from the bottom
    for (int x = t * px; x < t * px + px; x++) for (int y = 95 - h; y < 96; y += 2) gpx(x, y, 2);
    int y = log_y(f);                        // the pitch: a line
    if (last >= 0) for (int yy = last < y ? last : y; yy <= (last < y ? y : last); yy++) gpx(t * px, yy, 1);
    for (int x = t * px; x < t * px + px; x++) { gpx(x, y, 1); gpx(x, y + 1, 1); }
    last = y; t++;
  }
}

// ================================================================ pages
enum { PG_FX, PG_BANK, PG_HELP };
enum { Z_PAGE = 1, Z_SPIN, Z_BTN, Z_FX, Z_SLOT, Z_NAME, Z_HELP, Z_WAVE, Z_GEN };
enum { SP_FREQ, SP_SLIDE, SP_ACCEL, SP_ATT, SP_SUS, SP_DEC, SP_VOL, SP_DUTY, SP_SWEEP, SP_VIBD, SP_VIBS, SP_REP, SP_ARP, SP_ARPT };
enum { A_PLAY, A_COPY, A_PASTE, A_DEL, A_NEW, A_AUTO, A_SAVE, A_BSAVE, A_BLOAD, A_BDEL, A_BNEW, A_BDEMO, A_LAYOUT };
static const char *const wave_name[4] = { "CARRE", "TRIANGLE", "DENTS", "BRUIT" };

static void draw_status(const char *help) {
  ui_box(0, 29, 79, 29, UI_SUNK);
  const char *m = ui_message_text();
  ui_printn(1, 29, m ? m : help, 64, m ? UI_YELLOW : UI_GREY);
  ui_printn(66, 29, bank_name, 13, UI_CYAN);
}
static void draw_top(void) {
  static const char *const names[3] = { "EFFETS", "BANQUE", "AIDE" };
  ui_box(0, 1, 11, 1, UI_RAISED);
  ui_print(1, 1, "MONOSFX", UI_INK_BLUE);
  for (int p = 0; p < 3; p++) { snprintf(line, sizeof line, "F%d %s", p + 1, names[p]); ui_button(12 + p * 12, 1, 12, line, page == p, Z_PAGE, p); }
  ui_button(48, 1, 18, auto_play ? "ECOUTE AUTO : OUI" : "ECOUTE AUTO : NON", auto_play, Z_BTN, A_AUTO);
  ui_button(72, 1, 8, "SAUVER", false, Z_BTN, A_SAVE);
  if (dirty) ui_printc(72, 79, 1, "SAUVER", UI_INK_RED);
}
static void spin_val(int x, int y, const char *label, const char *val, int k) { ui_spin(x, y, label, 9, val, 5, Z_SPIN, k); }
static void draw_fx(void) {
  ui_box(2, 3, 23, 27, UI_SUNK);
  const int rows = 24;
  if (sel < scroll) scroll = sel;
  if (sel >= scroll + rows) scroll = sel - rows + 1;
  for (int i = 0; i < rows; i++) {
    int n = scroll + i, y = 3 + i;
    if (n >= MS_EFFECTS) break;
    if (n == sel) ui_put(2, y, UI_G_ARROW, UI_YELLOW);
    if (fx[n].name[0]) snprintf(line, sizeof line, "%02d %-8.8s", n, fx[n].name); else snprintf(line, sizeof line, "%02d -", n);
    ui_print(3, y, line, n == sel ? UI_YELLOW : fx[n].name[0] ? UI_WHITE : UI_GREY);
    if (ms_playing(0) && n == sel) ui_put(16, y, UI_G_RIGHT, UI_GREEN);
    ui_zone(3, y, 22, y, Z_FX, n);
  }
  // graph
  ui_box(26, 3, 57, 8, UI_SUNK);
  if (graph_dirty) { draw_graph(); graph_dirty = 0; }
  mc_sprite(1, GRAPH_X, GRAPH_Y, (int)(GRAPH_OFF / 32), 32, 12, P_GRAPH, 4); mc_sprite_zoom(1, 0x100, 0x100);
  ms_effect_t *e = &fx[sel];
  for (int w = 0; w < 4; w++) ui_button(60 + (w & 1) * 10, 3 + (w >> 1) * 2, 10, wave_name[w], e->name[0] && e->wave == w, Z_WAVE, w);
  if (e->name[0]) {
    snprintf(line, sizeof line, "%d IMAGES", ms_length(e));
    ui_print(61, 7, line, UI_GREY);
    snprintf(line, sizeof line, "%d.%02d S", ms_length(e) / 60, ms_length(e) % 60 * 100 / 60);
    ui_print(71, 7, line, UI_GREY);
  }
  // parameters
  ui_box(26, 10, 79, 22, UI_RAISED);
  if (!e->name[0]) {
    ui_print(27, 11, "EMPLACEMENT VIDE", UI_INK_DIM);
    ui_button(26, 13, 16, "NOUVEAU", false, Z_BTN, A_NEW);
    ui_print(27, 15, "OU UN GENERATEUR CI-DESSOUS", UI_INK_DIM);
  } else {
    ui_print(27, 10, "NOM", UI_INK);
    ui_printn(32, 10, e->name, 8, naming == 1 ? UI_INK_RED : UI_INK_BLUE);
    if (naming == 1 && (frame_count & 16)) ui_put(32 + slen(e->name, 8), 10, '_' - 32, UI_INK_RED);
    ui_zone(27, 10, 41, 10, Z_NAME, 1);
    snprintf(line, sizeof line, "%5d", e->freq);                                   spin_val(27, 12, "FREQ HZ", line, SP_FREQ);
    snprintf(line, sizeof line, "%+5d", e->slide);                                 spin_val(45, 12, "GLISSE", line, SP_SLIDE);
    snprintf(line, sizeof line, "%+5d", e->accel);                                 spin_val(63, 12, "ACCEL", line, SP_ACCEL);
    snprintf(line, sizeof line, "%5d", e->attack);                                 spin_val(27, 14, "ATTAQUE", line, SP_ATT);
    snprintf(line, sizeof line, "%5d", e->sustain);                                spin_val(45, 14, "TENUE", line, SP_SUS);
    snprintf(line, sizeof line, "%5d", e->decay);                                  spin_val(63, 14, "CHUTE", line, SP_DEC);
    snprintf(line, sizeof line, "%5d", e->volume);                                 spin_val(27, 16, "VOLUME", line, SP_VOL);
    snprintf(line, sizeof line, "%5d", e->duty);                                   spin_val(45, 16, "RAPPORT", line, SP_DUTY);
    snprintf(line, sizeof line, "%+5d", e->duty_sweep);                            spin_val(63, 16, "BALAYAGE", line, SP_SWEEP);
    snprintf(line, sizeof line, "%5d", e->vib_depth);                              spin_val(27, 18, "VIBR. HZ", line, SP_VIBD);
    snprintf(line, sizeof line, "%5d", e->vib_speed);                              spin_val(45, 18, "VIBR. IMG", line, SP_VIBS);
    snprintf(line, sizeof line, "%5d", e->repeat);                                 spin_val(63, 18, "REPETE", line, SP_REP);
    snprintf(line, sizeof line, "%+5d", e->arp);                                   spin_val(27, 20, "ARPEGE", line, SP_ARP);
    snprintf(line, sizeof line, "%5d", e->arp_time);                               spin_val(45, 20, "A L'IMAGE", line, SP_ARPT);
    ui_print(63, 20, "GLISSE: 1/16 HZ", UI_INK_DIM);
  }
  ui_button(26, 24, 12, "JOUER", ms_playing(0), Z_BTN, A_PLAY);
  ui_button(38, 24, 10, "COPIER", false, Z_BTN, A_COPY);
  ui_button(48, 24, 10, "COLLER", false, Z_BTN, A_PASTE);
  ui_button(58, 24, 10, "EFFACER", false, Z_BTN, A_DEL);
  for (int g = 0; g < G_COUNT; g++) ui_button(26 + g * 6 + (g >= 6 ? 2 : 0), 26, 6, gen_name[g], false, Z_GEN, g);
  if (e->name[0]) {
    snprintf(line, sizeof line, "JEU : MS_PLAY(&BANQUE, MS_FIND(&BANQUE, \"%.8s\"))", e->name);
    draw_status(naming == 1 ? "ECRIVEZ LE NOM, ENTREE POUR FINIR" : line);
  } else draw_status("ESPACE : JOUER  -  FLECHES : CHOISIR  -  ENTREE : NOMMER  -  F3 : AIDE");
}
static void draw_bank(void) {
  ui_box(2, 3, 41, 21, UI_SUNK);
  ui_print(3, 3, "EMPLACEMENTS DE LA NVRAM", UI_GREY);
  for (int k = 0; k < NV_SLOTS; k++) {
    int y = 5 + 2 * k;
    if (slot_sel == k) ui_put(2, y, UI_G_ARROW, UI_YELLOW);
    if (slot_size(k)) {
      ms_bank_t b;
      char nm[17] = "?";
      if (!ms_open(&b, slot_data(k), slot_size(k))) { memcpy(nm, b.h->name, 16); nm[16] = 0; }
      snprintf(line, sizeof line, "%d %-16.16s %2d BRUITS", k + 1, nm, ms_open(&b, slot_data(k), slot_size(k)) ? 0 : ms_count(&b));
    } else snprintf(line, sizeof line, "%d -", k + 1);
    ui_print(3, y, line, slot_sel == k ? UI_YELLOW : k == bank_slot ? UI_CYAN : slot_size(k) ? UI_WHITE : UI_GREY);
    ui_zone(3, y, 40, y, Z_SLOT, k);
  }
  ui_box(44, 3, 79, 21, UI_RAISED);
  ui_print(45, 3, "BANQUE EN COURS", UI_INK_BLUE);
  ui_print(45, 5, "NOM", UI_INK);
  ui_printn(50, 5, bank_name, 16, naming == 2 ? UI_INK_RED : UI_INK_BLUE);
  if (naming == 2 && (frame_count & 16)) ui_put(50 + slen(bank_name, 16), 5, '_' - 32, UI_INK_RED);
  ui_zone(45, 5, 66, 5, Z_NAME, 2);
  int n = 0;
  for (int i = 0; i < MS_EFFECTS; i++) if (fx[i].name[0]) n++;
  snprintf(line, sizeof line, "%d BRUITAGES, %u OCTETS", n, (unsigned)bank_encode(0));
  ui_print(45, 7, line, UI_INK);
  ui_button(44, 9, 12, "SAUVER", false, Z_BTN, A_BSAVE);
  ui_button(56, 9, 12, "CHARGER", false, Z_BTN, A_BLOAD);
  ui_button(68, 9, 10, "EFFACER", false, Z_BTN, A_BDEL);
  ui_button(44, 11, 12, "NOUVELLE", false, Z_BTN, A_BNEW);
  ui_button(56, 11, 12, "DEMO", false, Z_BTN, A_BDEMO);
  ui_print(45, 13, "CLAVIER", UI_INK);
  ui_button(54, 13, 12, ui_layout ? "QWERTY" : "AZERTY", false, Z_BTN, A_LAYOUT);
  ui_print(45, 15, "POUR UN JEU : OUVRIR L'OSD (SD),", UI_INK_DIM);
  ui_print(45, 16, "PUIS TOOLS/MSFX/MSFX.PY", UI_INK_DIM);
  ui_print(45, 17, "  MONOSFX.SAV N -O BRUITS.MFX", UI_INK_DIM);
  ui_print(45, 18, "ET : MODULES = MSFX", UI_INK_DIM);
  draw_status(naming == 2 ? "ECRIVEZ LE NOM, ENTREE POUR FINIR" : "CTRL+S : SAUVE  -  CTRL+L : CHARGE  -  CTRL+D : EFFACE L'EMPLACEMENT");
}
static void draw_help(void) {
  static const char *const help[] = {
    "MONOSFX - LES BRUITAGES DE LA MONOCORE-1 (SOURIS ET CLAVIER)",
    "",
    "F1 EFFETS  32 bruitages d'un canal PSG, calcules 60 fois par seconde",
    "F2 BANQUE  les 8 emplacements de la NVRAM : sauver, charger, nouvelle, demo",
    "",
    "ESPACE     joue le bruitage ; ECOUTE AUTO : il rejoue a chaque changement",
    "FLECHES    choisir le bruitage ; ENTREE : le nommer (8 lettres)",
    "COMPTEURS  clic : +, clic droit : -, MAJ : par 10, molette",
    "GENERATEURS  SAUT PIECE TIR BOUM BONUS AIE : un son de ce genre, au hasard",
    "           HASARD : n'importe quoi ; MUTER : de petits changements",
    "",
    "FREQ, GLISSE, ACCEL  la note de depart, sa pente, la courbure de la pente",
    "ATTAQUE, TENUE, CHUTE  le volume monte, reste, descend (en images)",
    "RAPPORT, BALAYAGE    la forme du carre, et son changement a chaque image",
    "VIBRATO    l'ecart en Hz et la periode en images",
    "ARPEGE     un saut de demi-tons a une image donnee (PIECE, BONUS)",
    "REPETE     la note repart toutes les n images",
    "",
    "CTRL+C, CTRL+V  copier, coller un bruitage ; SUPPR : l'effacer",
    "CTRL+S     sauve dans la NVRAM ; ouvrir l'OSD l'ecrit sur la carte SD",
    "",
    "POUR UN JEU : tools/msfx/msfx.py monosfx.sav 1 -o bruits.mfx",
    "  puis : modules = msfx, data = bruits.mfx (sdk/MSFX.md)",
    "",
    "                    (F3, ECHAP OU UN CLIC POUR REVENIR)" };
  ui_box(0, 1, 79, 27, UI_SUNK);
  for (unsigned i = 0; i < sizeof help / sizeof help[0]; i++) {
    char b[UI_COLS + 1];
    int n = 0;
    for (const char *s = help[i]; *s && n < UI_COLS; s++) b[n++] = (char)(*s >= 'a' && *s <= 'z' ? *s - 32 : *s);
    b[n] = 0;
    ui_print(2, 2 + i, b, i == 0 ? UI_YELLOW : (b[0] != ' ' && b[0]) ? UI_CYAN : UI_WHITE);
  }
  ui_zone(0, 1, 79, 27, Z_HELP, 0);
  draw_status("MONOSFX - SOURIS USB ET CLAVIER");
}

// ================================================================ actions
static void goto_page(int p) { if (p == PG_HELP && page != PG_HELP) help_back = page; page = p; naming = 0; }
static void spin(int k, int d) {
  ms_effect_t *e = &fx[sel];
  if (!e->name[0]) return;
  switch (k) {
    case SP_FREQ:  e->freq = (uint16_t)clampi(e->freq + d * 10, 20, 12000); break;
    case SP_SLIDE: e->slide = (int16_t)clampi(e->slide + d * 16, -4000, 4000); break;
    case SP_ACCEL: e->accel = (int16_t)clampi(e->accel + d, -200, 200); break;
    case SP_ATT:   e->attack = (uint8_t)clampi(e->attack + d, 0, 255); break;
    case SP_SUS:   e->sustain = (uint8_t)clampi(e->sustain + d, 0, 255); break;
    case SP_DEC:   e->decay = (uint8_t)clampi(e->decay + d, 0, 255); break;
    case SP_VOL:   e->volume = (uint8_t)clampi(e->volume + d * 8, 0, 255); break;
    case SP_DUTY:  e->duty = (uint8_t)clampi(e->duty + d * 8, 8, 248); break;
    case SP_SWEEP: e->duty_sweep = (int8_t)clampi(e->duty_sweep + d, -64, 64); break;
    case SP_VIBD:  e->vib_depth = (uint8_t)clampi(e->vib_depth + d, 0, 255); break;
    case SP_VIBS:  e->vib_speed = (uint8_t)clampi(e->vib_speed + d, 0, 60); break;
    case SP_REP:   e->repeat = (uint8_t)clampi(e->repeat + d, 0, 255); break;
    case SP_ARP:   e->arp = (int8_t)clampi(e->arp + d, -24, 24); break;
    case SP_ARPT:  e->arp_time = (uint8_t)clampi(e->arp_time + d, 0, 255); break;
  }
  changed();
}
static void action(int a) {
  switch (a) {
    case A_PLAY: play(); break;
    case A_COPY: if (fx[sel].name[0]) { clip = fx[sel]; clip_ok = 1; ui_message("BRUITAGE COPIE (CTRL+V POUR LE COLLER)"); } break;
    case A_PASTE: if (clip_ok) { fx[sel] = clip; changed(); } break;
    case A_DEL: if (fx[sel].name[0] && ui_confirm('E', "EFFACER LE BRUITAGE")) { memset(&fx[sel], 0, sizeof fx[0]); dirty = graph_dirty = 1; } break;
    case A_NEW: default_effect(&fx[sel], sel); changed(); break;
    case A_AUTO: auto_play = !auto_play; break;
    case A_SAVE: quick_save(); break;
    case A_BSAVE: if (!slot_size(slot_sel) || slot_sel == bank_slot || ui_confirm('S', "REMPLACER CET EMPLACEMENT")) bank_save(slot_sel); break;
    case A_BLOAD:
      if (!slot_size(slot_sel)) { ui_message("EMPLACEMENT VIDE"); break; }
      if (dirty && !ui_confirm('L', "MODIFICATIONS PERDUES")) break;
      if (bank_load(slot_data(slot_sel), slot_size(slot_sel))) { bank_slot = slot_sel; NV[5] = (uint8_t)slot_sel; ui_message("BANQUE CHARGEE"); }
      break;
    case A_BDEL:
      if (slot_size(slot_sel) && ui_confirm('D', "EFFACER L'EMPLACEMENT")) { nv_delete(slot_sel); if (bank_slot == slot_sel) bank_slot = -1; ui_message("EMPLACEMENT EFFACE"); }
      break;
    case A_BNEW: if (!dirty || ui_confirm('N', "MODIFICATIONS PERDUES")) { bank_clear(); strcpy(bank_name, "SANS NOM"); bank_slot = -1; dirty = 0; } break;
    case A_BDEMO: if (!dirty || ui_confirm('R', "MODIFICATIONS PERDUES")) { bank_load(demo_file, demo_size); bank_slot = -1; ui_message("DEMO CHARGEE"); } break;
    case A_LAYOUT: ui_layout = !ui_layout; NV[4] = (uint8_t)ui_layout; break;
  }
}
static void select_fx(int n) { sel = clampi(n, 0, MS_EFFECTS - 1); naming = 0; graph_dirty = 1; }
static void name_key(int k, char *name, int max) {
  int n = slen(name, max);
  if (k == KEY_ENTER || k == KEY_KPENTER || k == KEY_ESC) { naming = 0; return; }
  if (k == KEY_BACKSPACE) { if (n) name[n - 1] = 0; dirty = 1; return; }
  int c = ui_char(k);
  if (c && n < max) { name[n] = (char)c; if (n + 1 < max) name[n + 1] = 0; dirty = 1; }
}
static void key(int k) {
  if (naming == 1) {                              // a nameless effect would be an empty slot
    name_key(k, fx[sel].name, 8);
    if (!naming && !fx[sel].name[0]) snprintf(fx[sel].name, 8, "BRUIT%02d", sel);
    return;
  }
  if (naming == 2) { name_key(k, bank_name, 16); return; }
  switch (k) {
    case KEY_F1: goto_page(PG_FX); return;
    case KEY_F2: goto_page(PG_BANK); return;
    case KEY_F3: goto_page(page == PG_HELP ? help_back : PG_HELP); return;
    case KEY_ESC: if (page == PG_HELP) goto_page(help_back); return;
  }
  int c = ui_char(k);
  if (ui_ctrl() && c == 'S') { if (page == PG_BANK) action(A_BSAVE); else quick_save(); return; }
  if (page == PG_FX) {
    if (ui_ctrl() && c == 'C') { action(A_COPY); return; }
    if (ui_ctrl() && c == 'V') { action(A_PASTE); return; }
    switch (k) {
      case KEY_UP: select_fx(sel - 1); return;
      case KEY_DOWN: select_fx(sel + 1); return;
      case KEY_PGUP: select_fx(sel - 8); return;
      case KEY_PGDN: select_fx(sel + 8); return;
      case KEY_SPACE: play(); return;
      case KEY_ENTER: if (fx[sel].name[0]) naming = 1; else action(A_NEW); return;
      case KEY_DELETE: action(A_DEL); return;
    }
  } else if (page == PG_BANK) {
    if (k == KEY_UP) slot_sel = (slot_sel + NV_SLOTS - 1) % NV_SLOTS;
    else if (k == KEY_DOWN) slot_sel = (slot_sel + 1) % NV_SLOTS;
    else if (k == KEY_ENTER) naming = 2;
    else if (ui_ctrl() && c == 'L') action(A_BLOAD);
    else if (ui_ctrl() && c == 'D') action(A_BDEL);
  }
}
static int hold_btn, hold_timer, hold_x, hold_y;
static void click(const ui_zone_t *z, int btn) {
  int d = (btn == 2 ? -1 : 1) * (ui_shift() ? 10 : 1);
  switch (z->id) {
    case Z_PAGE: if (btn == 1) goto_page(z->arg); break;
    case Z_SPIN:
      if (z->arg % 4 == 0) spin(z->arg / 4, d);
      else if (z->arg % 4 == 1) spin(z->arg / 4, -d);
      break;
    case Z_BTN: if (btn == 1) action(z->arg); break;
    case Z_FX: select_fx(z->arg); if (btn == 1) play(); break;
    case Z_SLOT: slot_sel = z->arg; break;
    case Z_NAME: if (z->arg == 1 && fx[sel].name[0]) naming = 1; if (z->arg == 2) naming = 2; break;
    case Z_HELP: goto_page(help_back); break;
    case Z_WAVE: if (fx[sel].name[0]) { fx[sel].wave = (uint8_t)z->arg; changed(); } break;
    case Z_GEN: generate(z->arg); break;
  }
}
static void mouse(void) {
  if (!mc_mouse_present()) return;
  int mx = mc_mouse_x(), my = mc_mouse_y();
  uint32_t pr = mc_mouse_pressed();
  const ui_zone_t *z = ui_zone_at(mx, my);
  int btn = (pr & MOUSE_LEFT) ? 1 : (pr & MOUSE_RIGHT) ? 2 : 0;
  if (naming && btn && !(z && z->id == Z_NAME)) {
    naming = 0;
    if (!fx[sel].name[0]) snprintf(fx[sel].name, 8, "BRUIT%02d", sel);
  }
  if (btn && z) {
    click(z, btn);
    hold_btn = z->id == Z_SPIN && z->arg % 4 < 2 ? btn : 0; hold_timer = 18; hold_x = mx; hold_y = my;
  } else if (hold_btn) {
    if (!(mc_mouse_buttons() & (hold_btn == 1 ? MOUSE_LEFT : MOUSE_RIGHT))) hold_btn = 0;
    else if (--hold_timer == 0) { hold_timer = 4; const ui_zone_t *h = ui_zone_at(hold_x, hold_y); if (h && h->id == Z_SPIN) click(h, hold_btn); }
  }
  int w = mc_mouse_wheel();
  if (w && z) {
    if (z->id == Z_SPIN) spin(z->arg / 4, w * (ui_shift() ? 10 : 1));
    else if (z->id == Z_FX) select_fx(sel - w);
  }
}

int main(void) {
  mc_init();
  ui_init(UI_OFF);
  if (!ui_ok()) {
    mc_video_mode(384, 240);
    mc_text_layer(0, 3);
    mc_display(DISP_ON | DISP_LAYER(0));
    mc_text(0, 2, 12, "MONOSFX DEMANDE LE CORE", 0);
    mc_text(0, 2, 14, "MONOCORE VERSION 5 (HAUTE RESOLUTION)", 0);
    for (;;) mc_frame();
  }
  PALETTE[P_GRAPH * 16 + 1] = RGB555(90, 220, 255);    // pitch
  PALETTE[P_GRAPH * 16 + 2] = RGB555(220, 170, 60);    // volume
  PALETTE[P_GRAPH * 16 + 3] = RGB555(40, 46, 80);      // octaves
  mc_srand(0x5EED1234u);
  nv_check();
  int last = NV[5];
  if (last < NV_SLOTS && slot_size(last) && bank_load(slot_data(last), slot_size(last))) bank_slot = last;
  else { bank_load(demo_file, demo_size); bank_slot = -1; }
  ui_message("MONOSFX - ESPACE : JOUER, F3 : AIDE");
  for (;;) {
    mc_frame();
    ms_tick();
    frame_count++;
    ui_frame_begin();
    int k;
    while ((k = ui_key()) >= 0) key(k);
    mouse();
    mc_sprite_hide(1);
    if (page != PG_HELP) draw_top();
    if (page == PG_FX) draw_fx();
    else if (page == PG_BANK) draw_bank();
    else draw_help();
    ui_frame_end();
  }
}
