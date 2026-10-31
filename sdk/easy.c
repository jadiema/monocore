// MONOCORE-1 "easy": see easy.h
#include "easy.h"

#include "font_fr.h"
extern const uint8_t font_chr[], font_pal[];   // SDK font, added by build.py

// made by build.py from project.txt: all the sprite sheets one after the other, one entry per
// picture, the colours of the sheets and maps (colour palettes 0, 1, 2...) and the first map
typedef struct { uint16_t tile; uint8_t cells, palette; } picture_t;
extern const uint8_t easy_sprite_chr[];
extern const picture_t easy_pictures[];
extern const uint32_t easy_picture_count, easy_palette_count;
extern const uint8_t *const easy_palettes[];
extern const map_t *const easy_first_map;

volatile uint32_t frame_count;
static volatile uint32_t vbl;
static uint32_t joy_now[4], joy_prev[4];
static uint32_t seed = 0x2545F491u;

// sprites
static uint8_t spr_pal[64], spr_flip[64];      // spr_pal: hardware palette, or SHEET_COLOURS
static uint16_t spr_zoom[64];
#define SHEET_COLOURS 0xFF                     // the sprite takes the palette of its picture's sheet

// tunes
typedef struct { const char *start, *p; int beat, count, volume, on; } track_t;
static track_t track[8];

void irq_handler(void) {
  if (IRQ_STATUS & IRQ_VBLANK) { IRQ_STATUS = IRQ_VBLANK; vbl++; }
}

static uint16_t rgb(int r, int g, int b) {
  return (uint16_t)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}

void ink(int n, int r, int g, int b) {
  volatile uint16_t *p = PALETTE + (n & 7) * 16;
  for (int i = 0; i < 16; i++) p[i] = font_pal[2 * i] | (font_pal[2 * i + 1] << 8);
  p[1] = rgb(r, g, b);
}

void background(int r, int g, int b) { PALETTE[4095] = rgb(r, g, b); }

// sprite and map palettes 0-7 are the hardware palettes 8-15, after the text inks (8-15 also accepted)
static int colour_palette(int n) { return 8 + (n & 7); }

void load_palette(int n, const uint8_t *pal) {
  volatile uint16_t *p = PALETTE + colour_palette(n) * 16;
  for (int i = 0; i < 16; i++) p[i] = pal[2 * i] | (pal[2 * i + 1] << 8);
}

void cls(void) {
  volatile uint32_t *m = TILEMAP(0);
  for (int i = 0; i < 64 * 64; i++) m[i] = 0;
}

void init(void) {
  DISP_CTRL = 0;
  static const uint8_t inks[8][3] = {
    {255, 255, 255}, {255, 220, 60}, {80, 220, 255}, {90, 230, 90},
    {255, 80, 70}, {240, 100, 240}, {255, 150, 40}, {150, 150, 160}};
  for (int n = 0; n < 8; n++) ink(n, inks[n][0], inks[n][1], inks[n][2]);
  background(0, 0, 0);
  BG_COLOR = 4095;
  // layer 0: text
  LAYER_CTRL(0) = LC_MAPW(1) | LC_MAPH(1) | LC_PRIO(3);
  LAYER_CHR(0) = (uint32_t)font_chr;
  LAYER_SCROLLX(0) = 0; LAYER_SCROLLY(0) = 0;
  cls();
  // sprites: 64, all hidden
  for (int n = 0; n < 64; n++) { SPRITES[n].attr = 0; spr_pal[n] = SHEET_COLOURS; spr_flip[n] = 0; spr_zoom[n] = 0x100; }
  SPR_COUNT = 64;
  SPR_CHR = (uint32_t)easy_sprite_chr;
  for (uint32_t k = 0; k < easy_palette_count; k++) load_palette((int)k, easy_palettes[k]);
  for (int c = 0; c < 8; c++) { quiet(c); track[c].on = 0; }
  DISP_CTRL = DISP_ON | DISP_LAYER(0) | DISP_SPRITES;
  if (easy_first_map) load_map(*easy_first_map);
  IRQ_STATUS = 0x1F;
  IRQ_ENABLE = IRQ_VBLANK;
  irq_global_enable();
}

int random(int n) {
  seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
  return n > 0 ? (int)(seed % (uint32_t)n) : 0;
}

// ---------------------------------------------------------------- tunes
static const uint16_t scale[12] = {   // octave 4, in 1/16 Hz: C C# D D# E F F# G G# A A# B
  4186, 4435, 4699, 4978, 5274, 5588, 5920, 6272, 6645, 7040, 7459, 7902};

static int read_note(const char **pp, int *freq) {   // 1 note, 0 rest, 2 hold, -1 end
  const char *p = *pp;
  while (*p == ' ') p++;
  if (!*p) { *pp = p; return -1; }
  int r;
  if (*p == '-') { p++; r = 2; }
  else if (*p == '.') { p++; r = 0; }
  else {
    static const int8_t semitone[7] = {9, 11, 0, 2, 4, 5, 7};   // A B C D E F G
    int s = (*p >= 'A' && *p <= 'G') ? semitone[*p - 'A'] : -1;
    if (s < 0) { while (*p && *p != ' ') p++; *pp = p; return 0; }
    p++;
    if (*p == '#') { s++; p++; }         // sharp
    else if (*p == 'b') { s--; p++; }    // flat
    int oct = 4;
    if (*p >= '0' && *p <= '8') { oct = *p - '0'; p++; }
    int n = oct * 12 + s;                // semitones from C0: Cb4 is B3, B#4 is C5
    if (n < 0) n = 0;
    uint32_t f = scale[n % 12];
    *freq = (int)(n / 12 >= 4 ? f << (n / 12 - 4) : f >> (4 - n / 12));
    r = 1;
  }
  while (*p && *p != ' ') p++;
  *pp = p;
  return r;
}

static void tune_beat(int c) {
  track_t *t = &track[c];
  int f = 0;
  int r = read_note(&t->p, &f);
  if (r < 0) { t->p = t->start; r = read_note(&t->p, &f); if (r < 0) { t->on = 0; quiet(c); return; } }
  if (r == 1) {
    PSG[c].wave = PSG_SQUARE; PSG[c].duty = 128; PSG[c].freq = f;
    PSG[c].vol_l = PSG[c].vol_r = t->volume;
  } else if (r == 0) quiet(c);
}

void play(int channel, const char *tune, int frames_per_beat, int volume) {
  track_t *t = &track[channel & 7];
  t->start = t->p = tune; t->beat = frames_per_beat > 0 ? frames_per_beat : 1;
  t->count = 0; t->volume = volume; t->on = 1;
}

void stop(int channel) { track[channel & 7].on = 0; quiet(channel); }

void wait_frame(void) {
  uint32_t v = vbl;
  while (vbl == v) wfi();
  frame_count++;
  for (int j = 0; j < 4; j++) joy_prev[j] = joy_now[j];
  joy_now[0] = IN_P1; joy_now[1] = IN_P2;
  if (HW_VERSION >= 2) { joy_now[2] = IN_P3; joy_now[3] = IN_P4; }   // players 3 and 4: hardware version 2
  if ((joy_now[0] | joy_now[1] | joy_now[2] | joy_now[3]) & ~(joy_prev[0] | joy_prev[1] | joy_prev[2] | joy_prev[3]))
    seed ^= frame_count * 2654435761u;
  for (int c = 0; c < 8; c++) {
    track_t *t = &track[c];
    if (!t->on) continue;
    if (t->count == 0) tune_beat(c);
    else if (t->count == t->beat - 1) PSG[c].vol_l = PSG[c].vol_r = t->volume / 2;   // short breath
    if (++t->count >= t->beat) t->count = 0;
  }
}

// ---------------------------------------------------------------- text
void print_ink(int col, int row, const char *text, int n) {
  volatile uint32_t *m = TILEMAP(0);
  while (*text) {
    int c = font_next(&text);           // UTF-8: accented letters of font_fr.h
    if (c == '\n') { row++; col = 0; continue; }
    if (col >= 0 && col < 64 && row >= 0 && row < 64) m[row * 64 + col] = TILE(c - 32, n & 7);
    col++;
  }
}

void print(int col, int row, const char *text) { print_ink(col, row, text, 0); }

void print_num(int col, int row, int value, int digits, int n) {
  char buf[12];
  int neg = value < 0;
  uint32_t v = neg ? (uint32_t)-value : (uint32_t)value;
  if (digits > 10) digits = 10;
  if (digits < 1) digits = 1;
  for (int i = digits - 1; i >= 0; i--) { buf[i] = '0' + v % 10; v /= 10; }
  buf[digits] = 0;
  if (neg) buf[0] = '-';
  print_ink(col, row, buf, n);
}

// ---------------------------------------------------------------- joypads
int held(int player, uint32_t button) { return (joy_now[(player - 1) & 3] & button) != 0; }
int pressed(int player, uint32_t button) {
  int j = (player - 1) & 3;
  return (joy_now[j] & button) && !(joy_prev[j] & button);
}

// ---------------------------------------------------------------- sprites
void sprite(int n, int x, int y, int picture) {
  n &= 63;
  volatile sprite_t *s = &SPRITES[n];
  if (picture < 0 || (uint32_t)picture >= easy_picture_count) { s->attr = 0; return; }
  const picture_t *p = &easy_pictures[picture];
  s->x = (int16_t)x; s->y = (int16_t)y;
  s->tile = p->tile;
  s->pal = spr_pal[n] == SHEET_COLOURS ? (uint8_t)colour_palette(p->palette) : spr_pal[n];
  s->w = p->cells; s->h = p->cells;
  s->zoomx = spr_zoom[n]; s->zoomy = spr_zoom[n];
  s->attr = SA_ON | SA_PRIO(4) | spr_flip[n];
}

void hide_sprite(int n) { SPRITES[n & 63].attr = 0; }

void flip_sprite(int n, int horizontal, int vertical) {
  n &= 63;
  spr_flip[n] = (horizontal ? SA_HFLIP : 0) | (vertical ? SA_VFLIP : 0);
  if (SPRITES[n].attr & SA_ON) SPRITES[n].attr = SA_ON | SA_PRIO(4) | spr_flip[n];
}

void scale_sprite(int n, int percent) {
  n &= 63;
  int z = percent * 256 / 100;
  if (z < 16) z = 16;
  if (z > 0xFFFF) z = 0xFFFF;
  spr_zoom[n] = (uint16_t)z;
  SPRITES[n].zoomx = (uint16_t)z; SPRITES[n].zoomy = (uint16_t)z;
}

void sprite_palette(int n, int palette) {
  spr_pal[n & 63] = palette < 0 ? SHEET_COLOURS : (uint8_t)colour_palette(palette);
  if (palette >= 0) SPRITES[n & 63].pal = spr_pal[n & 63];   // -1: at the next sprite(n, ...)
}

// the box of a shown sprite, one sixteenth smaller on each side (1 pixel for a 16 x 16 sprite)
static int sprite_box(int n, int box[4]) {
  volatile sprite_t *s = &SPRITES[n & 63];
  if (!(s->attr & SA_ON)) return 0;
  int w = s->w * 8 * s->zoomx / 256, h = s->h * 8 * s->zoomy / 256;
  box[0] = s->x + w / 16; box[1] = s->y + h / 16;
  box[2] = s->x + w - w / 16; box[3] = s->y + h - h / 16;
  return 1;
}

int collide(int a, int b) {
  int p[4], q[4];
  if (!sprite_box(a, p) || !sprite_box(b, q)) return 0;
  return p[0] < q[2] && q[0] < p[2] && p[1] < q[3] && q[1] < p[3];
}

// ---------------------------------------------------------------- map
void load_map(map_t map) {
  load_palette((int)map.palette, map.pal);
  LAYER_CHR(1) = (uint32_t)map.chr;
  LAYER_CTRL(1) = LC_TILE16 | LC_MAPW(1) | LC_MAPH(1) | LC_PRIO(0);
  volatile uint32_t *m = TILEMAP(1);
  int width = (int)map.width, height = (int)map.height;
  for (int y = 0; y < 64; y++)
    for (int x = 0; x < 64; x++) {
      uint32_t t = 0;
      if (x < width && y < height) { int i = 2 * (y * width + x); t = map.map[i] | (map.map[i + 1] << 8); }
      m[y * 64 + x] = TILE(t, colour_palette((int)map.palette));
    }
  DISP_CTRL |= DISP_LAYER(1);
}

void scroll_map(int x, int y) { LAYER_SCROLLX(1) = (uint32_t)x; LAYER_SCROLLY(1) = (uint32_t)y; }

// ---------------------------------------------------------------- sound
void tone(int channel, int frequency, int volume) {
  volatile psg_chan_t *c = &PSG[channel & 7];
  c->wave = PSG_SQUARE; c->duty = 128; c->freq = (uint32_t)frequency * 16;
  c->vol_l = c->vol_r = (uint8_t)volume;
}

void noise(int channel, int volume) {
  volatile psg_chan_t *c = &PSG[channel & 7];
  c->wave = PSG_NOISE; c->freq = 6000 * 16;
  c->vol_l = c->vol_r = (uint8_t)volume;
}

void quiet(int channel) { PSG[channel & 7].vol_l = 0; PSG[channel & 7].vol_r = 0; }

void play_sample(int voice, sound_t sound, int volume) {
  volatile pcm_voice_t *v = &PCM[voice & 7];
  v->ctrl = 0;
  v->start = (uint32_t)sound.data; v->length = sound.count; v->loop = PCM_NOLOOP;
  v->step = PCM_STEP(sound.rate); v->format = PCM_S8;
  v->vol_l = v->vol_r = (uint8_t)volume;
  v->ctrl = 1;
}
