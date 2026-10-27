// MONOATELIER: the launcher of the atelier, a ROM with everything to make a small game on the console.
//
// tools/mkatelier.py puts in one ROM this launcher and complete programs: MONOPIXEL (images, decors,
// objects), MONOSFX (sound effects), MONOTRACKER (music), MONOBASIC (programs) and PLATEFORMES (the
// game). Being one ROM,
// they share one NVRAM (module nvslots): what is saved in an editor is there for the others, and
// PLATEFORMES plays the level, the sounds and the music saved here, with no computer between.
// F12 in each program comes back here. Documentation: ATELIER.md.
#include <stdio.h>
#include <string.h>
#include "mc.h"
#include "ui480.h"
#include "nvslots.h"
#include "atelier.h"
#include "mgfx.h"
#include "msfx.h"
#include "mtrack.h"

#define UI_OFF 0x1A000u
enum { Z_RUN = 1, Z_SLOT };
static const struct { const char *title, *label, *what1, *what2; int app, setting; } ed[3] = {
  { "MONOPIXEL", "F1 IMAGES", "PLANCHE, LUTINS, DECORS,", "ATTRIBUTS ET OBJETS", NVS_PIXEL, 12 },
  { "MONOSFX", "F2 BRUITAGES", "32 BRUITAGES PAR BANQUE", "(SAUT, PIECE, BOUM...)", NVS_SFX, 13 },
  { "MONOTRACKER", "F3 MUSIQUE", "MORCEAUX DE 16 PISTES", "(PSG ET ECHANTILLONS)", NVS_TRACKER, 14 },
};
static char line[UI_COLS + 1];

static const char *item_name(int app, int k) {
  static char s[17];
  const void *d = nvs_data_of(app, k);
  uint32_t n = nvs_size_of(app, k);
  s[0] = 0;
  if (!n) return s;
  if (app == NVS_PIXEL) { mg_bank_t b; if (!mg_open(&b, d, n)) snprintf(s, sizeof s, "%s", mg_name(&b)); }
  else if (app == NVS_SFX) { ms_bank_t b; if (!ms_open(&b, d, n)) { memcpy(s, b.h->name, 16); s[16] = 0; } }
  else snprintf(s, sizeof s, "%.16s", mt_song_name(d));
  if (!s[0]) snprintf(s, sizeof s, "?");
  return s;
}
// the slot the game takes: the one chosen here, else the last one saved by the editor
static int game_slot(int e) {
  int k = nvs_get(ed[e].setting);
  if (k < NVS_SLOTS && nvs_size_of(ed[e].app, k)) return k;
  k = nvs_last_of(ed[e].app);
  return k < NVS_SLOTS && nvs_size_of(ed[e].app, k) ? k : -1;
}
static void run(const char *title) {
  int i = at_find(title);
  if (i < 0) { snprintf(line, sizeof line, "%s N'EST PAS DANS CETTE ROM", title); ui_message(line); return; }
  at_run(i);
}

static void draw(void) {
  ui_box(0, 1, 79, 1, UI_RAISED);
  ui_print(1, 1, "MONOATELIER", UI_INK_BLUE);
  ui_print(14, 1, "- TOUT POUR FAIRE UN PETIT JEU SUR LA CONSOLE", UI_INK);
  static const int x0[3] = { 0, 26, 52 }, x1[3] = { 25, 51, 79 };
  for (int e = 0; e < 3; e++) {
    ui_box(x0[e], 3, x1[e], 16, UI_RAISED);
    ui_button(x0[e] + 2, 4, x1[e] - x0[e] - 3, ed[e].label, false, Z_RUN, e);
    ui_print(x0[e] + 1, 6, ed[e].what1, UI_INK_DIM);
    ui_print(x0[e] + 1, 7, ed[e].what2, UI_INK_DIM);
    int g = game_slot(e);
    for (int k = 0; k < NVS_SLOTS; k++) {
      int y = 8 + k;
      const char *nm = item_name(ed[e].app, k);
      snprintf(line, sizeof line, "%d %-16.16s", k + 1, nm[0] ? nm : "-");
      ui_print(x0[e] + 1, y, line, nm[0] ? (k == g ? UI_INK_BLUE : UI_INK) : UI_INK_DIM);
      if (k == g) ui_put(x1[e] - 2, y, UI_G_RIGHT, UI_INK_RED);
      ui_zone(x0[e] + 1, y, x1[e] - 1, y, Z_SLOT, e * 8 + k);
    }
  }
  ui_box(0, 18, 79, 26, UI_RAISED);
  ui_button(2, 19, 30, "F5 JOUER : PLATEFORMES", false, Z_RUN, 3);
  ui_button(2, 21, 30, "F4 PROGRAMMER : MONOBASIC", false, Z_RUN, 4);
  ui_print(34, 19, "LE JEU PREND :", UI_INK);
  static const char *const kind[3] = { "LE NIVEAU   ", "LES BRUITS  ", "LA MUSIQUE  " };
  for (int e = 0; e < 3; e++) {
    int g = game_slot(e);
    if (g >= 0) snprintf(line, sizeof line, "%s%d %s", kind[e], g + 1, item_name(ed[e].app, g));
    else snprintf(line, sizeof line, "%s(CELUI DU JEU)", kind[e]);
    ui_print(34, 21 + e, line, g >= 0 ? UI_INK_BLUE : UI_INK_DIM);
  }
  ui_print(1, 23, "UN CLIC SUR UN EMPLACEMENT :", UI_INK_DIM);
  ui_print(1, 24, "LE JEU ET LE BASIC LE PRENNENT.", UI_INK_DIM);
  ui_print(1, 25, "F12 DANS CHAQUE PROGRAMME : ICI.", UI_INK_DIM);
  snprintf(line, sizeof line, "NVRAM LIBRE : %u OCTETS SUR 32768 - L'OSD L'ECRIT SUR LA CARTE SD", (unsigned)nvs_free(-1));
  ui_print(1, 26, line, UI_INK_DIM);
  ui_box(0, 29, 79, 29, UI_SUNK);
  const char *m = ui_message_text();
  ui_printn(1, 29, m ? m : "F1 IMAGES  F2 BRUITAGES  F3 MUSIQUE  F4 BASIC  F5 JOUER  -  SOURIS ET CLAVIER", 78, m ? UI_YELLOW : UI_GREY);
}
static void open_editor(int e) { run(ed[e].title); }
int main(void) {
  mc_init();
  ui_init(UI_OFF);
  if (!at_present()) ui_message("CETTE ROM N'EST PAS UN ATELIER : TOOLS/MKATELIER.PY LA FAIT");
  volatile uint8_t *nv = MC_NVRAM;
  if (!(nv[0] == 'A' && nv[1] == 'T' && nv[2] == 'N' && nv[3] == 'V')) nvs_format_atelier();
  nvs_init("ATNV", NVS_GAME, 0);
  ui_layout = nvs_get(4) & 1;
  for (;;) {
    mc_frame();
    ui_frame_begin();
    int k;
    while ((k = ui_key()) >= 0) {
      if (k == KEY_F1) open_editor(0);
      if (k == KEY_F2) open_editor(1);
      if (k == KEY_F3) open_editor(2);
      if (k == KEY_F5 || k == KEY_ENTER) run("PLATEFORMES");
      if (k == KEY_F4) run("MONOBASIC");
    }
    if (mc_mouse_present() && (mc_mouse_pressed() & MOUSE_LEFT)) {
      const ui_zone_t *z = ui_zone_at(mc_mouse_x(), mc_mouse_y());
      if (z && z->id == Z_RUN) { if (z->arg < 3) open_editor(z->arg); else run(z->arg == 4 ? "MONOBASIC" : "PLATEFORMES"); }
      if (z && z->id == Z_SLOT) {
        int e = z->arg / 8, s = z->arg % 8;
        if (nvs_size_of(ed[e].app, s)) nvs_set(ed[e].setting, nvs_get(ed[e].setting) == s ? 0xFF : s);
      }
    }
    draw();
    ui_frame_end();
  }
}
