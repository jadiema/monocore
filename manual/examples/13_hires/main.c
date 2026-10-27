// Chapter 13: CARNET, a notepad in 640 x 480 interlaced (hardware version 5).
// 80 x 30 characters of 8 x 16 pixels, typed on the keyboard. It shows the two ways to cover 640 x 480:
//  - layer 2: 16 x 16 tiles, a map of 64 x 32 (1024 x 512 pixels) covers the whole picture (the frame);
//  - layers 0 and 1: 8 x 8 tiles for the text; a map of 128 x 32 covers only 1024 x 256 pixels, so
//    layer 0 holds the text rows 0-14 and layer 1 the rows 15-29, and the line scroll table of each
//    one shows two empty map rows in the half of the screen that is not its own.
#include "mc.h"
#include <stdio.h>
#include <string.h>

#define COLS 80
#define ROWS 30
#define TOP 2                          // the text area: rows 2 to 27, columns 1 to 78
#define BOTTOM 27
enum { P_TEXT, P_DIM, P_TITLE, P_FRAME = 8, P_CURSOR = 9 };
#define KEY_COMMA 0x41                 // keys without a name in monocore.h (PS/2 codes)
#define KEY_PERIOD 0x49
#define KEY_SEMICOLON 0x4C
#define KEY_MINUS 0x4E

static char page[ROWS][COLS + 1];       // the text typed
static int cx = 1, cy = TOP, azerty = 1;

// ---------------------------------------------------------------- the 8 x 16 font
// The 8 x 8 font of the SDK, each row drawn twice: character c uses the tiles 2c (top) and 2c + 1
// (bottom). Doubling the rows also keeps every horizontal stroke two lines thick: a line of a single
// pixel would show in one field out of two, and flicker.
static void make_font(void) {
  static uint8_t top[32], bot[32];
  for (int c = 0; c < 96; c++) {
    const uint8_t *g = &font_chr[c * 32];        // 8 rows of 4 bytes (4 bits a pixel)
    for (int y = 0; y < 8; y++) {
      memcpy(&top[y * 4], &g[(y / 2) * 4], 4);
      memcpy(&bot[y * 4], &g[(4 + y / 2) * 4], 4);
    }
    mc_chr_upload((2 * c) * 32, top, 32);
    mc_chr_upload((2 * c + 1) * 32, bot, 32);
  }
}
// a character at column x, row y: two tiles, in layer 0 (rows 0-14) or layer 1 (rows 15-29)
static void put(int x, int y, char ch, int pal) {
  int g = (ch < 32 || ch > 127) ? 0 : ch - 32;
  volatile uint32_t *m = mc_tilemap(y < 15 ? 0 : 1) + (y % 15) * 2 * 128 + x;
  m[0] = TILE(2 * g, pal);
  m[128] = TILE(2 * g + 1, pal);
}
static void print(int x, int y, const char *s, int pal) { for (; *s && x < COLS; s++) put(x++, y, *s, pal); }

// each text layer shows its own half; elsewhere its line scroll points at map rows 30-31, left empty
static void text_layers(void) {
  for (int l = 0; l < 2; l++) {
    mc_layer(l, MC_CHR(0), MC_TILE8 | MC_4BPP, 128, 32, 3);
    mc_layer_scroll(l, 0, l ? 16 : 0);            // layer 1: picture line 240 shows map row 0
    mc_layer_linescroll(l, true);
    volatile linescroll_t *ls = mc_linescroll(l);
    for (int y = 0; y < 480; y++) {               // y: line of the whole picture, also when interlaced
      bool mine = l ? y >= 240 : y < 240;
      ls[y].dx = 0;
      ls[y].dy = (int16_t)(mine ? 0 : (l ? 224 : 240) + (y & 15) - y);
    }
  }
}

// ---------------------------------------------------------------- the frame, in 16 x 16 tiles
// tile 0 of this layer: empty; 1: solid; 2 and 3: the top and bottom edges, two pixels thick
#define FRAME_OFF 0x4000
static void make_frame(void) {
  static uint8_t t[128];
  memset(t, 0, 128); mc_chr_upload(FRAME_OFF, t, 128);
  memset(t, 0x11, 128); mc_chr_upload(FRAME_OFF + 128, t, 128);
  memset(t, 0, 128); memset(t + 7 * 8, 0x22, 16); mc_chr_upload(FRAME_OFF + 2 * 128, t, 128);  // rows 7-8
  mc_layer(2, MC_CHR(FRAME_OFF), MC_TILE16 | MC_4BPP, 64, 32, 1);
  volatile uint32_t *m = mc_tilemap(2);
  for (int x = 0; x < 40; x++) {
    m[0 * 64 + x] = TILE(1, P_FRAME);            // title bar: the first 16 lines
    m[1 * 64 + x] = TILE(2, P_FRAME);            // a line under it
    m[28 * 64 + x] = TILE(2, P_FRAME);           // a line above the status bar
    m[29 * 64 + x] = TILE(1, P_FRAME);           // status bar: the last 16 lines
  }
}

// ---------------------------------------------------------------- keyboard
static int key_char(int k) {
  static const int16_t letters[26] = { KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
                                       KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z };
  static const int16_t digits[10] = { KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9 };
  if (azerty) {                                  // the letters that move on a French keyboard
    if (k == KEY_Q) return 'A'; if (k == KEY_A) return 'Q';
    if (k == KEY_W) return 'Z'; if (k == KEY_Z) return 'W';
    if (k == KEY_SEMICOLON) return 'M'; if (k == KEY_M) return ',';
  }
  for (int i = 0; i < 26; i++) if (letters[i] == k) return 'A' + i;
  for (int i = 0; i < 10; i++) if (digits[i] == k) return '0' + i;
  if (k == KEY_SPACE) return ' ';
  if (k == KEY_PERIOD) return azerty ? ';' : '.';
  if (k == KEY_COMMA) return azerty ? ';' : ',';
  if (k == KEY_MINUS) return azerty ? ')' : '-';
  return 0;
}
static void status(void) {
  char s[COLS + 1];
  snprintf(s, sizeof s, " LIGNE %2d  COLONNE %2d     F1 : %s     F2 : PAGE BLANCHE", cy - TOP + 1, cx, azerty ? "AZERTY" : "QWERTY");
  for (int x = 0; x < COLS; x++) put(x, ROWS - 1, ' ', P_TITLE);
  print(0, ROWS - 1, s, P_TITLE);
}
static void type(int k) {
  int c = key_char(k);
  if (c && cx < COLS - 1) { page[cy][cx] = (char)c; put(cx, cy, (char)c, P_TEXT); cx++; }
  else if (k == KEY_ENTER && cy < BOTTOM) { cy++; cx = 1; }
  else if (k == KEY_BACKSPACE && cx > 1) { cx--; page[cy][cx] = ' '; put(cx, cy, ' ', P_TEXT); }
  else if (k == KEY_LEFT && cx > 1) cx--;
  else if (k == KEY_RIGHT && cx < COLS - 2) cx++;
  else if (k == KEY_UP && cy > TOP) cy--;
  else if (k == KEY_DOWN && cy < BOTTOM) cy++;
  else if (k == KEY_F1) azerty = !azerty;
  else if (k == KEY_F2) {
    for (int y = TOP; y <= BOTTOM; y++) for (int x = 1; x < COLS - 1; x++) { page[y][x] = ' '; put(x, y, ' ', P_TEXT); }
    cx = 1; cy = TOP;
  }
  status();
}

int main(void) {
  mc_init();
  if (mc_hw_version() < 5) {                     // no 640 x 480 before version 5: say so in 320 x 240
    mc_color(P_TEXT * 16 + 1, MC_RGB(250, 210, 80));
    make_font(); text_layers(); mc_tilemap_fill(0, 0);
    mc_display(DISP_ON | DISP_LAYER(0));
    print(2, 6, "CARNET DEMANDE LE CORE VERSION 5", P_TEXT);
    for (;;) mc_frame();
  }
  mc_video_mode(640, 480);                       // a height of 384 to 512 selects the interlaced mode
  mc_color(P_TEXT * 16 + 1, MC_RGB(235, 235, 220));
  mc_color(P_DIM * 16 + 1, MC_RGB(130, 140, 170));
  mc_color(P_TITLE * 16 + 1, MC_RGB(255, 255, 255));
  mc_color(P_FRAME * 16 + 1, MC_RGB(40, 60, 140));
  mc_color(P_FRAME * 16 + 2, MC_RGB(90, 110, 190));
  mc_color(P_CURSOR * 16 + 1, MC_RGB(250, 210, 80));
  mc_color(4095, MC_RGB(16, 20, 40)); mc_background(4095);   // the colour behind everything: an entry of the palette
  make_font();
  text_layers();
  make_frame();
  mc_tilemap_fill(0, 0); mc_tilemap_fill(1, 0);
  // the cursor: a sprite, an 8 x 8 cell zoomed twice in height; sprites use picture coordinates
  static uint8_t block[32];
  memset(block, 0x11, sizeof block);
  mc_chr_upload(0x6000, block, 32);
  mc_sprite_sheet(MC_CHR(0x6000));
  mc_display(DISP_ON | DISP_LAYER(0) | DISP_LAYER(1) | DISP_LAYER(2) | DISP_SPRITES);

  print(1, 0, "CARNET   640 X 480 ENTRELACE, 80 X 30 CARACTERES", P_TITLE);
  static const char *const intro[] = {
    "LA HAUTE RESOLUTION SERT AUX EDITEURS ET AUX OUTILS : BEAUCOUP DE TEXTE, NET.",
    "LES JEUX GARDENT EN GENERAL LA BASSE RESOLUTION ET SES GROS PIXELS.",
    "",
    "TAPEZ VOTRE TEXTE ; FLECHES, ENTREE ET RETOUR ARRIERE POUR LE MODIFIER." };
  for (int y = 0; y < ROWS; y++) { memset(page[y], ' ', COLS); page[y][COLS] = 0; }
  for (int i = 0; i < 4; i++) print(1, TOP + i, intro[i], i == 3 ? P_TEXT : P_DIM);
  cy = TOP + 5;
  status();
  for (;;) {
    mc_frame();
    int e;
    while ((e = mc_key_event()) >= 0) if (e & 0x200) type(e & 0x1FF);
    // the cursor blinks: twice a second
    if ((mc_frames >> 4) & 1) mc_sprite_hide(0);
    else { mc_sprite(0, cx * 8, cy * 16, 0, 1, 1, P_CURSOR, 4); mc_sprite_zoom(0, 0x100, 0x200); }
  }
}
