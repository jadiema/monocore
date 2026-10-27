// MONOBASIC: the BASIC of MONOCORE-1, to write small games on the console itself.
//
// A BASIC with line numbers and a full-screen editor in the manner of the MO5 and the C64: a line
// typed with a number is kept in the program, one without is done at once (RUN, LIST, SAVE "NOM"...).
// The classic core (PRINT, IF THEN ELSE, FOR NEXT, WHILE WEND, GOSUB, GOTO, DIM, integers and
// strings) and statements in French for the games, on the modules of the SDK: DECOR shows a map of
// a MONOPIXEL bank and makes its objects, BOUGE moves them against the tiles (mjeu), CHOC, SOL,
// DANGER tell what they meet, SON plays a MONOSFX sound, MUSIQUE a MONOTRACKER song, ATTENDS draws
// a frame. In the atelier (sdk/ATELIER.md) the program takes the images, sounds and music saved
// there; alone, the demo banks. Programs are kept in the NVRAM (8 slots, as text). Doc: LISEZMOI.md.
#include <stdio.h>
#include <string.h>
#include "mc.h"
#include "ui480.h"
#include "nvslots.h"
#include "atelier.h"
#include "mgfx.h"
#include "mjeu.h"
#include "msfx.h"
#include "mtrack.h"

#define UI_OFF 0x1A000u
#define ROWS 28                       // the editing screen: rows 0-27; 29 the status line
#define COLS 80

// ================================================================ keyboard: every character
static int kb_char(int k, bool sh) {
  if (k == KEY_SPACE) return ' ';
  int c = ui_char(k);                 // letters (AZERTY or QWERTY), digits of the keypad
  if (c >= 'A' && c <= 'Z') return c;
  if (k == KEY_KP0 || k == KEY_KP1 || k == KEY_KP2 || k == KEY_KP3 || k == KEY_KP4 || k == KEY_KP5 || k == KEY_KP6 ||
      k == KEY_KP7 || k == KEY_KP8 || k == KEY_KP9) return c;
  switch (k) {                        // the keypad
    case 0x7C: return '*';
    case 0x7B: return '-';
    case 0x79: return '+';
    case 0x71: return '.';
    case 0x14A: return '/';
  }
  static const uint16_t row[12] = { 0x16, 0x1E, 0x26, 0x25, 0x2E, 0x36, 0x3D, 0x3E, 0x46, 0x45, 0x4E, 0x55 };
  if (ui_layout == 0) {               // AZERTY (the letters with accents give their digit)
    static const char low[12] = { '&', '2', '"', '\'', '(', '-', '7', '_', '9', '0', ')', '=' };
    static const char up[12] = { '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', ' ', '+' };
    for (int i = 0; i < 12; i++) if (k == row[i]) return sh ? up[i] : low[i];
    switch (k) {
      case 0x54: return '^';
      case 0x5B: return sh ? ' ' : '$';
      case 0x52: return sh ? '%' : '\'';
      case 0x5D: return '*';
      case 0x3A: return sh ? '?' : ',';      // the key M of QWERTY
      case 0x41: return sh ? '.' : ';';
      case 0x49: return sh ? '/' : ':';
      case 0x4A: return '!';
      case 0x61: return sh ? '>' : '<';
      case 0x0E: return ' ';
    }
    return 0;
  }
  static const char low[12] = { '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=' };
  static const char up[12] = { '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+' };
  for (int i = 0; i < 12; i++) if (k == row[i]) return sh ? up[i] : low[i];
  switch (k) {
    case 0x54: return sh ? '{' : '[';
    case 0x5B: return sh ? '}' : ']';
    case 0x5D: return sh ? '|' : '\\';
    case 0x4C: return sh ? ':' : ';';
    case 0x52: return sh ? '"' : '\'';
    case 0x41: return sh ? '<' : ',';
    case 0x49: return sh ? '>' : '.';
    case 0x4A: return sh ? '?' : '/';
    case 0x0E: return sh ? '~' : '`';
    case 0x61: return sh ? '>' : '<';
  }
  return 0;
}

// ================================================================ the editing screen
static char scr[ROWS][COLS + 1];
static int cx, cy;
static void scroll_up(void) { memmove(scr[0], scr[1], sizeof scr[0] * (ROWS - 1)); memset(scr[ROWS - 1], ' ', COLS); }
static void con_newline(void) { cx = 0; if (++cy >= ROWS) { cy = ROWS - 1; scroll_up(); } }
static void con_putc(char c) {
  if (c == '\n') { con_newline(); return; }
  if (cx >= COLS) con_newline();
  scr[cy][cx++] = c;
}
static void con_puts(const char *s) { while (*s) con_putc(*s++); }
static void con_line(const char *s) { if (cx) con_newline(); con_puts(s); con_newline(); }
static void con_clear(void) { for (int y = 0; y < ROWS; y++) { memset(scr[y], ' ', COLS); scr[y][COLS] = 0; } cx = cy = 0; }

// ================================================================ tokens
enum {
  T_PRINT = 0x80, T_IF, T_THEN, T_ELSE, T_GOTO, T_GOSUB, T_RETURN, T_FOR, T_TO, T_STEP, T_NEXT, T_END, T_REM,
  T_LET, T_DIM, T_WHILE, T_WEND, T_AND, T_OR, T_NOT, T_MOD, T_RND, T_ABS, T_CLS, T_STOP, T_STR, T_CHR, T_LEN,
  T_RUN, T_LIST, T_NEW, T_SAVE, T_LOAD, T_DIR, T_AIDE, T_EXEMPLE,
  // games
  T_DECOR, T_OBJETS, T_CHERCHE, T_EST, T_CREE, T_POSX, T_POSY, T_VITX, T_VITY, T_SOL, T_PLAFOND, T_MUR, T_CHOC,
  T_DANGER, T_EAU, T_ECHELLE, T_ACTIF, T_NOM, T_PLACE, T_VITESSE, T_GRAVITE, T_BOUGE, T_PATROUILLE, T_ENLEVE,
  T_CACHE, T_MIROIR, T_ANIME, T_IMAGE, T_BOITE, T_CAMERA, T_ATTENDS, T_SON, T_MUSIQUE, T_SILENCE, T_TEXTE,
  T_FOND, T_GAUCHE, T_DROITE, T_HAUT, T_BAS, T_BOUTON, T_APPUI, T_TEMPS, T_LAST
};
static const char *const kw[T_LAST - 0x80] = {
  "PRINT", "IF", "THEN", "ELSE", "GOTO", "GOSUB", "RETURN", "FOR", "TO", "STEP", "NEXT", "END", "REM",
  "LET", "DIM", "WHILE", "WEND", "AND", "OR", "NOT", "MOD", "RND", "ABS", "CLS", "STOP", "STR$", "CHR$", "LEN",
  "RUN", "LIST", "NEW", "SAVE", "LOAD", "DIR", "AIDE", "EXEMPLE",
  "DECOR", "OBJETS", "CHERCHE", "EST", "CREE", "POSX", "POSY", "VITX", "VITY", "SOL", "PLAFOND", "MUR", "CHOC",
  "DANGER", "EAU", "ECHELLE", "ACTIF", "NOM$", "PLACE", "VITESSE", "GRAVITE", "BOUGE", "PATROUILLE", "ENLEVE",
  "CACHE", "MIROIR", "ANIME", "IMAGE", "BOITE", "CAMERA", "ATTENDS", "SON", "MUSIQUE", "SILENCE", "TEXTE",
  "FOND", "GAUCHE", "DROITE", "HAUT", "BAS", "BOUTON", "APPUI", "TEMPS" };
static bool alnum(int c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }
static int upper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
// a line of text -> tokens (keywords as whole words; strings and REM kept as they are)
static int tokenize(const char *s, uint8_t *out, int max) {
  int n = 0, rem = 0;
  while (*s && n < max - 1) {
    int c = upper((unsigned char)*s);
    if (rem) { out[n++] = (uint8_t)*s++; continue; }
    if (c == '"') {
      out[n++] = (uint8_t)*s++;
      while (*s && *s != '"' && n < max - 2) out[n++] = (uint8_t)*s++;
      if (*s == '"') out[n++] = (uint8_t)*s++;
      continue;
    }
    if (c >= 'A' && c <= 'Z' && (n == 0 || !alnum(out[n - 1]))) {
      int best = -1, blen = 0;
      for (int t = 0; t < T_LAST - 0x80; t++) {
        int l = (int)strlen(kw[t]), i = 0;
        while (i < l && upper((unsigned char)s[i]) == kw[t][i]) i++;
        if (i == l && l > blen && (kw[t][l - 1] == '$' || !alnum(upper((unsigned char)s[l])))) { best = t; blen = l; }
      }
      if (best >= 0) { out[n++] = (uint8_t)(0x80 + best); s += blen; if (best + 0x80 == T_REM) rem = 1; continue; }
      while (alnum(upper((unsigned char)*s)) && n < max - 1) out[n++] = (uint8_t)upper((unsigned char)*s++);
      if (*s == '$' && n < max - 1) out[n++] = (uint8_t)*s++;
      continue;
    }
    out[n++] = (uint8_t)c; s++;
  }
  out[n] = 0;
  return n;
}
static int detokenize(const uint8_t *t, char *out, int max) {
  int n = 0;
  for (; *t && n < max - 1; t++) {
    if (*t >= 0x80 && *t < T_LAST) {
      const char *k = kw[*t - 0x80];
      bool word_before = n > 0 && alnum(out[n - 1]);
      if (word_before && n < max - 1) out[n++] = ' ';
      while (*k && n < max - 1) out[n++] = *k++;
      if (t[1] && alnum(t[1]) && n < max - 1) out[n++] = ' ';
    } else out[n++] = (char)*t;
  }
  out[n] = 0;
  return n;
}

// ================================================================ the program: lines kept in order
// a line: u16 number, u8 length, its tokens, 0
#define PROG_MAX 20000
static uint8_t prog[PROG_MAX];
static int prog_len;
static int line_num(int off) { return prog[off] | (prog[off + 1] << 8); }
static int line_next(int off) { return off + 4 + prog[off + 2]; }
static int find_line(int num) {      // the offset of line num, or -1
  for (int o = 0; o < prog_len; o = line_next(o)) if (line_num(o) == num) return o;
  return -1;
}
static void delete_line(int num) {
  int o = find_line(num);
  if (o < 0) return;
  int n = line_next(o) - o;
  memmove(prog + o, prog + o + n, prog_len - o - n);
  prog_len -= n;
}
static bool insert_line(int num, const uint8_t *t, int len) {
  delete_line(num);
  if (!len) return true;
  if (prog_len + len + 4 > PROG_MAX) return false;
  int o = 0;
  while (o < prog_len && line_num(o) < num) o = line_next(o);
  memmove(prog + o + len + 4, prog + o, prog_len - o);
  prog[o] = (uint8_t)num; prog[o + 1] = (uint8_t)(num >> 8); prog[o + 2] = (uint8_t)len;
  memcpy(prog + o + 3, t, len); prog[o + 3 + len] = 0;
  prog_len += len + 4;
  return true;
}
// a line typed or loaded: "120 PRINT ..." -> kept; returns 0, 1 if it has no number, -1 if full
static int enter_line(const char *s) {
  while (*s == ' ') s++;
  if (*s < '0' || *s > '9') return 1;
  int num = 0;
  while (*s >= '0' && *s <= '9') num = num * 10 + (*s++ - '0');
  if (num < 1 || num > 65535) return 1;
  if (*s == ' ') s++;
  static uint8_t t[256];
  int n = tokenize(s, t, 250);
  while (n && t[n - 1] == ' ') t[--n] = 0;
  return insert_line(num, t, n) ? 0 : -1;
}

// ================================================================ errors, variables
static int err;
static const char *err_msg;
static void error(const char *m) { if (!err) { err = 1; err_msg = m; } }

#define NVARS 160
#define SVARS 40
#define SLEN 80
#define AVARS 24
#define APOOL 8192
typedef struct { char name[9]; int32_t v; } nvar_t;
typedef struct { char name[9]; char s[SLEN + 1]; } svar_t;
typedef struct { char name[9]; int32_t *d; int n; } avar_t;
static nvar_t nvars[NVARS];
static svar_t svars[SVARS];
static avar_t avars[AVARS];
static int32_t apool[APOOL];
static int n_nv, n_sv, n_av, apool_used;
static void clear_vars(void) { n_nv = n_sv = n_av = apool_used = 0; }
static int32_t *num_var(const char *name) {
  for (int i = 0; i < n_nv; i++) if (!strcmp(nvars[i].name, name)) return &nvars[i].v;
  if (n_nv >= NVARS) { error("TROP DE VARIABLES"); return &nvars[0].v; }
  strcpy(nvars[n_nv].name, name); nvars[n_nv].v = 0;
  return &nvars[n_nv++].v;
}
static char *str_var(const char *name) {
  for (int i = 0; i < n_sv; i++) if (!strcmp(svars[i].name, name)) return svars[i].s;
  if (n_sv >= SVARS) { error("TROP DE VARIABLES"); return svars[0].s; }
  strcpy(svars[n_sv].name, name); svars[n_sv].s[0] = 0;
  return svars[n_sv++].s;
}
static avar_t *dim(const char *name, int n) {
  for (int i = 0; i < n_av; i++) if (!strcmp(avars[i].name, name)) return &avars[i];
  if (n_av >= AVARS || n < 1 || apool_used + n > APOOL) { error("MEMOIRE PLEINE"); return 0; }
  avar_t *a = &avars[n_av++];
  strcpy(a->name, name); a->d = apool + apool_used; a->n = n;
  memset(a->d, 0, n * sizeof(int32_t)); apool_used += n;
  return a;
}
static int32_t *arr_elem(const char *name, int idx) {
  static int32_t dummy;
  avar_t *a = 0;
  for (int i = 0; i < n_av; i++) if (!strcmp(avars[i].name, name)) a = &avars[i];
  if (!a) a = dim(name, 11);                  // as in the old BASICs: 0 to 10 without DIM
  if (!a) return &dummy;
  if (idx < 0 || idx >= a->n) { error("INDICE HORS LIMITES"); return &dummy; }
  return &a->d[idx];
}

// ================================================================ running
static const uint8_t *p;              // the token being read
static int cur = -1;                  // the line being run (offset in prog); -1: the line typed
static bool running, jumped, in_game;
static uint8_t imm[256];
static int err_lineno;
typedef struct { int kind; int32_t *var; int32_t limit, step; int line; const uint8_t *pos; } frame_t;
enum { F_FOR = 1, F_GOSUB, F_WHILE };
static frame_t stack[48];
static int sp;
static void skip(void) { while (*p == ' ') p++; }
static bool accept(int c) { skip(); if (*p == c) { p++; return true; } return false; }
static void expect(int c) { if (!accept(c)) error("SYNTAXE"); }
static bool read_name(char *name) {
  skip();
  if (!(*p >= 'A' && *p <= 'Z')) return false;
  int n = 0;
  while (alnum(*p)) { if (n < 8) name[n++] = (char)*p; p++; }
  name[n] = 0;
  return true;
}
static int32_t expr(void);
static void str_expr(char *out);
static int32_t game_fn(int t);
static char nom_result[17];               // NOM$(i): the name of the sprite of an object
static bool string_ahead(void) {
  skip();
  if (*p == '"' || *p == T_STR || *p == T_CHR || *p == T_NOM) return true;
  if (*p >= 'A' && *p <= 'Z') { const uint8_t *q = p; while (alnum(*q)) q++; return *q == '$'; }
  return false;
}
static void str_term(char *out) {
  skip();
  out[0] = 0;
  if (*p == '"') {
    p++;
    int n = 0;
    while (*p && *p != '"') { if (n < SLEN) out[n++] = (char)*p; p++; }
    out[n] = 0;
    if (*p == '"') p++;
  } else if (*p == T_STR) { p++; expect('('); int32_t v = expr(); expect(')'); snprintf(out, SLEN + 1, "%d", (int)v); }
  else if (*p == T_CHR) { p++; expect('('); int32_t v = expr(); expect(')'); out[0] = (char)(v >= 32 && v < 127 ? v : ' '); out[1] = 0; }
  else if (*p == T_NOM) { p++; game_fn(T_NOM); snprintf(out, SLEN + 1, "%s", nom_result); }
  else {
    char name[9];
    if (!read_name(name) || !accept('$')) { error("TYPE"); return; }
    snprintf(out, SLEN + 1, "%s", str_var(name));
  }
}
static void str_expr(char *out) {
  str_term(out);
  while (accept('+')) {
    char t[SLEN + 1];
    str_term(t);
    int n = (int)strlen(out);
    snprintf(out + n, SLEN + 1 - n, "%s", t);
  }
}
static int32_t primary(void) {
  skip();
  int c = *p;
  if (c >= '0' && c <= '9') { int32_t v = 0; while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0'); return v; }
  if (c == '(') { p++; int32_t v = expr(); expect(')'); return v; }
  if (c == T_RND) { p++; expect('('); int32_t n = expr(); expect(')'); return n > 0 ? (int32_t)(mc_rand() % (uint32_t)n) : 0; }
  if (c == T_ABS) { p++; expect('('); int32_t n = expr(); expect(')'); return n < 0 ? -n : n; }
  if (c == T_LEN) { p++; expect('('); char s[SLEN + 1]; str_expr(s); expect(')'); return (int32_t)strlen(s); }
  if (c >= T_DECOR && c < T_LAST) { p++; return game_fn(c); }
  char name[9];
  if (read_name(name)) {
    if (*p == '$') { error("TYPE"); return 0; }
    if (accept('(')) { int32_t i = expr(); expect(')'); return *arr_elem(name, i); }
    return *num_var(name);
  }
  error("SYNTAXE");
  return 0;
}
static int32_t unary(void) {
  if (accept('-')) return -unary();
  if (accept('+')) return unary();
  return primary();
}
static int32_t term(void) {
  int32_t v = unary();
  for (;;) {
    if (accept('*')) v *= unary();
    else if (accept('/')) { int32_t d = unary(); if (!d) { error("DIVISION PAR ZERO"); return 0; } v /= d; }
    else if (accept(T_MOD)) { int32_t d = unary(); if (!d) { error("DIVISION PAR ZERO"); return 0; } v %= d; }
    else return v;
  }
}
static int32_t sum(void) {
  int32_t v = term();
  for (;;) {
    if (accept('+')) v += term();
    else if (accept('-')) v -= term();
    else return v;
  }
}
static int rel_op(void) {             // 1 =, 2 <>, 3 <, 4 >, 5 <=, 6 >=; 0 none
  skip();
  if (*p == '=') { p++; return 1; }
  if (*p == '<') { p++; if (*p == '>') { p++; return 2; } if (*p == '=') { p++; return 5; } return 3; }
  if (*p == '>') { p++; if (*p == '=') { p++; return 6; } return 4; }
  return 0;
}
static int32_t compare(int op, int c) {
  switch (op) { case 1: return c == 0; case 2: return c != 0; case 3: return c < 0; case 4: return c > 0; case 5: return c <= 0; default: return c >= 0; }
}
static int32_t rel(void) {
  if (string_ahead()) {
    char a[SLEN + 1], b[SLEN + 1];
    str_expr(a);
    int op = rel_op();
    if (!op) { error("TYPE"); return 0; }
    str_expr(b);
    return compare(op, strcmp(a, b));
  }
  int32_t v = sum();
  const uint8_t *save = p;
  int op = rel_op();
  if (!op) { p = save; return v; }
  int32_t w = sum();
  return compare(op, v < w ? -1 : v > w ? 1 : 0);
}
static int32_t not_e(void) { if (accept(T_NOT)) return !not_e(); return rel(); }
static int32_t and_e(void) { int32_t v = not_e(); while (accept(T_AND)) { int32_t w = not_e(); v = v && w; } return v; }
static int32_t expr(void) { int32_t v = and_e(); while (accept(T_OR)) { int32_t w = and_e(); v = v || w; } return v; }

// ================================================================ the game: banks, objects, screen
#define MAXOBJ 96
static mg_bank_t bank;
static ms_bank_t sfx;
static mj_world_t world;
static mj_obj_t obj[MAXOBJ];
static int n_obj, have_world, have_bank, text_x, text_y;
static const void *song;
static uint32_t pad_now, pad_prev, game_frames;
static bool game_check(void) { if (!in_game) error("SEULEMENT DANS UN PROGRAMME (RUN)"); return in_game; }
static bool world_check(void) { if (game_check() && !have_world) error("PAS DE DECOR (DECOR 0 D'ABORD)"); return have_world; }
static mj_obj_t *obj_arg(void) {     // an object number as argument
  int32_t i = expr();
  static mj_obj_t none;
  if (!world_check()) return &none;
  if (i < 0 || i >= n_obj) { error("OBJET INEXISTANT"); return &none; }
  return &obj[i];
}
// in the atelier: the item chosen in the launcher (else the editor's last one), if valid
static const void *atelier_item(int app, int setting, uint32_t *size, int (*check)(const void *, uint32_t)) {
  if (!at_present() || !nvs_shared()) return 0;
  int k = nvs_get(setting);
  if (k >= NVS_SLOTS || !nvs_size_of(app, k)) k = nvs_last_of(app);
  if (k >= NVS_SLOTS || !nvs_size_of(app, k) || check(nvs_data_of(app, k), nvs_size_of(app, k))) return 0;
  *size = nvs_size_of(app, k);
  return nvs_data_of(app, k);
}
static void game_start(void) {
  in_game = true; have_world = 0; n_obj = 0; text_x = text_y = 0; pad_now = pad_prev = 0; game_frames = 0;
  mc_display(0);
  mc_video_mode(320, 240);
  for (int l = 0; l < 4; l++) { mc_layer_linescroll(l, false); mc_layer_scroll(l, 0, 0); mc_tilemap_fill(l, 0); }
  for (int i = 0; i < 128; i++) mc_sprite_hide(i);
  uint32_t n = images_size, ns = bruits_size, nm = musique_size;
  const void *level = images_file, *sounds = bruits_file, *q;
  song = musique_file;
  if ((q = atelier_item(NVS_PIXEL, 12, &n, mg_check))) level = q; else n = images_size;
  if ((q = atelier_item(NVS_SFX, 13, &ns, ms_check))) sounds = q; else ns = bruits_size;
  if ((q = atelier_item(NVS_TRACKER, 14, &nm, mt_song_check))) song = q;
  have_bank = !mg_open(&bank, level, n) && mg_load(&bank, 0, 16, MG_LOAD_ALL);
  ms_open(&sfx, sounds, ns);
  mc_text_layer(0, 3);
  PALETTE[1] = RGB555(255, 255, 255);
  mc_color(15 * 16 + 1, MC_RGB(20, 24, 48));
  mc_background(15 * 16 + 1);
  mc_display(DISP_ON | DISP_LAYER(0) | DISP_LAYER(1) | DISP_SPRITES);
}
static void game_text(int x, int y, const char *s) { if (x >= 0 && y >= 0 && y < 30) mc_text(0, x, y, s, 0); }
static void game_frame(void) {
  if (have_world) mj_draw_all(&world, obj, n_obj, 1, 4);
  mj_frame();
  game_frames++;
  pad_prev = pad_now;
  pad_now = mc_pad(1);
  if (mc_key(KEY_LEFT)) pad_now |= IN_LEFT;   // the keyboard of the console too
  if (mc_key(KEY_RIGHT)) pad_now |= IN_RIGHT;
  if (mc_key(KEY_UP)) pad_now |= IN_UP;
  if (mc_key(KEY_DOWN)) pad_now |= IN_DOWN;
  if (mc_key(KEY_SPACE)) pad_now |= IN_B(1);
  if (mc_key(KEY_LALT)) pad_now |= IN_B(2);
  if (mc_key(KEY_ESC)) error("ARRET");
}
static void str_arg(char *s) { str_expr(s); }
static int sprite_named(const char *s) { return have_bank ? mg_sprite_find(&bank, s) : -1; }
static int32_t game_fn(int t) {
  char s[SLEN + 1];
  int32_t a;
  mj_obj_t *o;
  switch (t) {
    case T_OBJETS: return n_obj;
    case T_GAUCHE: return (pad_now & IN_LEFT) != 0;
    case T_DROITE: return (pad_now & IN_RIGHT) != 0;
    case T_HAUT: return (pad_now & IN_UP) != 0;
    case T_BAS: return (pad_now & IN_DOWN) != 0;
    case T_TEMPS: return (int32_t)game_frames;
    case T_BOUTON: expect('('); a = expr(); expect(')'); return a >= 1 && a <= 6 && (pad_now & IN_B(a));
    case T_APPUI: expect('('); a = expr(); expect(')'); return a >= 1 && a <= 6 && (pad_now & IN_B(a)) && !(pad_prev & IN_B(a));
    case T_CHERCHE: {
      expect('('); str_arg(s);
      int from = -1;
      if (accept(',')) from = expr();
      expect(')');
      if (!world_check()) return -1;
      int sp_no = sprite_named(s);
      for (int i = from + 1; i < n_obj; i++) if (obj[i].sprite == sp_no && (obj[i].flags & MJ_ACTIVE)) return i;
      return -1;
    }
    case T_CREE: {
      expect('('); str_arg(s); expect(',');
      int32_t x = expr(); expect(','); int32_t y = expr(); expect(')');
      if (!world_check()) return -1;
      int sp_no = sprite_named(s);
      if (sp_no < 0) { error("LUTIN INCONNU"); return -1; }
      if (n_obj >= MAXOBJ) { error("TROP D'OBJETS"); return -1; }
      mj_obj(&obj[n_obj], &world, sp_no, x, y);
      return n_obj++;
    }
    case T_EST: expect('('); o = obj_arg(); expect(','); str_arg(s); expect(')'); return o->sprite == sprite_named(s);
    case T_CHOC: { expect('('); o = obj_arg(); expect(','); mj_obj_t *b = obj_arg(); expect(')'); return o != b && mj_overlap(o, b); }
    case T_NOM: {
      expect('('); o = obj_arg(); expect(')');
      const mg_sprite_t *d = have_bank ? mg_sprite_def(&bank, o->sprite) : 0;
      memset(nom_result, 0, sizeof nom_result);
      if (d) memcpy(nom_result, d->name, 8);
      return 0;
    }
  }
  expect('('); o = obj_arg(); expect(')');      // the functions of one object
  switch (t) {
    case T_POSX: return mj_px(o);
    case T_POSY: return mj_py(o);
    case T_VITX: return o->vx;
    case T_VITY: return o->vy;
    case T_SOL: return (o->touch & MJ_GROUND) != 0;
    case T_PLAFOND: return (o->touch & MJ_CEILING) != 0;
    case T_MUR: return (o->touch & (MJ_LEFT | MJ_RIGHT)) != 0;
    case T_DANGER: return have_world && (o->flags & MJ_ACTIVE) && mj_touches(&world, o, MG_DANGER);
    case T_EAU: return have_world && (o->flags & MJ_ACTIVE) && mj_touches(&world, o, MG_WATER);
    case T_ECHELLE: return have_world && (o->flags & MJ_ACTIVE) && mj_touches(&world, o, MG_LADDER);
    case T_ACTIF: return (o->flags & MJ_ACTIVE) != 0;
  }
  error("SYNTAXE");
  return 0;
}
static void set_flag(mj_obj_t *o, int f, int32_t on) { if (on) o->flags |= (uint8_t)f; else o->flags &= (uint8_t)~f; }
static bool game_statement(int t) {  // false: not a game statement
  char s[SLEN + 1];
  mj_obj_t *o;
  int32_t a, b;
  switch (t) {
    case T_DECOR:
      a = expr();
      if (!game_check()) return true;
      if (!have_bank || a < 0 || a >= mg_map_count(&bank)) { error("DECOR INEXISTANT"); return true; }
      for (int i = 0; i < 128; i++) mc_sprite_hide(i);
      mj_world(&world, &bank, a, 1);
      n_obj = mj_from_map(&world, obj, MAXOBJ);
      have_world = 1;
      return true;
    case T_PLACE: o = obj_arg(); expect(','); a = expr(); expect(','); b = expr(); mj_set_pos(o, a, b); return true;
    case T_VITESSE: o = obj_arg(); expect(','); a = expr(); expect(','); b = expr(); o->vx = (int16_t)a; o->vy = (int16_t)b; return true;
    case T_GRAVITE: o = obj_arg(); expect(','); set_flag(o, MJ_GRAVITY, expr()); return true;
    case T_CACHE: o = obj_arg(); expect(','); set_flag(o, MJ_HIDDEN, expr()); return true;
    case T_MIROIR: o = obj_arg(); expect(','); set_flag(o, MJ_HFLIP, expr()); return true;
    case T_ANIME: o = obj_arg(); expect(','); set_flag(o, MJ_ANIM, expr()); return true;
    case T_IMAGE: o = obj_arg(); expect(','); o->frame = (uint8_t)expr(); o->flags &= ~MJ_ANIM; return true;
    case T_ENLEVE: o = obj_arg(); o->flags &= ~MJ_ACTIVE; return true;
    case T_BOITE: {
      o = obj_arg(); expect(',');
      int32_t bx = expr(); expect(','); int32_t by = expr(); expect(','); int32_t bw = expr(); expect(','); int32_t bh = expr();
      mj_box(o, bx, by, bw, bh);
      return true;
    }
    case T_PATROUILLE: o = obj_arg(); expect(','); a = expr(); if (have_world) mj_patrol(&world, o, a, true); return true;
    case T_BOUGE:
      skip();
      if (*p == 0 || *p == ':' || *p == T_ELSE) {   // all the objects
        if (world_check()) for (int i = 0; i < n_obj; i++) mj_move(&world, &obj[i]);
        return true;
      }
      o = obj_arg(); if (have_world) mj_move(&world, o);
      return true;
    case T_CAMERA: o = obj_arg(); if (have_world) mj_camera(&world, o); return true;
    case T_ATTENDS:
      skip();
      a = (*p && *p != ':' && *p != T_ELSE) ? expr() : 1;
      if (!game_check()) return true;
      for (int i = 0; i < a && !err; i++) game_frame();
      return true;
    case T_SON: str_arg(s); if (game_check()) ms_play(&sfx, ms_find(&sfx, s)); return true;
    case T_MUSIQUE: if (game_check() && song) mt_play(song, 0); return true;
    case T_SILENCE: if (game_check()) { mt_stop(); ms_stop_all(); } return true;
    case T_FOND: {
      int32_t r = expr(); expect(','); int32_t g = expr(); expect(','); int32_t bl = expr();
      if (game_check()) mc_color(15 * 16 + 1, MC_RGB(r & 255, g & 255, bl & 255));
      return true;
    }
  }
  return false;
}

// ================================================================ statements
static void goto_line(int num) {
  int o = find_line(num);
  if (o < 0) { error("LIGNE INEXISTANTE"); return; }
  cur = o; p = prog + o + 3; jumped = true;
}
static int read_number(void) { skip(); int v = 0; while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0'); return v; }
static bool push(frame_t f) { if (sp >= 48) { error("TROP DE BOUCLES OU DE GOSUB"); return false; } stack[sp++] = f; return true; }
static void print_items(bool game, int x, int y) {      // PRINT and TEXTE: expressions and strings, ; and ,
  char out[COLS + 1], s[SLEN + 1];
  int n = 0;
  bool newline = true;
  out[0] = 0;
  for (;;) {
    skip();
    if (*p == 0 || *p == ':' || *p == T_ELSE) break;
    if (string_ahead()) str_expr(s); else snprintf(s, sizeof s, "%d", (int)expr());
    if (err) return;
    n += snprintf(out + n, sizeof out - n, "%s", s);
    if (n > COLS) n = COLS;
    newline = true;
    if (accept(';')) newline = false;
    else if (accept(',')) { newline = false; while (n < COLS && n % 10) out[n++] = ' '; out[n] = 0; }
    else break;
  }
  if (x >= 0) { game_text(x, y, out); return; }
  if (game) {
    game_text(text_x, text_y, out);
    text_x += n;
    if (newline) { text_x = 0; if (++text_y >= 30) text_y = 0; }
  } else { con_puts(out); if (newline) con_newline(); }
}
static void skip_to_else(void) {      // IF false: to the ELSE of this IF, or the end of the line
  int depth = 0;
  for (; *p; p++) {
    if (*p == '"') { p++; while (*p && *p != '"') p++; if (!*p) break; continue; }
    if (*p == T_IF) depth++;
    if (*p == T_ELSE) { if (!depth) { p++; return; } depth--; }
  }
}
static void list(int a, int b) {
  char s[300];
  for (int o = 0; o < prog_len; o = line_next(o)) {
    int n = line_num(o);
    if (n < a || n > b) continue;
    int k = snprintf(s, sizeof s, "%d ", n);
    detokenize(prog + o + 3, s + k, sizeof s - k);
    con_line(s);
  }
}
static void cmd_run(int from);
static void cmd_save(void), cmd_load(void), cmd_dir(void), cmd_help(void), cmd_example(void);
static void statement(void) {
  skip();
  int t = *p;
  if (t >= 0x80) {
    p++;
    if (game_statement(t)) return;
    switch (t) {
      case T_PRINT: print_items(in_game, -1, 0); return;
      case T_TEXTE: { int32_t x = expr(); expect(','); int32_t y = expr(); accept(','); if (game_check()) print_items(true, x, y); return; }
      case T_CLS:
        if (in_game) { for (int y = 0; y < 30; y++) game_text(0, y, "                                        "); text_x = text_y = 0; }
        else con_clear();
        return;
      case T_REM: while (*p) p++; return;
      case T_LET: break;
      case T_GOTO: goto_line(read_number()); if (cur >= 0 && !in_game && !err) cmd_run(cur); return;
      case T_GOSUB: {
        int n = read_number();
        if (push((frame_t){ F_GOSUB, 0, 0, 0, cur, p })) goto_line(n);
        return;
      }
      case T_RETURN:
        while (sp && stack[sp - 1].kind != F_GOSUB) sp--;
        if (!sp) { error("RETURN SANS GOSUB"); return; }
        sp--; cur = stack[sp].line; p = stack[sp].pos; jumped = true;
        return;
      case T_IF: {
        int32_t v = expr();
        if (!accept(T_THEN)) { skip(); if (*p != T_GOTO) { error("SYNTAXE"); return; } }
        if (v) { skip(); if (*p >= '0' && *p <= '9') goto_line(read_number()); else jumped = true; return; }   // what follows THEN
        skip_to_else();
        skip();
        if (*p >= '0' && *p <= '9') goto_line(read_number());
        else jumped = true;                       // what follows ELSE (or the end of the line)
        return;
      }
      case T_FOR: {
        char name[9];
        if (!read_name(name)) { error("SYNTAXE"); return; }
        int32_t *v = num_var(name);
        expect('='); *v = expr();
        expect(T_TO); int32_t lim = expr();
        int32_t step = accept(T_STEP) ? expr() : 1;
        for (int i = sp - 1; i >= 0; i--) if (stack[i].kind == F_FOR && stack[i].var == v) { sp = i; break; }
        push((frame_t){ F_FOR, v, lim, step, cur, p });
        return;
      }
      case T_NEXT: {
        char name[9];
        int32_t *v = read_name(name) ? num_var(name) : 0;
        while (sp && (stack[sp - 1].kind != F_FOR || (v && stack[sp - 1].var != v))) {
          if (stack[sp - 1].kind == F_GOSUB) break;
          sp--;
        }
        if (!sp || stack[sp - 1].kind != F_FOR) { error("NEXT SANS FOR"); return; }
        frame_t *f = &stack[sp - 1];
        *f->var += f->step;
        if (f->step >= 0 ? *f->var <= f->limit : *f->var >= f->limit) { cur = f->line; p = f->pos; jumped = true; }
        else sp--;
        return;
      }
      case T_WHILE: {
        const uint8_t *start = p - 1;
        int line = cur;
        if (expr()) { push((frame_t){ F_WHILE, 0, 0, 0, line, start }); return; }
        // false: after the WEND that goes with it
        int depth = 0;
        for (;;) {
          for (; *p; p++) {
            if (*p == '"') { p++; while (*p && *p != '"') p++; if (!*p) break; continue; }
            if (*p == T_WHILE) depth++;
            if (*p == T_WEND) { if (!depth) { p++; jumped = true; return; } depth--; }
          }
          if (cur < 0 || line_next(cur) >= prog_len) { error("WHILE SANS WEND"); return; }
          cur = line_next(cur); p = prog + cur + 3;
        }
      }
      case T_WEND:
        while (sp && stack[sp - 1].kind == F_FOR) sp--;
        if (!sp || stack[sp - 1].kind != F_WHILE) { error("WEND SANS WHILE"); return; }
        sp--; cur = stack[sp].line; p = stack[sp].pos; jumped = true;
        return;
      case T_END: running = false; return;
      case T_STOP: running = false; error("STOP"); return;
      case T_DIM:
        do {
          char name[9];
          if (!read_name(name)) { error("SYNTAXE"); return; }
          expect('('); int32_t n = expr(); expect(')');
          if (!err) dim(name, n + 1);
        } while (accept(','));
        return;
      // the commands of the editor
      case T_RUN: if (cur >= 0) { error("RUN DANS UN PROGRAMME"); return; } skip(); cmd_run(*p >= '0' && *p <= '9' ? find_line(read_number()) : 0); return;
      case T_LIST: {
        int a = 0, b = 65535;
        skip();
        if (*p >= '0' && *p <= '9') { a = b = read_number(); }
        if (accept('-')) { skip(); b = (*p >= '0' && *p <= '9') ? read_number() : 65535; }
        list(a, b);
        return;
      }
      case T_NEW: prog_len = 0; clear_vars(); return;
      case T_SAVE: cmd_save(); return;
      case T_LOAD: cmd_load(); return;
      case T_DIR: cmd_dir(); return;
      case T_AIDE: cmd_help(); return;
      case T_EXEMPLE: cmd_example(); return;
      default: error("SYNTAXE"); return;
    }
  }
  // assignment: NAME = expr, NAME$ = string, NAME(i) = expr
  char name[9];
  if (!read_name(name)) { error("SYNTAXE"); return; }
  if (accept('$')) { char *v = str_var(name); expect('='); char s[SLEN + 1]; str_expr(s); if (!err) strcpy(v, s); return; }
  if (accept('(')) { int32_t i = expr(); expect(')'); int32_t *e = arr_elem(name, i); expect('='); int32_t v = expr(); if (!err) *e = v; return; }
  int32_t *v = num_var(name);
  expect('=');
  int32_t x = expr();
  if (!err) *v = x;
}
// runs from p: the line typed (cur = -1) or the program
static void execute(void) {
  int count = 0;
  while (!err) {
    skip();
    if (*p == ':') { p++; continue; }
    if (*p == 0 || *p == T_ELSE) {
      if (cur < 0) return;                          // the end of the line typed
      cur = line_next(cur);
      if (cur >= prog_len) { running = false; return; }
      p = prog + cur + 3;
      continue;
    }
    if (cur >= 0 && !running) return;
    jumped = false;
    statement();
    if (err || (cur >= 0 && !running)) return;
    if (!jumped) { skip(); if (*p && *p != ':' && *p != T_ELSE) error("SYNTAXE"); }
    if (++count >= 2000) { count = 0; if (mc_key(KEY_ESC)) error("ARRET"); }
  }
}

// ================================================================ RUN, and the editor's commands
static void game_end(void);
static void cmd_run(int from) {
  if (!prog_len) { error("PAS DE PROGRAMME"); return; }
  if (from < 0) { error("LIGNE INEXISTANTE"); return; }
  clear_vars(); sp = 0;
  game_start();
  cur = from; p = prog + from + 3; running = true;
  execute();
  int line = cur >= 0 && cur < prog_len ? line_num(cur) : 0;
  if (!err) {                                   // the end: the picture stays until a key or a button
    game_text(9, 28, "FIN : UNE TOUCHE OU B1");
    for (int i = 0; i < 20; i++) mj_frame();
    while (mc_key_event() >= 0) {}
    for (;;) { mj_frame(); if (mc_key_event() >= 0 || (mc_pad_pressed(1) & (IN_B(1) | IN_START))) break; }
  } else err_lineno = line;
  game_end();
  running = false; cur = -1;
}
// programs in the NVRAM: "MBS1", u32 size, name (16 bytes), u32 0, then the text, a line by "\n"
static char cur_name[17];
static int item_check(const void *d, uint32_t n) { const uint8_t *b = d; return n < 28 || b[0] != 'M' || b[1] != 'B' || b[2] != 'S' || b[3] != '1'; }
static const char *item_name(int k) {
  static char s[17];
  memcpy(s, (const uint8_t *)nvs_data(k) + 8, 16); s[16] = 0;
  return s;
}
static void name_arg(char *name) {    // SAVE "NOM", LOAD "NOM", LOAD 3
  skip();
  name[0] = 0;
  if (*p >= '0' && *p <= '9') { int k = read_number(); if (k >= 1 && k <= NVS_SLOTS && nvs_size(k - 1)) snprintf(name, 17, "%s", item_name(k - 1)); return; }
  if (*p == '"') { char s[SLEN + 1]; str_expr(s); snprintf(name, 17, "%s", s); }
  else snprintf(name, 17, "%s", cur_name);
}
static uint8_t textbuf[PROG_MAX + 4096];
static void cmd_save(void) {
  char name[17];
  name_arg(name);
  if (!name[0]) { error("SAVE \"NOM\""); return; }
  int n = 28;
  for (int o = 0; o < prog_len && n < (int)sizeof textbuf - 300; o = line_next(o)) {
    n += snprintf((char *)textbuf + n, 12, "%d ", line_num(o));
    n += detokenize(prog + o + 3, (char *)textbuf + n, 290);
    textbuf[n++] = '\n';
  }
  memset(textbuf, 0, 28);
  memcpy(textbuf, "MBS1", 4);
  for (int i = 0; i < 4; i++) textbuf[4 + i] = (uint8_t)(n >> (8 * i));
  memcpy(textbuf + 8, name, strlen(name));
  int k = -1;
  for (int i = 0; i < NVS_SLOTS; i++) if (nvs_size(i) && !strcmp(item_name(i), name)) k = i;
  if (k < 0) for (int i = 0; i < NVS_SLOTS; i++) if (!nvs_size(i)) { k = i; break; }
  if (k < 0) { error("NVRAM : 8 PROGRAMMES AU PLUS (DIR)"); return; }
  uint8_t *dst = nvs_alloc(k, (uint32_t)n);
  if (!dst) { error("NVRAM PLEINE"); return; }
  memcpy(dst, textbuf, n);
  nvs_set_last(k);
  snprintf(cur_name, sizeof cur_name, "%s", name);
  char s[COLS + 1];
  snprintf(s, sizeof s, "SAUVE EN %d (%d OCTETS) - L'OSD L'ECRIT SUR LA CARTE SD", k + 1, n);
  con_line(s);
}
static bool load_text(const char *t, uint32_t n) {   // lines "10 PRINT ..." separated by "\n"
  prog_len = 0; clear_vars();
  char l[300];
  uint32_t i = 0;
  while (i < n) {
    int k = 0;
    while (i < n && t[i] != '\n') { if (k < 299) l[k++] = t[i]; i++; }
    i++; l[k] = 0;
    if (k && enter_line(l) < 0) { error("MEMOIRE PLEINE"); return false; }
  }
  return true;
}
static void cmd_load(void) {
  char name[17];
  name_arg(name);
  for (int k = 0; k < NVS_SLOTS; k++)
    if (nvs_size(k) && !strcmp(item_name(k), name)) {
      const uint8_t *d = nvs_data(k);
      if (load_text((const char *)d + 28, nvs_size(k) - 28)) {
        snprintf(cur_name, sizeof cur_name, "%s", name);
        nvs_set_last(k);
        con_line("PROGRAMME CHARGE");
      }
      return;
    }
  error("PROGRAMME INCONNU (DIR)");
}
static void cmd_dir(void) {
  char s[COLS + 1];
  for (int k = 0; k < NVS_SLOTS; k++) {
    if (nvs_size(k)) snprintf(s, sizeof s, "%d %-16s %5u OCTETS", k + 1, item_name(k), (unsigned)nvs_size(k));
    else snprintf(s, sizeof s, "%d -", k + 1);
    con_line(s);
  }
  snprintf(s, sizeof s, "LIBRE : %u OCTETS", (unsigned)nvs_free(-1));
  con_line(s);
}
static void cmd_help(void) {
  static const char *const h[] = {
    "BASIC   PRINT, IF ... THEN ... ELSE, FOR ... TO ... STEP / NEXT, WHILE / WEND, GOTO,",
    "        GOSUB / RETURN, DIM A(N), END, STOP, REM, CLS ; RND(N) ABS(N) LEN(A$) STR$(N) CHR$(N)",
    "        NOMBRES ENTIERS ; A$ = \"TEXTE\" ; AND OR NOT MOD ; = <> < > <= >=",
    "JEU     DECOR N : LE DECOR N DE LA BANQUE D'IMAGES, ET SES OBJETS (0 A OBJETS-1)",
    "        CHERCHE(\"HEROS\") : LE PREMIER OBJET DE CE LUTIN ; CHERCHE(\"PIECE\", I) : LE SUIVANT",
    "        EST(I, \"BLOB\")  NOM$(I)  CREE(\"TIR\", X, Y)  ACTIF(I)  POSX(I) POSY(I) VITX(I) VITY(I)",
    "        PLACE I,X,Y  VITESSE I,VX,VY (1/16 DE PIXEL)  GRAVITE I,1  BOITE I,X,Y,L,H",
    "        BOUGE I (OU BOUGE : TOUS)  PATROUILLE I,V  ENLEVE I  CACHE I,1  MIROIR I,1",
    "        ANIME I,1  IMAGE I,N  CAMERA I ; SOL(I) PLAFOND(I) MUR(I) DANGER(I) EAU(I) CHOC(I,J)",
    "        GAUCHE DROITE HAUT BAS BOUTON(N) APPUI(N) (MANETTE 1, FLECHES, ESPACE) ; TEMPS",
    "        ATTENDS [N] : DESSINE UNE IMAGE ; TEXTE X,Y,... ; SON \"SAUT\" ; MUSIQUE ; SILENCE",
    "        FOND R,V,B : LA COULEUR DU CIEL",
    "EDITEUR RUN, LIST [A-B], NEW, SAVE \"NOM\", LOAD \"NOM\", DIR, EXEMPLE ; 120 SEUL EFFACE",
    "        LA LIGNE 120 ; F5 RUN, F2 LIST ; ECHAP ARRETE LE PROGRAMME ; F12 : L'ATELIER" };
  for (unsigned i = 0; i < sizeof h / sizeof h[0]; i++) con_line(h[i]);
}
static const char example[] =
  "10 REM UN PETIT JEU DE PLATEFORMES EN BASIC (LA BANQUE DE DEMO)\n"
  "20 DECOR 0 : MUSIQUE : FOND 120, 200, 240\n"
  "30 H = CHERCHE(\"HEROS\") : GRAVITE H, 1 : BOITE H, 3, 2, 10, 14\n"
  "40 FOR I = 0 TO OBJETS - 1 : IF EST(I, \"BLOB\") THEN GRAVITE I, 1\n"
  "50 NEXT I\n"
  "60 S = 0 : V = 3\n"
  "100 REM LA BOUCLE DU JEU\n"
  "110 VX = 0\n"
  "120 IF GAUCHE THEN VX = -28 : MIROIR H, 1\n"
  "130 IF DROITE THEN VX = 28 : MIROIR H, 0\n"
  "140 VITESSE H, VX, VITY(H)\n"
  "150 IF SOL(H) AND APPUI(1) THEN VITESSE H, VX, -76 : SON \"SAUT\"\n"
  "160 BOUGE H : ANIME H, VX <> 0\n"
  "170 FOR I = 0 TO OBJETS - 1\n"
  "180 IF EST(I, \"BLOB\") THEN PATROUILLE I, 10 : IF CHOC(H, I) THEN GOSUB 500\n"
  "190 IF EST(I, \"PIECE\") THEN IF CHOC(H, I) THEN ENLEVE I : S = S + 10 : SON \"PIECE\"\n"
  "200 IF EST(I, \"DRAPEAU\") THEN IF CHOC(H, I) THEN GOTO 600\n"
  "210 NEXT I\n"
  "220 IF DANGER(H) OR POSY(H) > 400 THEN GOSUB 500\n"
  "230 CAMERA H : TEXTE 1, 1, \"SCORE \"; S; \"  VIES \"; V; \" \"\n"
  "240 ATTENDS : GOTO 110\n"
  "500 REM TOUCHE : UNE VIE DE MOINS\n"
  "510 SON \"AIE\" : V = V - 1 : PLACE H, 32, 160 : VITESSE H, 0, 0\n"
  "520 IF V = 0 THEN TEXTE 15, 12, \"PERDU !\" : SON \"PERDU\" : END\n"
  "530 RETURN\n"
  "600 TEXTE 15, 12, \"BRAVO !\" : SON \"BRAVO\" : ATTENDS 120 : END\n";
static void cmd_example(void) {
  load_text(example, sizeof example - 1);
  snprintf(cur_name, sizeof cur_name, "EXEMPLE");
  con_line("EXEMPLE CHARGE : RUN (OU F5) POUR JOUER, LIST POUR LE LIRE");
}

// ================================================================ the editor
static void show_error(void) {
  char s[COLS + 1];
  if (err_lineno) snprintf(s, sizeof s, "? %s EN %d", err_msg, err_lineno);
  else snprintf(s, sizeof s, "? %s", err_msg);
  con_line(s);
  if (err_lineno) list(err_lineno, err_lineno);
  err = 0; err_lineno = 0;
}
static void do_line(const char *text) {   // Enter on a line of the screen
  char l[COLS + 1];
  int n = 0;
  while (*text == ' ') text++;
  while (text[n] && n < COLS) { l[n] = text[n]; n++; }
  while (n && l[n - 1] == ' ') n--;
  l[n] = 0;
  con_newline();
  if (!n) return;
  int r = enter_line(l);
  if (r == 0) return;
  if (r < 0) { con_line("? MEMOIRE PLEINE"); return; }
  tokenize(l, imm, sizeof imm);
  err = 0; cur = -1; p = imm; running = false;
  execute();
  if (err) {
    if (in_game) game_end();
    show_error();
  } else con_line("OK");
}
static int blink;
static void draw(void) {
  for (int y = 0; y < ROWS; y++)
    for (int x = 0; x < COLS; x++) {
      char c = scr[y][x];
      if (c != ' ') ui_put(x, y, c - 32, UI_WHITE);
    }
  if ((blink++ / 16) & 1) ui_put(cx < COLS ? cx : COLS - 1, cy, UI_G_SOLID, UI_PAL_FIRST_FREE);
  ui_box(0, 29, 79, 29, UI_SUNK);
  char s[COLS + 1];
  snprintf(s, sizeof s, "MONOBASIC  %-16s %5d OCTETS  F5 RUN  F2 LIST  AIDE  EXEMPLE%s", cur_name[0] ? cur_name : "-", prog_len,
           at_present() ? "  F12 ATELIER" : "");
  ui_printn(1, 29, s, 78, UI_GREY);
}
static void editor_init(void) {
  ui_init(UI_OFF);
  PALETTE[UI_PAL_FIRST_FREE * 16 + 3] = RGB555(90, 210, 240);   // the cursor
  ui_layout = nvs_get(4) & 1;
}
static void game_end(void) {
  mt_stop(); ms_stop_all();
  for (int i = 0; i < 128; i++) mc_sprite_hide(i);
  in_game = false;
  editor_init();
}
static void type_key(int k) {
  bool sh = ui_shift();
  switch (k) {
    case KEY_LEFT: if (cx > 0) cx--; return;
    case KEY_RIGHT: if (cx < COLS - 1) cx++; return;
    case KEY_UP: if (cy > 0) cy--; return;
    case KEY_DOWN: if (cy < ROWS - 1) cy++; else scroll_up(); return;
    case KEY_HOME: cx = 0; return;
    case KEY_END: cx = COLS - 1; while (cx > 0 && scr[cy][cx - 1] == ' ') cx--; return;
    case KEY_BACKSPACE: if (cx > 0) { memmove(&scr[cy][cx - 1], &scr[cy][cx], COLS - cx); scr[cy][COLS - 1] = ' '; cx--; } return;
    case KEY_DELETE: memmove(&scr[cy][cx], &scr[cy][cx + 1], COLS - cx - 1); scr[cy][COLS - 1] = ' '; return;
    case KEY_ENTER: case KEY_KPENTER: do_line(scr[cy]); return;
    case KEY_F5: con_line("RUN"); do_line("RUN"); return;
    case KEY_F2: con_line("LIST"); do_line("LIST"); return;
    case KEY_F12: if (at_present()) at_exit(); return;
    case KEY_ESC: memset(scr[cy], ' ', COLS); cx = 0; return;
  }
  int c = kb_char(k, sh);
  if (c && cx < COLS) {
    memmove(&scr[cy][cx + 1], &scr[cy][cx], COLS - cx - 1);
    scr[cy][cx++] = (char)c;
  }
}
int main(void) {
  mc_init();
  nvs_init("MBNV", NVS_BASIC, item_check);
  editor_init();
  mt_init(mtbank_file);
  mt_reserve(MT_PSG(6), true); mt_reserve(MT_PSG(7), true);   // PSG 6-7: the sound effects (SON)
  con_clear();
  con_line("MONOBASIC - LE BASIC DE LA MONOCORE-1");
  con_line(at_present() ? "LES IMAGES, BRUITAGES ET MUSIQUES DE L'ATELIER ; F12 : RETOUR A L'ATELIER"
                        : "AVEC LES BANQUES DE DEMONSTRATION (DANS L'ATELIER : LES SIENNES)");
  con_line("AIDE : LES INSTRUCTIONS - EXEMPLE : UN JEU A LIRE ET A CHANGER - DIR : LES PROGRAMMES");
  con_line("OK");
  int last = nvs_last();
  if (last < NVS_SLOTS && nvs_size(last)) {
    const uint8_t *d = nvs_data(last);
    if (load_text((const char *)d + 28, nvs_size(last) - 28)) { snprintf(cur_name, sizeof cur_name, "%s", item_name(last)); con_line("(LE DERNIER PROGRAMME SAUVE EST CHARGE)"); }
    err = 0;
  }
  if (!ui_ok()) con_line("? CE CORE N'A PAS LE 640 X 480 (VERSION 5)");
  for (;;) {
    mc_frame();
    ui_frame_begin();
    int k;
    while ((k = ui_key()) >= 0) type_key(k);
    draw();
    ui_frame_end();
  }
}
