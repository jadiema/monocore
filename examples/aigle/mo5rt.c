#include "mo5rt.h"

uint8_t mem[0x10000];
uint8_t A, B, DP, fN, fZ, fV, fC, fH, fI, fF, fE;
uint16_t X, Y, U, S;
uint8_t vram_forme[8000], vram_color[8000];

extern const uint8_t mo5_font[96 * 8];

// ---------------------------------------------------------------- video memory
// $A7C0 bit 0 selects what $0000-$1F3F shows: 1 = forme (pixels), 0 = couleur.
static int bank_forme(void) { return mem[0xA7C0] & 1; }
uint8_t vram_read(uint16_t a) {
  if (a >= 8000) return 0;
  return bank_forme() ? vram_forme[a] : vram_color[a];
}
void vram_write(uint16_t a, uint8_t v) {
  if (a >= 8000) return;
  uint8_t *p = bank_forme() ? &vram_forme[a] : &vram_color[a];
  if (*p != v) { *p = v; plat_byte_changed(a); }
}
void io_write(uint16_t a, uint8_t v) { (void)a; (void)v; }

static void put_byte(int off, uint8_t forme, uint8_t color) {
  if (vram_forme[off] != forme || vram_color[off] != color) {
    vram_forme[off] = forme; vram_color[off] = color; plat_byte_changed(off);
  }
}

// ---------------------------------------------------------------- text output
static int cur_col, cur_row, fg = 7, bg = 0, win_top = 0, win_bot = 24;
static int esc_state, accent;

void mo5_color(int f, int b) { if (f >= 0) fg = f & 15; if (b >= 0) bg = b & 15; }
void mo5_console(int top, int bottom) { win_top = top; win_bot = bottom; cur_col = 0; cur_row = top; }
void mo5_locate(int col, int row) { cur_col = col; cur_row = row; }

static void clear_rows(int r0, int r1) {
  for (int r = r0; r <= r1; r++)
    for (int y = 0; y < 8; y++)
      for (int c = 0; c < 40; c++) put_byte((r * 8 + y) * 40 + c, 0, (uint8_t)(fg << 4 | bg));
}
void mo5_cls(void) { clear_rows(win_top, win_bot); cur_col = 0; cur_row = win_top; }

static void scroll(void) {
  for (int r = win_top; r < win_bot; r++)
    for (int y = 0; y < 8; y++)
      for (int c = 0; c < 40; c++) {
        int d = (r * 8 + y) * 40 + c, s = d + 320;
        put_byte(d, vram_forme[s], vram_color[s]);
      }
  clear_rows(win_bot, win_bot);
}

static void newline(void) {
  cur_col = 0;
  if (++cur_row > win_bot) { cur_row = win_bot; scroll(); }
}

// accents of the MO5 (0x16 then A grave, B acute, C circumflex, H diaeresis, then the letter)
static const uint8_t accent_rows[4][2] = { { 0x20, 0x10 }, { 0x08, 0x10 }, { 0x10, 0x28 }, { 0x00, 0x24 } };
static void draw_char(uint8_t c) {
  if (c < 0x20 || c > 0x7F) c = '?';
  const uint8_t *g = &mo5_font[(c - 0x20) * 8];
  for (int y = 0; y < 8; y++) {
    uint8_t row = g[y];
    if (accent && y < 2) row |= accent_rows[accent - 1][y];
    put_byte((cur_row * 8 + y) * 40 + cur_col, row, (uint8_t)(fg << 4 | bg));
  }
  accent = 0;
  if (++cur_col >= 40) newline();
}

void mo5_putc(uint8_t c) {
  if (esc_state == 1) { cur_row = (c - 0x40) & 31; esc_state = 2; return; }     // US row col
  if (esc_state == 2) { cur_col = (c - 0x40) % 40; esc_state = 0; return; }
  if (esc_state == 4) {                                                        // accent
    esc_state = 0; accent = c == 'A' ? 1 : c == 'B' ? 2 : c == 'C' ? 3 : c == 'H' ? 4 : 0; return;
  }
  if (esc_state == 3) {                                                        // ESC colour
    esc_state = 0;
    if (c >= 0x40 && c <= 0x47) fg = c - 0x40;
    else if (c >= 0x50 && c <= 0x57) bg = c - 0x50;
    return;
  }
  switch (c) {
  case 0x1F: esc_state = 1; return;
  case 0x1B: esc_state = 3; return;
  case 0x16: esc_state = 4; return;
  case 0x0C: mo5_cls(); return;
  case 0x0D: cur_col = 0; return;
  case 0x0A: newline(); return;
  case 0x08: if (cur_col > 0) cur_col--; return;
  case 0x09: if (++cur_col >= 40) newline(); return;
  case 0x14: case 0x11: case 0x07: return;     // cursor off / on, bell
  default: if (c >= 0x20) draw_char(c);
  }
}
void mo5_puts(const char *s) { while (*s) mo5_putc((uint8_t)*s++); }

void mo5_screen(int f, int b) {
  if (f >= 0) fg = f;
  if (b >= 0) bg = b;
  for (int i = win_top * 320; i < (win_bot + 1) * 320 && i < 8000; i++)
    put_byte(i, vram_forme[i], (uint8_t)(fg << 4 | bg));
}

void mo5_init(void) {
  mem[0xA7C0] = 1;
  win_top = 0; win_bot = 24; fg = 7; bg = 0; mo5_cls();
}

// ---------------------------------------------------------------- monitor and ROM calls
void mo5_swi(uint8_t fn) {
  switch (fn) {
  case 0x02: mo5_putc(B); break;
  case 0x0C: { int k = plat_key(); B = (uint8_t)(k < 0 ? 0xFF : 57 - k); break; }   // KTST
  default: break;
  }
}
void mo5_print_string(void) {           // STROUT: X points just before the string
  uint16_t p = (uint16_t)(X + 1);
  while (mem[p]) mo5_putc(mem[p++]);
}
void mo5_print_int(void) {
  char buf[8]; int n = 0; unsigned v = D;
  do { buf[n++] = (char)('0' + v % 10); v /= 10; } while (v);
  while (n) mo5_putc((uint8_t)buf[--n]);
}


// ---------------------------------------------------------------- BASIC graphics
int mo5_gx, mo5_gy;
static void set_gcursor(int x, int y) {
  mo5_gx = x; mo5_gy = y;
  mem[0x2032] = (uint8_t)(x >> 8); mem[0x2033] = (uint8_t)x;
  mem[0x2034] = (uint8_t)(y >> 8); mem[0x2035] = (uint8_t)y;
}
// colour as in MO5 BASIC: 0-15 sets the pixel in that colour (forme colour of its 8-pixel group);
// -1 to -16 clears it and sets the background colour of the group to -(colour + 1).
static void plot(int x, int y, int color) {
  if (x < 0 || x >= MO5_W || y < 0 || y >= MO5_H) return;
  int off = y * 40 + (x >> 3); uint8_t bit = (uint8_t)(0x80 >> (x & 7));
  uint8_t f = vram_forme[off], c = vram_color[off];
  if (color >= 0) { f |= bit; c = (uint8_t)((color & 15) << 4 | (c & 15)); }
  else { f &= (uint8_t)~bit; c = (uint8_t)((c & 0xF0) | ((-color - 1) & 15)); }
  put_byte(off, f, c);
}
void mo5_pset(int x, int y, int color) {
  set_gcursor(x, y);
  plot(x, y, color);
}
void mo5_line(int x0, int y0, int x1, int y1, int color) {
  int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
  int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1, e = dx + dy;
  for (;;) {
    plot(x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * e;
    if (e2 >= dy) { e += dy; x0 += sx; }
    if (e2 <= dx) { e += dx; y0 += sy; }
  }
  set_gcursor(x1, y1);
}
void mo5_box(int x0, int y0, int x1, int y1, int color) {
  mo5_line(x0, y0, x1, y0, color); mo5_line(x1, y0, x1, y1, color);
  mo5_line(x1, y1, x0, y1, color); mo5_line(x0, y1, x0, y0, color);
}


// ---------------------------------------------------------------- BASIC helpers
int mo5_fg(void) { return fg; }
int mo5_row(void) { return cur_row; }
void mo5_border(int c) { plat_border(c); }
void mo5_delay_loop(int n) { plat_wait_ms(n); }        // an empty FOR loop: about 1 ms per turn
int mo5_key_wait(const char *keys) { return plat_menu_key(keys); }

uint32_t mo5_rnd(int x) {
  // RND with a negative argument always gives the same number for the same argument; the game uses
  // it to dress each room the same way at every visit. (Not the exact Microsoft generator.)
  uint32_t s = (uint32_t)x * 2654435761u;
  s ^= s >> 15; s *= 2246822519u; s ^= s >> 13; s *= 3266489917u; s ^= s >> 16;
  return s >> 16;
}

static int oct = 4, len = 24, tempo = 5;     // PLAY settings, kept from one PLAY to the next
void mo5_play_reset(void) { oct = 4; len = 24; tempo = 5; }   // as after RUN of a new program

static int max_note_ms;                       // 0: no limit
void mo5_play_short(const char *t, int max_ms) { max_note_ms = max_ms; mo5_play(t); max_note_ms = 0; }

void mo5_play(const char *t) {
  static const int semis[7] = { 0, 2, 4, 5, 7, 9, 11 };      // DO RE MI FA SO LA SI
  static const char *names[7] = { "DO", "RE", "MI", "FA", "SO", "LA", "SI" };
  while (*t) {
    char c = *t;
    if (c == 'O' || c == 'L' || c == 'T' || c == 'A') {
      t++; int v = 0; while (*t >= '0' && *t <= '9') v = v * 10 + (*t++ - '0');
      if (c == 'O') oct = v; else if (c == 'L') len = v; else if (c == 'T') tempo = v;
      continue;
    }
    if (c == 'P') { t++; plat_beep(0, len * tempo * 4); continue; }
    int n = -1;
    for (int k = 0; k < 7; k++) if (t[0] == names[k][0] && t[1] == names[k][1]) n = k;
    if (n < 0) { t++; continue; }
    t += 2; int semi = semis[n];
    if (*t == '#') { semi++; t++; } else if (*t == 'b') { semi--; t++; }
    // LA of octave 4 = 440 Hz
    int st = (oct - 4) * 12 + semi - 9;
    uint32_t f = 440 * 1024;
    while (st >= 12) { f *= 2; st -= 12; }
    while (st < 0) { f /= 2; st += 12; }
    static const uint16_t ratio[12] = { 1024, 1085, 1149, 1218, 1290, 1367, 1448, 1534, 1625, 1722, 1825, 1933 };
    f = f / 1024 * ratio[st] / 1024;
    int ms = len * tempo * 4;
    if (max_note_ms && ms > max_note_ms) ms = max_note_ms;
    plat_beep((int)f, ms);
  }
}
