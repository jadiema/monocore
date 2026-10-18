// MONOCORE-1 test ROM: video, sound, controls and CPU checks.
// Build with rom/Makefile. Text is ASCII only (the ROM font has no accents).
#include "monocore.h"
#include "assets.h"

extern const uint8_t assets[];
#define ASSET(n) ((const void *)(assets + A_##n))

static const int16_t *SIN;
static const uint32_t *NOTE;
static inline int isin(int a) { return SIN[a & 255]; }   // * 16384
static inline int icos(int a) { return SIN[(a + 64) & 255]; }

// ------------------------------------------------------------------ globals
static volatile uint32_t vbl;
static void (*volatile line_hook)(void);
static int scr_w = 320, scr_h = 240, rot, cols, rows, mode;
static uint32_t pad, prev, pressed, frame_start;
static int cpu_pct, t;
static uint32_t rnd_state = 12345;
static uint32_t rnd(void) { rnd_state = rnd_state * 1103515245u + 12345u; return rnd_state >> 16; }

static inline uint32_t rd_instret(void) { uint32_t v; __asm__ volatile("csrr %0, minstret" : "=r"(v)); return v; }

static void debug(const char *s) { while (*s) DEBUG_OUT = (uint8_t)*s++; }

// ------------------------------------------------------------------ text (layer 0, 8x8 font, map 64x64)
static void cls(void) { dma_fill(TILEMAP(0), 0, 0x4000); }
static void print(int x, int y, const char *s, int pal) {
  volatile uint32_t *m = TILEMAP(0) + y * 64 + x;
  while (*s) {
    char c = *s++;
    if (c < 32 || c > 127) c = '?';
    *m++ = TILE(c - 32, pal) | T_PRIO;
  }
}
static void fmt_u(char *b, uint32_t v, int width) {
  char tmp[12]; int n = 0;
  do { tmp[n++] = '0' + v % 10; v /= 10; } while (v);
  while (n < width) tmp[n++] = ' ';
  while (n) *b++ = tmp[--n];
  *b = 0;
}
static void fmt_x(char *b, uint32_t v, int digits) {
  for (int i = digits - 1; i >= 0; i--) { int d = (v >> (i * 4)) & 15; *b++ = d < 10 ? '0' + d : 'A' + d - 10; }
  *b = 0;
}
static void print_u(int x, int y, uint32_t v, int w, int pal) { char b[12]; fmt_u(b, v, w); print(x, y, b, pal); }
static void print_x(int x, int y, uint32_t v, int d, int pal) { char b[12]; fmt_x(b, v, d); print(x, y, b, pal); }
static void center(int y, const char *s, int pal) { int n = 0; while (s[n]) n++; print((cols - n) / 2, y, s, pal); }

static void hud(const char *title, const char *help) {
  cls();
  print(1, 0, title, PAL_FONT_HI);
  print(1, rows - 1, help, PAL_FONT_DIM);
}
static void hud_cpu(void) {
  print(cols - 9, 0, "CPU", PAL_FONT_DIM);
  print_u(cols - 5, 0, cpu_pct, 3, cpu_pct > 90 ? PAL_FONT_HI : PAL_FONT_OK);
  print(cols - 2, 0, "%", PAL_FONT_DIM);
}

// ------------------------------------------------------------------ video helpers
static const struct { uint16_t w, h; uint8_t rot; const char *name; } modes[] = {
  {320, 240, 0, "320X240"}, {256, 224, 0, "256X224"},
  {384, 224, 0, "384X224 (CPS1)"}, {224, 288, 1, "224X288 ROT. 90"},
};
static void set_mode(int m) {
  mode = m; scr_w = modes[m].w; scr_h = modes[m].h; rot = modes[m].rot;
  SCREEN_W = scr_w; SCREEN_H = scr_h;
  cols = scr_w / 8; rows = scr_h / 8;
}
static void display(uint32_t flags) { DISP_CTRL = flags | DISP_ON | DISP_ROT(rot); }
static void layer(int n, uint32_t ctrl, const void *chr) {
  LAYER_CTRL(n) = ctrl; LAYER_CHR(n) = (uint32_t)chr; LAYER_SCROLLX(n) = 0; LAYER_SCROLLY(n) = 0;
}
static void load_map(int n, const uint16_t *src, int sw, int sh, int mw, int pal) {
  volatile uint32_t *m = TILEMAP(n);
  for (int y = 0; y < sh; y++)
    for (int x = 0; x < sw; x++) m[y * mw + x] = TILE(src[y * sw + x], pal);
}
static void spr_set(int i, int x, int y, int tile, int pal, int attr, int w, int h, int zx, int zy) {
  volatile sprite_t *s = &SPRITES[i];
  s->x = x; s->y = y; s->tile = tile; s->pal = pal; s->attr = attr | SA_ON;
  s->w = w; s->h = h; s->zoomx = zx; s->zoomy = zy;
}

// sky gradient: palette entry 31*16 rewritten every 4 lines by the LINE interrupt
#define SKY_INDEX (31 * 16)
static int sky_on;
static uint16_t sky_color(int y) {
  int r = 8 + 120 * y / scr_h, g = 8 + 40 * y / scr_h, b = 48 + 100 * y / scr_h;
  return RGB555(r, g, b);
}
static void sky_line(void) {
  int y = VCOUNT;
  PALETTE[SKY_INDEX] = sky_color(y);
  LINE_CMP = y + 4 < scr_h ? y + 4 : 0x7FFF;
}
static void sky(int on) {
  sky_on = on;
  BG_COLOR = SKY_INDEX;
  PALETTE[SKY_INDEX] = on ? sky_color(0) : RGB555(16, 16, 56);
  line_hook = on ? sky_line : 0;
  LINE_CMP = on ? 0 : 0x7FFF;
}

// ------------------------------------------------------------------ sound helpers
static void pcm_play(int v, const void *addr, uint32_t len, uint32_t loop, uint32_t step, int fmt, int vl, int vr) {
  volatile pcm_voice_t *p = &PCM[v];
  p->ctrl = 0;
  p->start = (uint32_t)addr; p->length = len; p->loop = loop; p->step = step;
  p->format = fmt; p->vol_l = vl; p->vol_r = vr;
  p->ctrl = 1;
}
static void psg_set(int c, int wave, uint32_t freq, int duty, int vl, int vr) {
  volatile psg_chan_t *p = &PSG[c];
  p->wave = wave; p->freq = freq; p->duty = duty; p->vol_l = vl; p->vol_r = vr;
}
static void psg_vol(int c, int vl, int vr) { PSG[c].vol_l = vl; PSG[c].vol_r = vr; }

// ------------------------------------------------------------------ music (sequenced from the 240 Hz timer interrupt)
static volatile int music_on;
static int mtick, mstep, lead_vol, bass_vol, arp_vol;
static const uint8_t bass_notes[4] = {45, 41, 48, 43};
static const uint8_t lead_notes[64] = {
  69, 0, 72, 0, 76, 0, 72, 0, 74, 0, 72, 0, 69, 0, 0, 0,
  65, 0, 69, 0, 72, 0, 69, 0, 72, 0, 74, 0, 72, 0, 0, 0,
  67, 0, 72, 0, 76, 0, 79, 0, 76, 0, 72, 0, 74, 0, 0, 0,
  71, 0, 74, 0, 79, 0, 74, 0, 71, 0, 67, 0, 71, 0, 74, 0};
static const uint8_t arp_notes[4][3] = {{57, 60, 64}, {53, 57, 60}, {55, 60, 64}, {55, 59, 62}};

static void music_tick(void) {
  if (!music_on) return;
  lead_vol = lead_vol * 240 >> 8; bass_vol = bass_vol * 252 >> 8; arp_vol = arp_vol * 220 >> 8;
  psg_vol(0, bass_vol, bass_vol);
  psg_vol(1, lead_vol, lead_vol * 3 >> 2);
  psg_vol(2, arp_vol >> 1, arp_vol);
  if (++mtick < 30) return;     // 240 Hz / 30 = 8 steps per second (120 bpm sixteenths)
  mtick = 0;
  int s = mstep; mstep = (mstep + 1) & 63;
  int bar = s >> 4, st = s & 15;
  if (st == 0 || st == 6 || st == 10) { PSG[0].freq = NOTE[bass_notes[bar] - (st == 6 ? 0 : 0)]; bass_vol = 190; }
  if (lead_notes[s]) { PSG[1].freq = NOTE[lead_notes[s]]; lead_vol = 150; }
  PSG[2].freq = NOTE[arp_notes[bar][st % 3] + 12]; arp_vol = 90;
  if (st == 0 || st == 8 || (st == 11 && (bar & 1))) pcm_play(0, ASSET(SMP_KICK), KICK_LEN, PCM_NOLOOP, PCM_STEP(24000), PCM_S8, 230, 230);
  if (st == 4 || st == 12) pcm_play(1, ASSET(SMP_SNARE), SNARE_LEN, PCM_NOLOOP, PCM_STEP(48000), PCM_S16, 150, 170);
  if (!(st & 1)) pcm_play(2, ASSET(SMP_HAT), HAT_LEN, PCM_NOLOOP, PCM_STEP(48000), PCM_S8, 60, 90);
}
static void music(int on) {
  if (on && !music_on) {
    mtick = 29; mstep = 0; lead_vol = bass_vol = arp_vol = 0;
    psg_set(0, PSG_TRIANGLE, NOTE[45], 128, 0, 0);
    psg_set(1, PSG_SQUARE, NOTE[69], 64, 0, 0);
    psg_set(2, PSG_SAW, NOTE[57], 128, 0, 0);
  }
  music_on = on;
  if (!on) for (int c = 0; c < 3; c++) psg_vol(c, 0, 0);
}

// ------------------------------------------------------------------ interrupts
void irq_handler(void) {
  uint32_t st = IRQ_STATUS & IRQ_ENABLE;
  if (st & IRQ_LINE) { IRQ_STATUS = IRQ_LINE; if (line_hook) line_hook(); }
  if (st & IRQ_TIMER0) { IRQ_STATUS = IRQ_TIMER0; music_tick(); }
  if (st & IRQ_VBLANK) {
    IRQ_STATUS = IRQ_VBLANK; vbl++;
    if (sky_on) { PALETTE[SKY_INDEX] = sky_color(0); LINE_CMP = 0; }
  }
}

static void frame_wait(void) {
  uint32_t used = CYCLE_LO - frame_start;
  cpu_pct = used / (CPU_HZ / 6000);
  uint32_t v = vbl;
  while (vbl == v) wfi();
  frame_start = CYCLE_LO;
  prev = pad; pad = IN_P1 | IN_P2; pressed = pad & ~prev;
  t++;
}

// ------------------------------------------------------------------ screens
enum { S_MENU, S_CARD, S_PLANES, S_SPRITES, S_SOUND, S_CONTROLS, S_CPU, S_COUNT };
static int screen;
static void enter(int s);

static void reset_video(void) {
  line_hook = 0; sky_on = 0; LINE_CMP = 0x7FFF;
  BG_COLOR = 0;
  dma_fill(SPRITES, 0, 512 * 16);
  dma_fill((void *)TILEMAP_BASE, 0, 0x10000);
  dma_fill((void *)LINETAB_BASE, 0, 0x2000);
  for (int n = 0; n < 4; n++) layer(n, 0, 0);
  SPR_COUNT = 0;
  layer(0, LC_MAPW(1) | LC_MAPH(1) | LC_PRIO(3), ASSET(FONT));
}

static void setup_stars(int n) {
  layer(n, LC_MAPW(1) | LC_MAPH(0) | LC_PRIO(0), ASSET(STARS_CHR));
  load_map(n, ASSET(STARS_MAP), 64, 32, 64, PAL_STARS);
}
static void setup_wide16(int n, const void *chr, const void *map, int pal, int prio) {
  layer(n, LC_TILE16 | LC_MAPW(0) | LC_MAPH(0) | LC_PRIO(prio), chr);
  load_map(n, map, 32, 16, 32, pal);
  LAYER_SCROLLY(n) = 256 - scr_h;
}

// ---- menu
static const char *menu_items[] = {
  "MIRE ET RESOLUTIONS", "PLANS, PARALLAXE, RASTER", "SPRITES, ZOOM, PRIORITES",
  "SON : PSG, PCM, ADPCM", "CONTROLES ET DIP", "CPU ET DMA"};
static int menu_sel;
static void menu_draw(void) {
  for (int i = 0; i < S_COUNT - 1; i++) {
    int y = rows / 2 - 1 + i * 2;
    print(cols / 2 - 14, y, i == menu_sel ? ">" : " ", PAL_FONT_HI);
    print(cols / 2 - 12, y, menu_items[i], i == menu_sel ? PAL_FONT_HI : PAL_FONT);
  }
}
static void menu_enter(void) {
  setup_stars(3);
  sky(1);
  center(rows / 2 - 4, "ROM DE TEST - CONSOLE VIRTUELLE", PAL_FONT_DIM);
  center(rows - 2, "HAUT/BAS : CHOISIR   BOUTON 1 : ENTRER", PAL_FONT_DIM);
  menu_draw();
  display(DISP_LAYER(0) | DISP_LAYER(3) | DISP_SPRITES);
}
static void menu_update(void) {
  LAYER_SCROLLX(3) = t / 2;
  int z = 0x100 + (isin(t * 2) * 0x20 >> 14);
  int w = 128 * z >> 8, h = 32 * z >> 8;
  spr_set(0, (scr_w - w) / 2, rows / 2 * 8 - 72 - h / 2 + 16, SPR_LOGO, 128, SA_8BPP | SA_PRIO(4), 16, 4, z, z);
  SPR_COUNT = 1;
  int n = S_COUNT - 1;
  if (pressed & IN_UP) { menu_sel = (menu_sel + n - 1) % n; menu_draw(); }
  if (pressed & IN_DOWN) { menu_sel = (menu_sel + 1) % n; menu_draw(); }
  if (pressed & (IN_B(1) | IN_START)) enter(menu_sel + 1);
}

// ---- test card
static void card_enter(void) {
  hud("MIRE ET RESOLUTIONS", "B1 MODE SUIVANT  B2 RETOUR");
  // layer 1: border, grid of the 256-colour test palette (palettes 32..47), using the solid tiles
  layer(1, LC_MAPW(1) | LC_MAPH(1) | LC_PRIO(0), ASSET(FONT));
  volatile uint32_t *m = TILEMAP(1);
  for (int x = 0; x < cols; x++) {
    m[x] = TILE(96 + 15, 32 + ((x & 1) ? 0 : 1));
    m[(rows - 1) * 64 + x] = TILE(96 + 15, 32 + ((x & 1) ? 0 : 1));
  }
  for (int y = 0; y < rows; y++) {
    m[y * 64] = TILE(96 + 15, 32 + ((y & 1) ? 0 : 1));
    m[y * 64 + cols - 1] = TILE(96 + 15, 32 + ((y & 1) ? 0 : 1));
  }
  int cw = cols >= 36 ? 2 : 1;
  int gx = (cols - 16 * cw) / 2, gy = 3;
  for (int p = 0; p < 16; p++)
    for (int k = 0; k < 16; k++)
      for (int i = 0; i < cw; i++) m[(gy + p) * 64 + gx + k * cw + i] = TILE(96 + k, 32 + p);
  // layer 2: the 8bpp logo cells used as an 8bpp tile layer
  layer(2, LC_8BPP | LC_MAPW(1) | LC_MAPH(1) | LC_PRIO(1), ASSET(SPR_CHR));
  volatile uint32_t *m2 = TILEMAP(2);
  int lx = (cols - 16) / 2, ly = gy + 17;
  for (int y = 0; y < 4; y++)
    for (int x = 0; x < 16; x++) m2[(ly + y) * 64 + lx + x] = TILE(SPR_LOGO + y * 16 + x, 128);
  print(2, rows - 3, "MODE", PAL_FONT_DIM);
  print(7, rows - 3, modes[mode].name, PAL_FONT);
  print(2, rows - 2, "PLAN 8 BPP + 4096 COULEURS", PAL_FONT_DIM);
  print(cols / 2 - 1, rows / 2, "+", PAL_FONT_HI);
  BG_COLOR = 32 * 16 + 1;
  PALETTE[0] = RGB555(0, 0, 0);
  display(DISP_LAYER(0) | DISP_LAYER(1) | DISP_LAYER(2));
}
static void card_update(void) {
  if (pressed & IN_B(1)) { set_mode((mode + 1) % 4); enter(S_CARD); }
}

// ---- planes, parallax, raster
static int speed = 2, wave_on = 1, scroll_x;
static void planes_text(void) {
  print(1, 2, "VITESSE (G/D)  :", PAL_FONT_DIM); print_u(18, 2, speed + 4, 2, PAL_FONT); print(20, 2, "/8", PAL_FONT_DIM);
  print(1, 3, "VAGUE (B1)     :", PAL_FONT_DIM); print(18, 3, wave_on ? "OUI" : "NON", PAL_FONT);
  print(1, 4, "RASTER CIEL(B3):", PAL_FONT_DIM); print(18, 4, sky_on ? "OUI" : "NON", PAL_FONT);
}
static void planes_enter(void) {
  hud("PLANS, PARALLAXE, RASTER", "G/D VITESSE B1 VAGUE B3 CIEL B2 RETOUR");
  setup_stars(3);
  setup_wide16(2, ASSET(MOUNT_CHR), ASSET(MOUNT_MAP), PAL_MOUNT, 1);
  setup_wide16(1, ASSET(GROUND_CHR), ASSET(GROUND_MAP), PAL_GROUND, 2);
  LAYER_CTRL(1) |= LC_LINESCROLL;
  if (wave_on) LAYER_CTRL(2) |= LC_LINESCROLL;
  sky(1);
  planes_text();
  display(DISP_LAYER(0) | DISP_LAYER(1) | DISP_LAYER(2) | DISP_LAYER(3));
}
static void planes_update(void) {
  hud_cpu();
  if (pressed & IN_LEFT && speed > -4) speed--;
  if (pressed & IN_RIGHT && speed < 4) speed++;
  if (pressed & IN_B(1)) { wave_on = !wave_on; if (wave_on) LAYER_CTRL(2) |= LC_LINESCROLL; else LAYER_CTRL(2) &= ~LC_LINESCROLL; }
  if (pressed & IN_B(3)) sky(!sky_on);
  if (pressed & (IN_LEFT | IN_RIGHT | IN_B(1) | IN_B(3))) planes_text();
  scroll_x += speed + 4;                       // 8.3 fixed point: (speed+4)/8 px per frame for the ground
  int g = scroll_x;
  LAYER_SCROLLX(3) = g >> 4;
  LAYER_SCROLLX(2) = g >> 3;
  LAYER_SCROLLX(1) = g >> 1;
  volatile linescroll_t *ls1 = LINESCROLL(1), *ls2 = LINESCROLL(2);
  int base = g >> 1, off = 256 - scr_h;
  for (int y = 0; y < scr_h; y++) {
    int iy = y + off;
    ls1[y].dx = iy >= 195 ? base * (iy - 150) / 45 - base : 0;   // pseudo-3D floor
    if (wave_on) ls2[y].dx = isin(y * 3 + t * 4) * 5 >> 14;      // heat-haze wave
  }
}

// ---- sprites
#define MAX_BALLS 509
static int nballs = 64;
static int bx[MAX_BALLS], by[MAX_BALLS], bvx[MAX_BALLS], bvy[MAX_BALLS];
static int ship_x, ship_y, ship_flip;
static void ball_init(int i) {
  bx[i] = (rnd() % (scr_w - 16)) << 8; by[i] = (rnd() % (scr_h / 2)) << 8;
  bvx[i] = (int)(rnd() % 512) - 256; bvy[i] = -(int)(rnd() % 512);
}
static void sprites_text(void) {
  print(1, 2, "SPRITES (B1 +16, B3 -16):", PAL_FONT_DIM); print_u(27, 2, nballs + 2, 3, PAL_FONT);
}
static void sprites_enter(void) {
  hud("SPRITES, ZOOM, PRIORITES", "JOY VAISSEAU  B4 MIROIR V  B2 RETOUR");
  setup_stars(3);
  setup_wide16(2, ASSET(MOUNT_CHR), ASSET(MOUNT_MAP), PAL_MOUNT, 1);
  sky(0);
  for (int i = 0; i < MAX_BALLS; i++) ball_init(i);
  ship_x = scr_w / 2 - 16; ship_y = scr_h - 80;
  sprites_text();
  print(1, 3, "MAX PAR LIGNE           :", PAL_FONT_DIM);
  print(1, 4, "DEBORDEMENT (>64/LIGNE) :", PAL_FONT_DIM);
  display(DISP_LAYER(0) | DISP_LAYER(2) | DISP_LAYER(3) | DISP_SPRITES);
}
static void sprites_update(void) {
  hud_cpu();
  if (pressed & IN_B(1)) { nballs += 16; if (nballs > MAX_BALLS) nballs = MAX_BALLS; sprites_text(); }
  if (pressed & IN_B(3)) { nballs -= 16; if (nballs < 0) nballs = 0; sprites_text(); }
  if (pressed & IN_B(4)) ship_flip ^= SA_VFLIP;
  int hf = 0;
  if (pad & IN_LEFT) { ship_x -= 2; hf = SA_HFLIP; }
  if (pad & IN_RIGHT) ship_x += 2;
  if (pad & IN_UP) ship_y -= 2;
  if (pad & IN_DOWN) ship_y += 2;
  if (ship_x < 0) ship_x = 0; if (ship_x > scr_w - 32) ship_x = scr_w - 32;
  if (ship_y < 16) ship_y = 16; if (ship_y > scr_h - 40) ship_y = scr_h - 40;
  spr_set(0, ship_x, ship_y, SPR_SHIP, PAL_SHIP, SA_PRIO(5) | hf | ship_flip, 4, 4, 0x100, 0x100);
  int zx = 0x100 + (isin(t * 3) * 0xA0 >> 14), zy = 0x100 + (icos(t * 2) * 0x60 >> 14);
  int w = 128 * zx >> 8, h = 32 * zy >> 8;
  spr_set(1, (scr_w - w) / 2, 64 - h / 2, SPR_LOGO, 128, SA_8BPP | SA_PRIO(4), 16, 4, zx, zy);
  int floor = (scr_h - 16) << 8, right = (scr_w - 16) << 8;
  for (int i = 0; i < nballs; i++) {
    bvy[i] += 20;
    bx[i] += bvx[i]; by[i] += bvy[i];
    if (bx[i] < 0) { bx[i] = 0; bvx[i] = -bvx[i]; }
    if (bx[i] > right) { bx[i] = right; bvx[i] = -bvx[i]; }
    if (by[i] > floor) { by[i] = floor; bvy[i] = -(bvy[i] * 15 >> 4); if (bvy[i] > -400) bvy[i] = -900 - (int)(rnd() % 600); }
    // even balls behind the mountains (prio 1), odd balls in front (prio 5)
    spr_set(2 + i, bx[i] >> 8, by[i] >> 8, SPR_BALL, PAL_BALL0 + (i & 7), SA_PRIO((i & 1) ? 5 : 1), 2, 2, 0x100, 0x100);
  }
  SPR_COUNT = 2 + nballs;
  print_u(27, 3, SPR_PEAK, 3, PAL_FONT);
  print(27, 4, (GPU_STATUS & 2) ? "OUI" : "NON", (GPU_STATUS & 2) ? PAL_FONT_HI : PAL_FONT_OK);
}

// ---- sound
static const char *snd_items[] = {
  "PSG CARRE (MAINTENIR B1)", "PSG TRIANGLE (MAINTENIR)", "PSG DENT DE SCIE (MAINT.)",
  "PSG BRUIT (MAINTENIR)", "PCM 8 BITS : GROSSE CAISSE", "PCM 16 BITS : CAISSE CLAIRE",
  "ADPCM OKI : CARILLON", "PCM EN BOUCLE (MAINTENIR)", "STEREO : BALAYAGE G/D", "MUSIQUE (TIMER 240 HZ)"};
#define SND_N 10
static int snd_sel, snd_note = 69, pan_on;
static const char *note_names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static void sound_draw(void) {
  for (int i = 0; i < SND_N; i++) {
    print(1, 3 + i, i == snd_sel ? ">" : " ", PAL_FONT_HI);
    int on = (i == 8 && pan_on) || (i == 9 && music_on);
    print(3, 3 + i, snd_items[i], i == snd_sel ? PAL_FONT_HI : (on ? PAL_FONT_OK : PAL_FONT));
  }
  print(1, 14, "NOTE (G/D) :     ", PAL_FONT_DIM);
  print(14, 14, note_names[snd_note % 12], PAL_FONT);
  print_u(16, 14, snd_note / 12 - 1, 1, PAL_FONT);
  print_u(19, 14, NOTE[snd_note] / 16, 5, PAL_FONT); print(25, 14, "HZ", PAL_FONT_DIM);
}
static void sound_enter(void) {
  hud("SON : PSG, PCM, ADPCM", "H/B CHOIX B1 JOUER G/D NOTE B2 RETOUR");
  setup_stars(3);
  sky(0);
  sound_draw();
  print(1, 16, "VOIX PCM  ETAT  POSITION", PAL_FONT_DIM);
  display(DISP_LAYER(0) | DISP_LAYER(3));
}
static void sound_update(void) {
  hud_cpu();
  LAYER_SCROLLX(3) = t;
  int redraw = 0;
  if (pressed & IN_UP) { snd_sel = (snd_sel + SND_N - 1) % SND_N; redraw = 1; }
  if (pressed & IN_DOWN) { snd_sel = (snd_sel + 1) % SND_N; redraw = 1; }
  if ((pressed & IN_LEFT) && snd_note > 36) { snd_note--; redraw = 1; }
  if ((pressed & IN_RIGHT) && snd_note < 96) { snd_note++; redraw = 1; }
  int held = pad & IN_B(1), hit = pressed & IN_B(1);
  // held PSG notes on channel 4
  if (snd_sel <= 3 && held) {
    uint32_t f = NOTE[snd_note];
    if (snd_sel == 3) f *= 16;
    psg_set(4, snd_sel, f, 128, 140, 140);
  } else psg_vol(4, 0, 0);
  if (hit && snd_sel == 4) pcm_play(4, ASSET(SMP_KICK), KICK_LEN, PCM_NOLOOP, PCM_STEP(24000), PCM_S8, 255, 255);
  if (hit && snd_sel == 5) pcm_play(5, ASSET(SMP_SNARE), SNARE_LEN, PCM_NOLOOP, PCM_STEP(48000), PCM_S16, 220, 220);
  if (hit && snd_sel == 6) {
    // chime recorded at 16 kHz for A5 (note 81): pitch follows the selected note
    uint32_t step = (NOTE[snd_note] * 4096 / NOTE[81]) * 16 * 21845 >> 16;   // 16 kHz base = 1/3 of 48 kHz
    pcm_play(6, ASSET(SMP_CHIME), SMP_CHIME_NIBBLES, PCM_NOLOOP, step, PCM_ADPCM, 220, 220);
  }
  if (snd_sel == 7 && hit) {
    uint32_t step = (NOTE[snd_note] * 4096 / NOTE[57]) * 16;   // loop recorded at 220 Hz (A3)
    pcm_play(7, ASSET(SMP_PAD), PAD_LEN, 0, step, PCM_S16, 120, 120);
  }
  if (!(pad & IN_B(1)) && (prev & IN_B(1))) PCM[7].ctrl = 0;
  if (hit && snd_sel == 8) { pan_on = !pan_on; redraw = 1; if (!pan_on) psg_vol(7, 0, 0); }
  if (hit && snd_sel == 9) { music(!music_on); redraw = 1; }
  if (pan_on) {
    int p = isin(t * 2) * 120 >> 14;
    psg_set(7, PSG_SQUARE, NOTE[64], 96, 125 - p, 125 + p);
  }
  if (redraw) sound_draw();
  for (int v = 0; v < 8; v++) {
    int on = PCM[v].ctrl & 1;
    print_u(3, 17 + v, v, 2, PAL_FONT_DIM);
    print(11, 17 + v, on ? "ON " : "-- ", on ? PAL_FONT_OK : PAL_FONT_DIM);
    print_u(16, 17 + v, on ? PCM[v].pos : 0, 6, PAL_FONT);
  }
}

// ---- controls
static int hold;
static void pad_draw(int x, uint32_t in, const char *name) {
  #define L(cond) ((cond) ? PAL_FONT_HI : PAL_FONT_DIM)
  print(x, 3, name, PAL_FONT);
  print(x + 4, 5, "HAUT", L(in & IN_UP));
  print(x, 6, "GAUCHE", L(in & IN_LEFT)); print(x + 7, 6, "DROITE", L(in & IN_RIGHT));
  print(x + 4, 7, "BAS", L(in & IN_DOWN));
  for (int b = 1; b <= 6; b++) {
    char s[4] = {'B', (char)('0' + b), 0, 0};
    print(x + (b - 1) % 3 * 4, 9 + (b - 1) / 3, s, L(in & IN_B(b)));
  }
  print(x, 12, "START", L(in & IN_START)); print(x + 7, 12, "PIECE", L(in & IN_COIN));
  print(x, 14, "0X", PAL_FONT_DIM); print_x(x + 2, 14, in, 3, PAL_FONT);
}
static void controls_enter(void) {
  hud("CONTROLES ET DIP", "MAINTENIR START J1 POUR QUITTER");
  setup_stars(3);
  display(DISP_LAYER(0) | DISP_LAYER(3));
  hold = 0;
}
static void controls_update(void) {
  hud_cpu();
  uint32_t p1 = IN_P1, p2 = IN_P2, sys = IN_SYS;
  int half = cols / 2;
  pad_draw(2, p1, "JOUEUR 1");
  pad_draw(half + 1, p2, "JOUEUR 2");
  print(2, 17, "SYSTEME :", PAL_FONT_DIM);
  print(12, 17, "SERVICE", L(sys & 1)); print(20, 17, "TEST", L(sys & 2)); print(25, 17, "TILT", L(sys & 4));
  print(2, 19, "DIP :", PAL_FONT_DIM); print(8, 19, "0X", PAL_FONT_DIM); print_x(10, 19, DIPSW, 8, PAL_FONT);
  for (int i = 0; i < 16 && 2 + i < cols; i++) print(2 + i, 20, (DIPSW >> (15 - i)) & 1 ? "1" : "0", PAL_FONT);
  if (p1 & IN_START) hold++; else hold = 0;
  char bar[21];
  int n = hold * 20 / 60;
  for (int i = 0; i < 20; i++) bar[i] = i < n ? '#' : '.';
  bar[20] = 0;
  print(2, rows - 3, bar, PAL_FONT_HI);
  if (hold >= 60) enter(S_MENU);
}

// ---- CPU and DMA benchmark
static uint32_t bench_buf[16384];   // 64 KB in WRAM
static void cpu_enter(void) {
  hud("CPU ET DMA", "B1 : RELANCER   B2 : RETOUR");
  setup_stars(3);
  display(DISP_LAYER(0) | DISP_LAYER(3));
  const uint8_t *rom = assets;   // (never dereference address 0 in C: the compiler treats it as NULL)
  // 1. bitwise CRC32 over 16 KB of ROM
  uint32_t c0 = CYCLE_LO, i0 = rd_instret();
  uint32_t crc = 0xFFFFFFFF;
  for (int i = 0; i < 16384; i++) {
    crc ^= rom[i];
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
  }
  crc = ~crc;
  uint32_t c1 = CYCLE_LO, i1 = rd_instret();
  // 2. CPU copy 64 KB ROM -> WRAM
  const uint32_t *src = (const uint32_t *)assets;
  for (int i = 0; i < 16384; i++) bench_buf[i] = src[i];
  uint32_t c2 = CYCLE_LO;
  // 3. DMA copy 64 KB ROM -> WRAM
  dma_copy(bench_buf, assets, 65536);
  uint32_t c3 = CYCLE_LO;
  // 4. multiply/divide mix
  volatile uint32_t acc = 1;
  for (uint32_t i = 1; i < 2000; i++) acc = acc * 2654435761u / (i | 1) + i;
  uint32_t c4 = CYCLE_LO, i4 = rd_instret();
  uint32_t dc = c1 - c0, di = i1 - i0;
  print(1, 3, "CRC32 16 KO   CYCLES :", PAL_FONT_DIM); print_u(24, 3, dc, 9, PAL_FONT);
  print(1, 4, "  INSTRUCTIONS       :", PAL_FONT_DIM); print_u(24, 4, di, 9, PAL_FONT);
  print(1, 5, "  MIPS A 50 MHZ      :", PAL_FONT_DIM); print_u(24, 5, di / (dc / (CPU_HZ / 1000000) + 1), 9, PAL_FONT);
  print(1, 6, "  RESULTAT 0X", PAL_FONT_DIM); print_x(14, 6, crc, 8, PAL_FONT);
  print(1, 8, "COPIE CPU 64 KO      :", PAL_FONT_DIM); print_u(24, 8, c2 - c1, 9, PAL_FONT);
  print(1, 9, "COPIE DMA 64 KO      :", PAL_FONT_DIM); print_u(24, 9, c3 - c2, 9, PAL_FONT);
  print(1, 10, "MUL/DIV x2000        :", PAL_FONT_DIM); print_u(24, 10, c4 - c3, 9, PAL_FONT);
  print(1, 12, "BUDGET PAR IMAGE     :", PAL_FONT_DIM); print_u(24, 12, CPU_HZ / 60, 9, PAL_FONT);
  print(1, 14, "INSTRUCTIONS TOTALES :", PAL_FONT_DIM); print_u(24, 14, i4 - i0, 9, PAL_FONT);
  print(1, 16, "UN 68000 A 10 MHZ FAIT ENVIRON", PAL_FONT_DIM);
  print(1, 17, "1,5 MIPS : IL RESTE DE LA MARGE", PAL_FONT_DIM);
  print(1, 18, "POUR LE CODE TRADUIT.", PAL_FONT_DIM);
}
static void cpu_update(void) {
  hud_cpu();
  LAYER_SCROLLX(3) = t;
  if (pressed & IN_B(1)) enter(S_CPU);
}

// ------------------------------------------------------------------ dispatch
static void enter(int s) {
  screen = s;
  for (int v = 3; v < PCM_VOICES; v++) PCM[v].ctrl = 0;
  for (int c = 3; c < 8; c++) psg_vol(c, 0, 0);
  pan_on = 0;
  reset_video();
  switch (s) {
    case S_MENU: menu_enter(); break;
    case S_CARD: card_enter(); break;
    case S_PLANES: planes_enter(); break;
    case S_SPRITES: sprites_enter(); break;
    case S_SOUND: sound_enter(); break;
    case S_CONTROLS: controls_enter(); break;
    case S_CPU: cpu_enter(); break;
  }
}

int main(void) {
  SIN = ASSET(SINE);
  NOTE = ASSET(NOTES);
  debug("MONOCORE-1 : ROM de test, demarrage\n");
  MASTER_VOL = 255;
  dma_copy(PALETTE, ASSET(PALS), A_PALS_SIZE);
  dma_copy(PALETTE + 32 * 16, ASSET(PAL_CARD), A_PAL_CARD_SIZE);
  dma_copy(PALETTE + 128 * 16, ASSET(PAL_LOGO), A_PAL_LOGO_SIZE);
  SPR_CHR = (uint32_t)ASSET(SPR_CHR);
  set_mode(0);
  IRQ_STATUS = 0x1F;
  IRQ_ENABLE = IRQ_VBLANK | IRQ_LINE | IRQ_TIMER0;
  T0_RELOAD = CPU_HZ / 240;
  T0_CTRL = 1;
  irq_global_enable();
  enter(S_MENU);
  debug("pret\n");
  frame_start = CYCLE_LO;
  for (;;) {
    frame_wait();
    if (screen != S_MENU && screen != S_CONTROLS && (pressed & IN_B(2))) {
      if (screen == S_CARD && mode != 0) set_mode(0);
      enter(S_MENU);
      continue;
    }
    switch (screen) {
      case S_MENU: menu_update(); break;
      case S_CARD: card_update(); break;
      case S_PLANES: planes_update(); break;
      case S_SPRITES: sprites_update(); break;
      case S_SOUND: sound_update(); break;
      case S_CONTROLS: controls_update(); break;
      case S_CPU: cpu_update(); break;
    }
  }
}
