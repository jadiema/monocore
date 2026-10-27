// MONOCORE-1 "ui480": the 640 x 480 interface of the editors (MONOPIXEL; later the BASIC...).
//
// 640 x 480 interlaced (hardware version 5), 80 x 30 characters of 8 x 16 (the MONOTRACKER font),
// bevelled grey panels in the manner of the Amiga programs, a mouse pointer, clickable zones and the
// keyboard (AZERTY or QWERTY, auto-repeat). It takes:
//   - layers 0 and 1 (the text: rows 0-14 and 15-29, a map of 8 x 8 tiles covering 256 lines only)
//     and layer 2 (the panels, tiles of 16 x 16 shifted 8 pixels up, so that a panel leaves half a
//     character above and below its text); layer 3 is free (behind everything: drawings, maps...);
//   - palettes 0-31, sprite 0 (the pointer), 24 KB of CHR RAM from the offset given to ui_init;
//   - SPR_CHR: the start of the CHR RAM (sprite cell = CHR RAM offset / 32).
// Priorities: text 3, panels 1, so a sprite of priority 3 to 5 shows over the panels and under the
// text; layer 3 should have priority 0. A UI_FRAME panel is open: what is behind it shows inside.
//
// A frame of the program:
//     ui_frame_begin();                // clears the screen being drawn, starts a new list of zones
//     int k; while ((k = ui_key()) >= 0) ...;  const ui_zone_t *z = ui_zone_at(mx, my) ...
//     ui_box(...); ui_print(...); ui_button(...); ...
//     ui_frame_end();                  // sends what changed to the tilemaps, keeps the zones
//     mc_frame();
// In project.txt: library = mc, modules = ui480. Documentation: UI480.md.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define UI_COLS 80
#define UI_ROWS 30
#define UI_CHR_SIZE 0x6000u          // CHR RAM taken by ui480

// text colours (palettes): on the dark background, then on the grey panels
enum { UI_WHITE, UI_GREY, UI_CYAN, UI_GREEN, UI_ORANGE, UI_YELLOW, UI_RED, UI_BLUE,
       UI_INK, UI_INK_BLUE, UI_INK_RED, UI_INK_DIM, UI_INK_WHITE, UI_INK_GREEN };
#define UI_PAL_FIRST_FREE 32         // the program's own palettes start here

// glyphs: 0-95 = the characters 32-127; then those of ui480; 128-255 for the program
enum { UI_G_SOLID = 96,              // a block of colour 3 of its palette (swatches, bars)
       UI_G_UP, UI_G_DOWN, UI_G_LEFT, UI_G_RIGHT, UI_G_ARROW, UI_G_CHECK, UI_G_DOT,
       UI_G_FRAME = 112 };           // 112-127: a frame of colour 1, bits 1 top, 2 bottom, 4 left, 8 right
#define UI_G_USER 128
void ui_glyph_define(int g, const char *rows);   // 16 rows of 8 characters: '.' 0, '1'-'9' colours
void ui_glyph_tiles(int g, const uint8_t *top, const uint8_t *bottom);   // two 8 x 8 tiles, 4 bpp

// panels: x0 even, x1 odd, in characters; rows y0 to y1 of text (the bevel is in the half rows
// around them, so two panels one above the other need a free row between them)
enum { UI_RAISED, UI_SUNK, UI_PRESSED, UI_FRAME, UI_HOLE,
       UI_BACK };                    // UI_BACK: the background colour, opaque, no bevel (hides layer 3)
void ui_box(int x0, int y0, int x1, int y1, int style);

void ui_init(uint32_t chr_offset);   // 640 x 480 interlaced, layers 0-2, palettes, pointer
int  ui_ok(void);                    // 0 if the core is too old (no 640 x 480, hardware version < 5)

// text
void ui_cls(void);
void ui_put(int x, int y, int glyph, int pal);
void ui_print(int x, int y, const char *s, int pal);
void ui_printn(int x, int y, const char *s, int n, int pal);   // exactly n characters, padded
void ui_printc(int x0, int x1, int y, const char *s, int pal); // centred between x0 and x1
void ui_printf(int x, int y, int pal, const char *fmt, ...);
void ui_frame(int x0, int y0, int x1, int y1, int pal);        // a rectangle of frame glyphs

// zones (in characters): the zones of a frame answer the mouse during the next one
typedef struct { uint8_t x0, y0, x1, y1, id; int16_t arg; } ui_zone_t;
void ui_zone(int x0, int y0, int x1, int y1, int id, int arg);
const ui_zone_t *ui_zone_at(int px, int py);   // under the pixel (px, py), the last drawn on top

// widgets: a button (w characters, even; the zone gets id, arg) and a counter with arrows: zones
// id with arg * 4 + 0 (up), + 1 (down), + 2 (the value)
void ui_button(int x, int y, int w, const char *label, bool on, int id, int arg);
void ui_spin(int x, int y, const char *label, int wl, const char *value, int wv, int id, int arg);

// a frame
void ui_frame_begin(void);
void ui_frame_end(void);

// mouse pointer (sprite 0), drawn by ui_frame_end
void ui_pointer_shape(int shape);    // 0 arrow, 1 cross (precise drawing)

// keyboard
extern int ui_layout;                // 0 AZERTY (default), 1 QWERTY
int  ui_key(void);                   // next key pressed (KEY_xxx of monocore.h), with repeat, or -1
int  ui_char(int key);               // character printed on the key ('A'-'Z', '0'-'9', ' ', '-', '.'), 0 if none
int  ui_hex(int key);                // 0-15, or -1
bool ui_shift(void);
bool ui_ctrl(void);
bool ui_alt(void);

// messages of the status line
void ui_message(const char *s);
const char *ui_message_text(void);   // the message being shown, or 0
bool ui_confirm(int key, const char *what);   // true the second time within 3 s; else says so
