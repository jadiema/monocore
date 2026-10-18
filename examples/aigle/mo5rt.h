// Runtime of the L'Aigle d'Or port: the parts of the MO5 (video memory, text output, BASIC
// graphics) that the game relies on, rewritten in C. The platform layer (MONOCORE-1 or PC test)
// only has to show the bitmap and supply the keyboard.
#pragma once
#include <stdint.h>
#include "cpu6809.h"

#define MO5_W 320
#define MO5_H 200
extern uint8_t vram_forme[8000], vram_color[8000];   // 40 bytes per line, 200 lines

// platform layer
void plat_byte_changed(unsigned offset);            // forme or couleur byte at offset changed
int  plat_key(void);                                // MO5 key number 0-57, or -1 when none
uint32_t plat_rand(void);
void plat_beep(int freq_hz, int ms);                // PLAY: one note (0 Hz = rest)

// text screen (PUTC of the monitor, PRINT/LOCATE/COLOR/CONSOLE of BASIC)
void mo5_init(void);
void mo5_putc(uint8_t c);
void mo5_puts(const char *s);
void mo5_locate(int col, int row);
void mo5_color(int fg, int bg);
void mo5_console(int top, int bottom);
void mo5_cls(void);
void mo5_screen(int fg, int bg);                   // SCREEN fg,bg: recolours the console window

// BASIC graphics: PSET/LINE/BOX; colour -1 = background colour, -5 etc. = keep (see mo5rt.c)
void mo5_pset(int x, int y, int color);
void mo5_line(int x0, int y0, int x1, int y1, int color);
void mo5_box(int x0, int y0, int x1, int y1, int color);
extern int mo5_gx, mo5_gy;                          // graphic cursor (also in $2032/$2034)


// additions used by the translated BASIC
int   mo5_fg(void);
int   mo5_row(void);
void  mo5_border(int c);
void  mo5_play(const char *tune);          // PLAY of MO5 BASIC (blocking, as on the MO5)
uint32_t mo5_rnd(int x);                   // RND(x) x 65536 (0-65535); x < 0: same value for the same x
void  mo5_delay_loop(int n);               // FOR X=1 TO n:NEXT
int   mo5_key_wait(const char *keys);      // INPUT$(1) restricted to these keys

// platform layer (continued)
void plat_tick(void);                      // one pass of the game loop (paces the game)
void plat_wait_ms(int ms);
void plat_border(int c);
int  plat_menu_key(const char *keys);      // waits for a choice among keys
