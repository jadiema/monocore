// MONOCORE-1 - écran d'accueil façon « intro Amiga » (ROM boot.rom, SDK bas niveau mc)
//   barres copper (défilement ligne par ligne du plan 1), logo chromé (un sprite 256 x 40),
//   champ d'étoiles sur 3 plans devant un ciel dégradé (effets raster), texte défilant sinusoïdal
//   et musique sur 3 voies.
#include "mc.h"

extern const uint8_t sheet_chr[], sheet_pal[];
extern const uint8_t copper_chr[], copper_pal[], copper_map[];

static const int8_t sinus[256] = { 0, 3, 6, 9, 12, 16, 19, 22, 25, 28, 31, 34, 37, 40, 43, 46, 49, 51, 54, 57, 60, 63, 65, 68, 71, 73, 76, 78, 81, 83, 85, 88, 90, 92, 94, 96, 98, 100, 102, 104, 106, 107, 109, 111, 112, 113, 115, 116, 117, 118, 120, 121, 122, 122, 123, 124, 125, 125, 126, 126, 126, 127, 127, 127, 127, 127, 127, 127, 126, 126, 126, 125, 125, 124, 123, 122, 122, 121, 120, 118, 117, 116, 115, 113, 112, 111, 109, 107, 106, 104, 102, 100, 98, 96, 94, 92, 90, 88, 85, 83, 81, 78, 76, 73, 71, 68, 65, 63, 60, 57, 54, 51, 49, 46, 43, 40, 37, 34, 31, 28, 25, 22, 19, 16, 12, 9, 6, 3, 0, -3, -6, -9, -12, -16, -19, -22, -25, -28, -31, -34, -37, -40, -43, -46, -49, -51, -54, -57, -60, -63, -65, -68, -71, -73, -76, -78, -81, -83, -85, -88, -90, -92, -94, -96, -98, -100, -102, -104, -106, -107, -109, -111, -112, -113, -115, -116, -117, -118, -120, -121, -122, -122, -123, -124, -125, -125, -126, -126, -126, -127, -127, -127, -127, -127, -127, -127, -126, -126, -126, -125, -125, -124, -123, -122, -122, -121, -120, -118, -117, -116, -115, -113, -112, -111, -109, -107, -106, -104, -102, -100, -98, -96, -94, -92, -90, -88, -85, -83, -81, -78, -76, -73, -71, -68, -65, -63, -60, -57, -54, -51, -49, -46, -43, -40, -37, -34, -31, -28, -25, -22, -19, -16, -12, -9, -6, -3 };
#define SIN(a) sinus[(a) & 255]
#define COS(a) sinus[((a) + 64) & 255]

// ---------------------------------------------------------------- palettes
// 0-7 : encres du texte (easy) ; 16-22 : barres copper ; 23 : filets ; 24 : logo ;
// 25-27 : étoiles ; 28-35 : lettres du défilant
#define PAL_BAR    16
#define NB_BARS    7
#define PAL_RULE   23
#define PAL_LOGO   24
#define PAL_STAR   25
#define PAL_SCROLL 28

static void set_color(int pal, int i, int r, int g, int b) { PALETTE[pal * 16 + i] = RGB555(r, g, b); }

static void make_palettes(void) {
  static const uint8_t hue[NB_BARS + 1][3] = {
    {255, 40, 40}, {255, 140, 20}, {255, 230, 40}, {40, 220, 60},
    {30, 200, 255}, {60, 90, 255}, {220, 60, 255}, {255, 200, 80}};
  for (int b = 0; b <= NB_BARS; b++)
    for (int i = 1; i <= 15; i++) {
      int p = SIN((i * 128) / 16);                 // 0 -> 127 -> 0 du haut au bas de la barre
      int w = p > 100 ? (p - 100) * 9 : 0;         // reflet blanc au centre
      int r = hue[b][0] * p / 127 + w, g = hue[b][1] * p / 127 + w, bl = hue[b][2] * p / 127 + w;
      set_color(PAL_BAR + b, i, r > 255 ? 255 : r, g > 255 ? 255 : g, bl > 255 ? 255 : bl);
    }
  static const uint8_t chrome[15][3] = {           // ciel bleu, horizon sombre, sol doré
    {0, 0, 0}, {255, 255, 255}, {214, 240, 255}, {168, 216, 255}, {112, 176, 240}, {56, 120, 208},
    {24, 60, 144}, {90, 42, 16}, {154, 72, 24}, {208, 112, 40}, {240, 152, 56}, {255, 192, 96},
    {255, 232, 160}, {10, 10, 30}, {0, 0, 0}};
  for (int i = 1; i < 15; i++) set_color(PAL_LOGO, i, chrome[i][0], chrome[i][1], chrome[i][2]);
  for (int s = 0; s < 3; s++) {                    // étoiles : plus claires quand elles vont vite
    int v = 90 + s * 80;
    set_color(PAL_STAR + s, 1, v, v, v + 40 > 255 ? 255 : v + 40);
    set_color(PAL_STAR + s, 2, 255, 255, 255);
  }
  static const uint8_t rainbow[8][3] = {
    {255, 70, 70}, {255, 160, 40}, {255, 240, 60}, {90, 240, 90},
    {60, 220, 255}, {100, 120, 255}, {230, 90, 255}, {255, 120, 200}};
  for (int k = 0; k < 8; k++) {
    set_color(PAL_SCROLL + k, 1, rainbow[k][0], rainbow[k][1], rainbow[k][2]);
    set_color(PAL_SCROLL + k, 2, 20, 20, 40);
  }
}

// ---------------------------------------------------------------- barres copper
// Le plan 1 contient une « texture » : la rangée de tuiles q (16 lignes) est la barre q, dans la
// palette PAL_BAR + q ; la rangée 63 est vide. Le défilement ligne par ligne choisit, pour chaque
// ligne de l'écran, la ligne de texture à afficher : dy = ligne voulue - ligne de l'écran.
#define TEX_EMPTY (63 * 16)
static int16_t target[240];

static void setup_copper(void) {
  LAYER_CHR(1) = (uint32_t)copper_chr;
  LAYER_CTRL(1) = LC_TILE16 | LC_MAPW(1) | LC_MAPH(1) | LC_LINESCROLL | LC_PRIO(0);
  LAYER_SCROLLX(1) = 0; LAYER_SCROLLY(1) = 0;
  volatile uint32_t *m = TILEMAP(1);
  for (int q = 0; q < 64; q++)
    for (int x = 0; x < 64; x++)
      m[q * 64 + x] = q <= NB_BARS ? TILE(1, PAL_BAR + q) : TILE(0, 0);
  for (int y = 0; y < 240; y++) { LINESCROLL(1)[y].dx = 0; LINESCROLL(1)[y].dy = TEX_EMPTY - y; }
}

static void draw_bar(int q, int center, int top, int bottom) {
  for (int r = 0; r < 15; r++) {
    int y = center - 7 + r;
    if (y >= top && y < bottom) target[y] = (int16_t)(q * 16 + r);
  }
}

static void update_copper(int t) {
  for (int y = 0; y < 240; y++) target[y] = TEX_EMPTY;
  int order[NB_BARS], z[NB_BARS];
  for (int i = 0; i < NB_BARS; i++) {             // les barres du fond d'abord
    z[i] = COS(t * 2 + i * 18);
    int j = i;
    while (j > 0 && z[order[j - 1]] > z[i]) { order[j] = order[j - 1]; j--; }
    order[j] = i;
  }
  for (int k = 0; k < NB_BARS; k++) {
    int i = order[k];
    draw_bar(i, 45 + SIN(t * 2 + i * 18) * 36 / 127, 0, 90);
  }
  for (int r = 0; r < 8; r++) {                    // deux filets dorés fixes
    target[90 + r] = (int16_t)(NB_BARS * 16 + 3 + r);
    target[190 + r] = (int16_t)(NB_BARS * 16 + 3 + r);
  }
  for (int y = 0; y < 240; y++) LINESCROLL(1)[y].dy = (int16_t)(target[y] - y);
}

// ---------------------------------------------------------------- sprites
#define SPR_LOGO   0
#define SPR_STAR   1
#define NB_STARS   40
#define SPR_SCROLL 41
#define NB_SCROLL  23
#define CELL_DOT   160
#define CELL_CROSS 161
#define CELL_FONT  192                              // rangée de cellules 6, 32 cellules par rangée

static void put_sprite(int n, int x, int y, int cell, int w, int h, int pal, int zoom) {
  mc_sprite(n, x, y, cell, w, h, pal, 4);
  mc_sprite_zoom(n, zoom, zoom);
}

typedef struct { int x, y, speed; } star_t;
static star_t stars[NB_STARS];

static void update_stars(void) {
  for (int i = 0; i < NB_STARS; i++) {
    star_t *s = &stars[i];
    s->x -= s->speed;
    if (s->x < -8) { s->x = 320; s->y = 98 + (int)(mc_rand() % 86); }
    put_sprite(SPR_STAR + i, s->x, s->y, s->speed == 3 ? CELL_CROSS : CELL_DOT, 1, 1,
               PAL_STAR + s->speed - 1, 0x100);
  }
}

static const char message[] =
  "BIENVENUE SUR LA MONOCORE-1 !   UNE CONSOLE 32 BITS NEE DANS UN FPGA CYCLONE V ...   "
  "PROCESSEUR RISC-V A 50 MHZ, 4 PLANS, 512 SPRITES, 16 VOIES SONORES ...   "
  "CODE : JULIEN ET JEREMIE ...   SALUTATIONS A TOUTE LA SCENE MISTER !   "
  "BIENTOT : LA CARTOUCHE W25Q64 ...                    ";

static void update_scroller(int t) {
  int len = sizeof(message) - 1;
  int pos = (t * 2) % (len * 16 + 320);
  int slot = 0;
  for (int i = 0; i < len && slot < NB_SCROLL; i++) {
    int x = 320 - pos + i * 16;
    if (x <= -16) continue;
    if (x >= 320) break;
    int c = message[i] - 32;
    int y = 212 + SIN(x + t * 3) * 10 / 127;
    if (c > 0) put_sprite(SPR_SCROLL + slot++, x, y, CELL_FONT + (c / 16) * 32 + c % 16, 1, 1,
                          PAL_SCROLL + ((i + t / 6) & 7), 0x200);
  }
  while (slot < NB_SCROLL) mc_sprite_hide(SPR_SCROLL + slot++);
}

// ---------------------------------------------------------------- musique (3 voies, 32 temps)
static const char *lead =
  "E5 - - D5 C5 - B4 C5 A4 - - - C5 - D5 - E5 - G5 - E5 - D5 C5 D5 - - - B4 - . . ";
static const char *bass =
  "A2 - A3 - A2 - A3 - F2 - F3 - F2 - F3 - C3 - C4 - C3 - C4 - G2 - G3 - G2 - G3 - ";
static const char *arp =
  "A4 C5 E5 A5 E5 C5 A4 C5 F4 A4 C5 F5 C5 A4 F4 A4 C5 E5 G5 C6 G5 E5 C5 E5 G4 B4 D5 G5 D5 B4 G4 B4 ";

// ---------------------------------------------------------------- ciel dégradé (raster)
// Une interruption toutes les 2 lignes dans la bande centrale change la couleur de fond.
static void sky(void) {
  int y = mc_raster_line;
  if (y >= 190) { mc_color(4095, 0); return; }
  int k = (y - 98) * 255 / 92;                     // 0 en haut, 255 en bas de la bande
  mc_color(4095, MC_RGB(k / 6, 0, 40 + k / 3));
}

static void text_center(int row, const char *s, int pal) {
  int n = 0;
  while (s[n]) n++;
  mc_text(0, (40 - n) / 2, row, s, pal);
}

static void inks(void) {                         // palettes 0-7 : police du SDK recolorée
  static const uint8_t c[8][3] = {
    {255, 255, 255}, {255, 220, 60}, {80, 220, 255}, {90, 230, 90},
    {255, 80, 70}, {240, 100, 240}, {255, 150, 40}, {150, 150, 170}};
  for (int p = 0; p < 8; p++) {
    for (int i = 0; i < 16; i++) PALETTE[p * 16 + i] = PALETTE[i];
    set_color(p, 1, c[p][0], c[p][1], c[p][2]);
  }
}

// ---------------------------------------------------------------- programme
int main(void) {
  mc_init();
  mc_text_layer(0, 3);                             // plan 0 : texte, devant tout
  inks();
  mc_color(4095, 0);
  mc_background(4095);
  make_palettes();
  mc_sprite_sheet(sheet_chr);
  mc_sprites_used(64);
  setup_copper();
  mc_display(DISP_ON | DISP_LAYER(0) | DISP_LAYER(1) | DISP_SPRITES);
  for (int y = 98; y <= 190; y += 2) mc_raster_add(y, sky);
  for (int i = 0; i < NB_STARS; i++) {
    stars[i].x = (int)(mc_rand() % 320); stars[i].y = 98 + (int)(mc_rand() % 86); stars[i].speed = 1 + i % 3;
  }
  text_center(13, "MONOCORE-1  *  CONSOLE FPGA 32 BITS", 1);
  text_center(15, "RISC-V 50 MHZ - CYCLONE V - MISTER", 2);
  text_center(17, "4 PLANS  -  512 SPRITES", 7);
  mc_tune(0, lead, 7, 70);
  mc_tune(1, bass, 7, 80);
  mc_tune(2, arp, 7, 30);

  int message_timer = 0;
  for (int t = 0;; t++) {
    mc_frame();
    update_copper(t);
    int logo_y = t < 64 ? 26 - (64 - t) * (64 - t) * 66 / 4096 : 26 + SIN(t * 3) * 4 / 127;
    put_sprite(SPR_LOGO, 32, logo_y, 0, 32, 5, PAL_LOGO, 0x100);
    update_stars();
    update_scroller(t);
    if (mc_pad_pressed(1) & IN_START) message_timer = 180;
    if (message_timer > 0) {
      message_timer--;
      text_center(21, " INSEREZ UNE CARTOUCHE ", 4);
    } else if ((t / 30) % 2 == 0) text_center(21, "   APPUYEZ SUR START   ", 6);
    else                          text_center(21, "                       ", 0);
  }
}
