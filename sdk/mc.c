// MONOCORE-1 "mc" low-level SDK: see mc.h
#include "mc.h"

extern const uint8_t font_chr[], font_pal[];   // SDK font, added by build.py
int vsnprintf(char *buf, size_t size, const char *fmt, __builtin_va_list ap);

volatile uint32_t mc_frames;
volatile int mc_raster_line;
sprite_t mc_spr[MC_SPRITES];
static int spr_used = MC_SPRITES;
static uint32_t pad_now[4], pad_prev[4];
static uint32_t hw_version;                         // HW_VERSION, read by mc_init
static uint16_t spin_prev[4];
static int      spin_delta[4];
static uint32_t key_now[8], key_prev[8];
static mc_handler_t vbl_fn, timer_fn[2];
static uint32_t lctrl[4];
static int map_w[4] = {64, 64, 64, 64};

// ---------------------------------------------------------------- raster
typedef struct { int16_t line; mc_handler_t fn; } raster_t;
static raster_t rast[MC_RASTER_MAX];
static volatile int rast_n, rast_i;

static void raster_run_due(int cur) {
  // runs the effects whose line is <= cur + 1, then programs the next one (one line early)
  while (rast_i < rast_n && rast[rast_i].line <= cur + 1) {
    mc_raster_line = rast[rast_i].line;
    rast[rast_i].fn();
    rast_i++;
  }
  if (rast_i < rast_n) LINE_CMP = (uint32_t)(rast[rast_i].line - 1);
}

void irq_handler(void) {
  uint32_t st = IRQ_STATUS & IRQ_ENABLE;
  if (st & IRQ_VBLANK) {
    IRQ_STATUS = IRQ_VBLANK;
    mc_frames++;
    if (vbl_fn) vbl_fn();
    rast_i = 0;
    if (rast_n) raster_run_due(-1);                // effects of line 0, then the next one
  }
  if (st & IRQ_LINE) {
    IRQ_STATUS = IRQ_LINE;
    raster_run_due((int)LINE_CMP);
  }
  if (st & IRQ_TIMER0) { IRQ_STATUS = IRQ_TIMER0; if (timer_fn[0]) timer_fn[0](); }
  if (st & IRQ_TIMER1) { IRQ_STATUS = IRQ_TIMER1; if (timer_fn[1]) timer_fn[1](); }
  if (st & IRQ_DMA)    { IRQ_STATUS = IRQ_DMA; }
}

int mc_raster_add(int line, mc_handler_t fn) {
  if (rast_n >= MC_RASTER_MAX || !fn) return -1;
  mc_irq_off();
  int j = rast_n++;
  while (j > 0 && rast[j - 1].line > line) { rast[j] = rast[j - 1]; j--; }
  rast[j].line = (int16_t)line; rast[j].fn = fn;
  IRQ_ENABLE |= IRQ_LINE;
  mc_irq_on();
  return j;
}

void mc_raster_clear(void) {
  mc_irq_off();
  rast_n = 0; rast_i = 0;
  IRQ_ENABLE &= ~IRQ_LINE;
  mc_irq_on();
}

void mc_on_vblank(mc_handler_t fn) { vbl_fn = fn; }

void mc_on_timer(int t, uint32_t hz, mc_handler_t fn) {
  t &= 1;
  uint32_t bit = t ? IRQ_TIMER1 : IRQ_TIMER0;
  if (t) T1_CTRL = 0; else T0_CTRL = 0;
  timer_fn[t] = fn;
  if (!hz || !fn) { IRQ_ENABLE &= ~bit; return; }
  if (t) { T1_RELOAD = CPU_HZ / hz; T1_CTRL = 1; } else { T0_RELOAD = CPU_HZ / hz; T0_CTRL = 1; }
  IRQ_STATUS = bit;
  IRQ_ENABLE |= bit;
}

// ---------------------------------------------------------------- system
uint64_t mc_cycles(void) {
  uint32_t hi, lo;
  do { hi = CYCLE_HI; lo = CYCLE_LO; } while (hi != CYCLE_HI);
  return ((uint64_t)hi << 32) | lo;
}
uint32_t mc_micros(void) { return (uint32_t)(mc_cycles() / 50); }

void mc_debug_putc(char c) { DEBUG_OUT = (uint8_t)c; }
void mc_debug(const char *fmt, ...) {
  char buf[160];
  __builtin_va_list ap;
  __builtin_va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  __builtin_va_end(ap);
  for (char *p = buf; *p; p++) mc_debug_putc(*p);
}

static void tunes_step(void);

void mc_init(void) {
  irq_global_disable();
  DISP_CTRL = 0;
  IRQ_ENABLE = 0;
  IRQ_STATUS = 0x1F;
  T0_CTRL = 0; T1_CTRL = 0;
  for (int i = 0; i < 4; i++) { LAYER_CTRL(i) = 0; LAYER_SCROLLX(i) = 0; LAYER_SCROLLY(i) = 0; lctrl[i] = 0; }
  dma_fill(mc_spr, 0, sizeof mc_spr);
  dma_fill(SPRITES, 0, sizeof mc_spr);
  SPR_COUNT = MC_SPRITES; spr_used = MC_SPRITES;
  PALETTE[0] = 0; BG_COLOR = 0;
  for (int c = 0; c < 8; c++) { PSG[c].vol_l = PSG[c].vol_r = 0; PCM[c].ctrl = 0; }
  MASTER_VOL = 255;
  rast_n = rast_i = 0; vbl_fn = 0; timer_fn[0] = timer_fn[1] = 0;
  for (int j = 0; j < 4; j++) { pad_now[j] = pad_prev[j] = 0; spin_delta[j] = 0; }
  for (int k = 0; k < 8; k++) key_now[k] = key_prev[k] = 0;
  hw_version = HW_VERSION;
  if (hw_version >= 2) {
    for (int j = 0; j < 4; j++) spin_prev[j] = (uint16_t)(PADDLE_P(j) >> 16);
    while (KEY_EVENT & 0x80000000u) KEY_EVENT = 0;   // events from before the start of the game
  }
  mc_srand((uint32_t)mc_cycles() | 1u);
  IRQ_ENABLE = IRQ_VBLANK;
  irq_global_enable();
}

// mouse: the counters of MOUSE_XY become a pointer kept inside the screen
static uint32_t ms_raw, ms_btn_now, ms_btn_prev;
static int ms_x, ms_y, ms_dx, ms_dy, ms_wheel, ms_num = 1, ms_den = 1, ms_rx, ms_ry;
static uint8_t ms_wraw, ms_started;
static void mouse_step(void) {
  uint32_t xy = MOUSE_XY, b = MOUSE_BTN;
  if (!ms_started) { ms_raw = xy; ms_wraw = (uint8_t)(b >> 8); ms_started = 1; }   // moves from before the start
  ms_dx = (int16_t)(uint16_t)(xy - ms_raw);
  ms_dy = (int16_t)(uint16_t)((xy >> 16) - (ms_raw >> 16));
  ms_raw = xy;
  ms_wheel = (int8_t)(uint8_t)((uint8_t)(b >> 8) - ms_wraw); ms_wraw = (uint8_t)(b >> 8);
  ms_btn_prev = ms_btn_now; ms_btn_now = b & 7u;
  ms_rx += ms_dx * ms_num; ms_ry += ms_dy * ms_num;      // num/den pixels per count, remainder kept
  int mx = ms_rx / ms_den, my = ms_ry / ms_den;
  ms_rx -= mx * ms_den; ms_ry -= my * ms_den;
  ms_x += mx; ms_y += my;
  int w = mc_screen_w(), h = mc_screen_h();
  if (ms_x < 0) ms_x = 0; if (ms_x > w - 1) ms_x = w - 1;
  if (ms_y < 0) ms_y = 0; if (ms_y > h - 1) ms_y = h - 1;
}

void mc_frame(void) {
  uint32_t f = mc_frames;
  while (mc_frames == f) wfi();
  dma_copy(SPRITES, mc_spr, (uint32_t)spr_used * sizeof(sprite_t));
  for (int j = 0; j < 4; j++) pad_prev[j] = pad_now[j];
  pad_now[0] = IN_P1; pad_now[1] = IN_P2;
  if (hw_version >= 2) {
    pad_now[2] = IN_P3; pad_now[3] = IN_P4;
    for (int j = 0; j < 4; j++) {
      uint16_t s = (uint16_t)(PADDLE_P(j) >> 16);
      spin_delta[j] = (int16_t)(s - spin_prev[j]); spin_prev[j] = s;
    }
    for (int k = 0; k < 8; k++) { key_prev[k] = key_now[k]; key_now[k] = KEY_STATE(k); }
  }
  if (hw_version >= 3) mouse_step();
  tunes_step();
}

// ---------------------------------------------------------------- video
static uint32_t video_flags;                 // DISP_INTERLACE when the mode is interlaced
void mc_video_mode(int w, int h) {
  video_flags = h > 288 ? DISP_INTERLACE : 0;
  SCREEN_W = (uint32_t)w; SCREEN_H = (uint32_t)h;
  DISP_CTRL = (DISP_CTRL & ~DISP_INTERLACE) | video_flags;
}
int  mc_screen_w(void) { return SCREEN_W ? (int)SCREEN_W : 320; }
int  mc_screen_h(void) { return SCREEN_H ? (int)SCREEN_H : video_flags ? 480 : 240; }
int  mc_interlaced(void) { return video_flags != 0; }
int  mc_field(void) { return (GPU_STATUS & STATUS_FIELD) ? 1 : 0; }
void mc_display(uint32_t flags) { DISP_CTRL = flags | video_flags; }

void mc_color(int index, uint16_t c) { PALETTE[index & 4095] = c; }
void mc_palette(int pal, const uint16_t *colors, int count) { dma_copy(PALETTE + pal * 16, colors, (uint32_t)count * 2); }
void mc_background(int index) { BG_COLOR = (uint32_t)(index & 4095); }

static uint32_t map_code(int n) { return n >= 128 ? 2 : n >= 64 ? 1 : 0; }
void mc_layer(int n, const void *chr, uint32_t flags, int mw, int mh, int prio) {
  n &= 3;
  map_w[n] = mw >= 128 ? 128 : mw >= 64 ? 64 : 32;
  lctrl[n] = (flags & (LC_TILE16 | LC_8BPP)) | LC_MAPW(map_code(mw)) | LC_MAPH(map_code(mh)) | LC_PRIO(prio & 3)
           | (lctrl[n] & LC_LINESCROLL);
  LAYER_CHR(n) = (uint32_t)chr;
  LAYER_CTRL(n) = lctrl[n];
}
void mc_layer_scroll(int n, int x, int y) { LAYER_SCROLLX(n & 3) = (uint32_t)x; LAYER_SCROLLY(n & 3) = (uint32_t)y; }
void mc_layer_linescroll(int n, bool on) {
  n &= 3;
  if (on) { dma_fill(LINESCROLL(n), 0, 0x800); lctrl[n] |= LC_LINESCROLL; }
  else lctrl[n] &= ~LC_LINESCROLL;
  LAYER_CTRL(n) = lctrl[n];
}
void mc_tile(int n, int x, int y, uint32_t e) { TILEMAP(n & 3)[y * map_w[n & 3] + x] = e; }
void mc_tilemap_fill(int n, uint32_t e) { dma_fill(TILEMAP(n & 3), e, 0x4000); }
void mc_tilemap_load(int n, const uint32_t *e, int count) { dma_copy(TILEMAP(n & 3), e, (uint32_t)count * 4); }

void mc_chr_upload(uint32_t off, const void *src, uint32_t bytes) { dma_copy(MC_CHRRAM + off, src, bytes); }

void mc_sprite_sheet(const void *chr) { SPR_CHR = (uint32_t)chr; }
void mc_sprite(int n, int x, int y, int cell, int w, int h, int pal, int prio) {
  sprite_t *s = &mc_spr[n & 511];
  s->x = (int16_t)x; s->y = (int16_t)y; s->tile = (uint16_t)cell;
  s->w = (uint8_t)w; s->h = (uint8_t)h; s->pal = (uint8_t)pal;
  if (!s->zoomx) s->zoomx = 0x100;
  if (!s->zoomy) s->zoomy = 0x100;
  s->attr = (uint8_t)((s->attr & (SA_HFLIP | SA_VFLIP | SA_8BPP)) | SA_ON | SA_PRIO(prio & 7));
}
void mc_sprite_zoom(int n, int zx, int zy) { mc_spr[n & 511].zoomx = (uint16_t)zx; mc_spr[n & 511].zoomy = (uint16_t)zy; }
void mc_sprite_flip(int n, bool h, bool v) {
  sprite_t *s = &mc_spr[n & 511];
  s->attr = (uint8_t)((s->attr & ~(SA_HFLIP | SA_VFLIP)) | (h ? SA_HFLIP : 0) | (v ? SA_VFLIP : 0));
}
void mc_sprite_8bpp(int n, bool on) {
  sprite_t *s = &mc_spr[n & 511];
  s->attr = (uint8_t)(on ? s->attr | SA_8BPP : s->attr & ~SA_8BPP);
}
void mc_sprite_hide(int n) { mc_spr[n & 511].attr = 0; }
void mc_sprites_hide_all(void) { for (int i = 0; i < MC_SPRITES; i++) mc_spr[i].attr = 0; }
void mc_sprites_used(int count) {
  spr_used = count < 0 ? 0 : count > MC_SPRITES ? MC_SPRITES : count;
  SPR_COUNT = (uint32_t)spr_used;
}

void mc_text_layer(int n, int prio) {
  mc_layer(n, font_chr, MC_TILE8 | MC_4BPP, 64, 64, prio);
  for (int i = 0; i < 16; i++) PALETTE[i] = font_pal[2 * i] | (font_pal[2 * i + 1] << 8);
  mc_text_clear(n);
  DISP_CTRL |= DISP_ON | DISP_LAYER(n & 3);
}
void mc_text(int n, int col, int row, const char *s, int pal) {
  for (; *s; s++, col++) {
    int c = (unsigned char)*s;
    if (c == '\n') { row++; col = -1; continue; }
    if (c < 32 || c > 127) c = '?';
    if (col >= 0 && col < map_w[n & 3] && row >= 0) mc_tile(n, col, row, TILE(c - 32, pal));
  }
}
void mc_text_clear(int n) { mc_tilemap_fill(n, 0); }

// ---------------------------------------------------------------- sound
void mc_pcm_play(int voice, const void *data, uint32_t samples, int format, uint32_t rate,
                 uint32_t loop, int vl, int vr) {
  volatile pcm_voice_t *v = &PCM[voice & 7];
  v->ctrl = 0;
  v->start = (uint32_t)data; v->length = samples; v->loop = loop;
  v->step = PCM_STEP(rate); v->format = (uint8_t)format;
  v->vol_l = (uint8_t)vl; v->vol_r = (uint8_t)vr;
  v->ctrl = 1;
}
void mc_pcm_stop(int voice) { PCM[voice & 7].ctrl = 0; }
uint32_t mc_pcm_pos(int voice) { return PCM[voice & 7].pos; }
void mc_psg(int ch, int wave, uint32_t hz, int duty, int vl, int vr) {
  volatile psg_chan_t *c = &PSG[ch & 7];
  c->wave = (uint8_t)wave; c->duty = (uint8_t)duty; c->freq = hz * 16;
  c->vol_l = (uint8_t)vl; c->vol_r = (uint8_t)vr;
}
void mc_psg_freq16(int ch, uint32_t f) { PSG[ch & 7].freq = f; }
void mc_psg_volume(int ch, int vl, int vr) { PSG[ch & 7].vol_l = (uint8_t)vl; PSG[ch & 7].vol_r = (uint8_t)vr; }
void mc_master_volume(int vol) { MASTER_VOL = (uint32_t)vol; }

typedef struct { const char *start, *p; int beat, count, volume, on; } track_t;
static track_t track[8];
static const uint16_t scale[12] = {4186, 4435, 4699, 4978, 5274, 5588, 5920, 6272, 6645, 7040, 7459, 7902};

static int read_note(const char **pp, uint32_t *freq) {   // 1 note, 0 rest, 2 hold, -1 end
  const char *p = *pp;
  while (*p == ' ') p++;
  if (!*p) { *pp = p; return -1; }
  int r;
  if (*p == '-') { p++; r = 2; }
  else if (*p == '.') { p++; r = 0; }
  else {
    static const int8_t semi[7] = {9, 11, 0, 2, 4, 5, 7};
    int s = (*p >= 'A' && *p <= 'G') ? semi[*p - 'A'] : -1;
    if (s < 0) { while (*p && *p != ' ') p++; *pp = p; return 0; }
    p++;
    if (*p == '#') { s++; p++; }
    int oct = 4;
    if (*p >= '0' && *p <= '8') { oct = *p - '0'; p++; }
    uint32_t f = (uint32_t)scale[s % 12] << (s / 12);
    *freq = oct >= 4 ? f << (oct - 4) : f >> (4 - oct);
    r = 1;
  }
  while (*p && *p != ' ') p++;
  *pp = p;
  return r;
}

static void tune_beat(int c) {
  track_t *t = &track[c];
  uint32_t f = 0;
  int r = read_note(&t->p, &f);
  if (r < 0) { t->p = t->start; r = read_note(&t->p, &f); if (r < 0) { t->on = 0; mc_psg_volume(c, 0, 0); return; } }
  if (r == 1) { PSG[c].wave = PSG_SQUARE; PSG[c].duty = 128; PSG[c].freq = f; mc_psg_volume(c, t->volume, t->volume); }
  else if (r == 0) mc_psg_volume(c, 0, 0);
}
static void tunes_step(void) {
  for (int c = 0; c < 8; c++) {
    track_t *t = &track[c];
    if (!t->on) continue;
    if (t->count == 0) tune_beat(c);
    else if (t->count == t->beat - 1) mc_psg_volume(c, t->volume / 2, t->volume / 2);
    if (++t->count >= t->beat) t->count = 0;
  }
}
void mc_tune(int ch, const char *notes, int fpb, int vol) {
  track_t *t = &track[ch & 7];
  t->start = t->p = notes; t->beat = fpb > 0 ? fpb : 1; t->count = 0; t->volume = vol; t->on = 1;
}
void mc_tune_stop(int ch) { track[ch & 7].on = 0; mc_psg_volume(ch, 0, 0); }

// ---------------------------------------------------------------- inputs
uint32_t mc_pad(int p) { return pad_now[(p - 1) & 3]; }
uint32_t mc_pad_pressed(int p) { int j = (p - 1) & 3; return pad_now[j] & ~pad_prev[j]; }
uint32_t mc_pad_released(int p) { int j = (p - 1) & 3; return ~pad_now[j] & pad_prev[j]; }
int mc_hw_version(void) { return (int)hw_version; }
int mc_stick_x(int p, int stick) {
  if (hw_version < 2) return 0;
  return (int8_t)(ANALOG_P((p - 1) & 3) >> (stick ? 16 : 0));
}
int mc_stick_y(int p, int stick) {
  if (hw_version < 2) return 0;
  return (int8_t)(ANALOG_P((p - 1) & 3) >> (stick ? 24 : 8));
}
int mc_paddle(int p) { return hw_version < 2 ? 0 : (int)(PADDLE_P((p - 1) & 3) & 0xFF); }
int mc_spinner(int p) { return hw_version < 2 ? 0 : (int)(uint16_t)(PADDLE_P((p - 1) & 3) >> 16); }
int mc_spinner_delta(int p) { return spin_delta[(p - 1) & 3]; }
static inline bool key_bit(const uint32_t *t, int key) { int i = KEY_INDEX(key); return (t[i >> 5] >> (i & 31)) & 1; }
bool mc_key(int key) { return key_bit(key_now, key); }
bool mc_key_pressed(int key) { return key_bit(key_now, key) && !key_bit(key_prev, key); }
bool mc_key_released(int key) { return !key_bit(key_now, key) && key_bit(key_prev, key); }
int mc_key_event(void) {
  if (hw_version < 2) return -1;
  uint32_t e = KEY_EVENT;
  if (!(e & 0x80000000u)) return -1;
  KEY_EVENT = 0;
  return (int)(e & 0x3FF);
}
bool mc_mouse_present(void) { return hw_version >= 3 && (MOUSE_BTN & 0x80000000u); }
int  mc_mouse_x(void) { return ms_x; }
int  mc_mouse_y(void) { return ms_y; }
int  mc_mouse_dx(void) { return ms_dx; }
int  mc_mouse_dy(void) { return ms_dy; }
uint32_t mc_mouse_buttons(void) { return ms_btn_now; }
uint32_t mc_mouse_pressed(void) { return ms_btn_now & ~ms_btn_prev; }
uint32_t mc_mouse_released(void) { return ~ms_btn_now & ms_btn_prev; }
int  mc_mouse_wheel(void) { return ms_wheel; }
void mc_mouse_set(int x, int y) { ms_x = x; ms_y = y; ms_rx = ms_ry = 0; }
void mc_mouse_speed(int num, int den) { ms_num = num > 0 ? num : 1; ms_den = den > 0 ? den : 1; ms_rx = ms_ry = 0; }
// MIDI: bytes of MIDI_IN gathered into messages (running status; system messages left out)
static uint8_t midi_status, midi_d[2], midi_n, midi_sysex;
int mc_midi_event(void) {
  if (hw_version < 4) return -1;
  for (;;) {
    uint32_t r = MIDI_IN;
    if (!(r & 0x80000000u)) return -1;
    MIDI_IN = 0;
    uint8_t b = (uint8_t)r;
    if (b >= 0xF8) continue;                                  // real time: clock, start, active sensing...
    if (b == 0xF0) { midi_sysex = 1; midi_status = 0; continue; }
    if (b >= 0xF1) { midi_sysex = 0; midi_status = 0; continue; }   // other system messages: dropped
    if (b & 0x80) { midi_sysex = 0; midi_status = b; midi_n = 0; continue; }
    if (midi_sysex || !midi_status) continue;
    midi_d[midi_n++] = b;
    int need = (midi_status & 0xF0) == 0xC0 || (midi_status & 0xF0) == 0xD0 ? 1 : 2;
    if (midi_n < need) continue;
    midi_n = 0;                                                // running status: the next data reuse it
    uint8_t st = midi_status;
    if ((st & 0xF0) == MIDI_NOTE_ON && need == 2 && midi_d[1] == 0) st = (uint8_t)(MIDI_NOTE_OFF | (st & 0x0F));
    return (int)((uint32_t)st << 16 | (uint32_t)midi_d[0] << 8 | (need == 2 ? midi_d[1] : 0));
  }
}
uint32_t mc_sys(void) { return IN_SYS; }
uint32_t mc_dip(void) { return DIPSW; }

// ---------------------------------------------------------------- maths
static const int32_t sin_q[257] = {0, 402, 804, 1206, 1608, 2010, 2412, 2814, 3216, 3617, 4019, 4420, 4821, 5222, 5623, 6023, 6424, 6824, 7224, 7623, 8022, 8421, 8820, 9218, 9616, 10014, 10411, 10808, 11204, 11600, 11996, 12391, 12785, 13180, 13573, 13966, 14359, 14751, 15143, 15534, 15924, 16314, 16703, 17091, 17479, 17867, 18253, 18639, 19024, 19409, 19792, 20175, 20557, 20939, 21320, 21699, 22078, 22457, 22834, 23210, 23586, 23961, 24335, 24708, 25080, 25451, 25821, 26190, 26558, 26925, 27291, 27656, 28020, 28383, 28745, 29106, 29466, 29824, 30182, 30538, 30893, 31248, 31600, 31952, 32303, 32652, 33000, 33347, 33692, 34037, 34380, 34721, 35062, 35401, 35738, 36075, 36410, 36744, 37076, 37407, 37736, 38064, 38391, 38716, 39040, 39362, 39683, 40002, 40320, 40636, 40951, 41264, 41576, 41886, 42194, 42501, 42806, 43110, 43412, 43713, 44011, 44308, 44604, 44898, 45190, 45480, 45769, 46056, 46341, 46624, 46906, 47186, 47464, 47741, 48015, 48288, 48559, 48828, 49095, 49361, 49624, 49886, 50146, 50404, 50660, 50914, 51166, 51417, 51665, 51911, 52156, 52398, 52639, 52878, 53114, 53349, 53581, 53812, 54040, 54267, 54491, 54714, 54934, 55152, 55368, 55582, 55794, 56004, 56212, 56418, 56621, 56823, 57022, 57219, 57414, 57607, 57798, 57986, 58172, 58356, 58538, 58718, 58896, 59071, 59244, 59415, 59583, 59750, 59914, 60075, 60235, 60392, 60547, 60700, 60851, 60999, 61145, 61288, 61429, 61568, 61705, 61839, 61971, 62101, 62228, 62353, 62476, 62596, 62714, 62830, 62943, 63054, 63162, 63268, 63372, 63473, 63572, 63668, 63763, 63854, 63944, 64031, 64115, 64197, 64277, 64354, 64429, 64501, 64571, 64639, 64704, 64766, 64827, 64884, 64940, 64993, 65043, 65091, 65137, 65180, 65220, 65259, 65294, 65328, 65358, 65387, 65413, 65436, 65457, 65476, 65492, 65505, 65516, 65525, 65531, 65535, 65536};
static const uint8_t atan_t[129] = {0, 1, 3, 4, 5, 6, 8, 9, 10, 11, 13, 14, 15, 16, 18, 19, 20, 22, 23, 24, 25, 27, 28, 29, 30, 31, 33, 34, 35, 36, 38, 39, 40, 41, 42, 44, 45, 46, 47, 48, 49, 51, 52, 53, 54, 55, 56, 57, 58, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 96, 97, 98, 99, 100, 101, 102, 102, 103, 104, 105, 106, 106, 107, 108, 109, 110, 110, 111, 112, 113, 113, 114, 115, 116, 116, 117, 118, 119, 119, 120, 121, 121, 122, 123, 123, 124, 125, 125, 126, 127, 127, 128};

fix16 mc_sin(int a) {
  a &= 1023;
  int q = a >> 8, i = a & 255;
  switch (q) {
  case 0: return sin_q[i];
  case 1: return sin_q[256 - i];
  case 2: return -sin_q[i];
  default: return -sin_q[256 - i];
  }
}
fix16 mc_cos(int a) { return mc_sin(a + 256); }
int mc_atan2(int y, int x) {
  if (!x && !y) return 0;
  uint32_t ax = (uint32_t)(x < 0 ? -x : x), ay = (uint32_t)(y < 0 ? -y : y);
  int a;
  if (ax >= ay) a = atan_t[(uint32_t)(((uint64_t)ay * 128) / ax)];
  else a = 256 - atan_t[(uint32_t)(((uint64_t)ax * 128) / ay)];
  if (x < 0) a = 512 - a;
  if (y < 0) a = -a;
  return a & 1023;
}
uint32_t mc_isqrt(uint32_t v) {
  uint32_t r = 0, b = 1u << 30;
  while (b > v) b >>= 2;
  while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
  return r;
}
static uint32_t seed = 0x2545F491u;
uint32_t mc_rand(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
void mc_srand(uint32_t s) { seed = s ? s : 0x2545F491u; }
