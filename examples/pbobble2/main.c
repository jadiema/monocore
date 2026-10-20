// Puzzle Bobble 2 / Bust-A-Move Again on MONOCORE-1: the console layer.
// The translated 68000 program (build/tr/pb2_code.c) runs on neort.c; this file shows the Neo Geo
// picture with the MONOCORE hardware:
//   fix layer          -> layer 0 (8x8 tiles in ROM), written as the game writes it
//   palettes           -> the 4096 colours, converted to RGB555 as the game writes them
//   wide sprite chains -> layers 1-3 (16x16 tiles in ROM): the backgrounds, which the Neo Geo builds
//                         out of columns of sprites
//   other columns      -> one 16x16 MONOCORE sprite per tile, from a tile cache in CHR RAM (the
//                         cell number of a sprite only reaches 2 MB of graphics, the game has 4.2)
//   LSPC video RAM     -> kept in CHR RAM (68 KB), next to the tile cache
#include "mc.h"
#include "neort.h"

extern const uint8_t p1_file[], spr_file[], spr_cells_file[], spr_map_file[], fix_file[];
extern const uint32_t p1_size, spr_map_size;
extern const uint8_t sounds_file[], sounds_tab_file[];

#define VRAM_BYTES  0x11000u                       // 0x8800 words
#define CACHE_SLOTS ((MC_CHRRAM_SIZE - VRAM_BYTES) / 128)
#define CACHE_BASE  (CHRRAM_BASE + VRAM_BYTES)
#define SPR_PRIO    2                              // in front of layers 1-3, behind the fix layer

static m68k_t cpu;
static uint32_t vframe;

// --- colours and fix layer (called by neort.c while the program runs) ---
static inline uint16_t rgb555(uint32_t c) {
  uint32_t r = ((c >> 7) & 0x1E) | ((c >> 14) & 1), g = ((c >> 3) & 0x1E) | ((c >> 13) & 1),
           b = ((c << 1) & 0x1E) | ((c >> 12) & 1);
  return (uint16_t)((r << 10) | (g << 5) | b);
}
void neo_platform_color(uint32_t index, uint32_t color) { PALETTE[index & 0xFFF] = rgb555(color); }
void neo_platform_bank(void) {
  const uint16_t *p = neo_pal[neo.pal_bank];
  for (int i = 0; i < 0x1000; i++) PALETTE[i] = rgb555(p[i]);
}
void neo_platform_fix(uint32_t x, uint32_t y, uint32_t e) {
  TILEMAP(0)[y * 64 + x] = MC_TILE(e & 0xFFF, e >> 12, 0);
}
uint32_t neo_platform_line(void) {                 // MONOCORE line -> Neo Geo line from the vblank
  uint32_t total = (uint32_t)mc_screen_h() + 24, v = VCOUNT;
  uint32_t from_vbl = (v + total - (uint32_t)mc_screen_h()) % total;
  return from_vbl * 264 / total;
}

// --- sprite tile cache in CHR RAM ---
static uint16_t slot_tile[CACHE_SLOTS], slot_used[CACHE_SLOTS];
static int16_t slot_next[CACHE_SLOTS], bucket[1024];
static uint32_t hand;

static void cache_init(void) {
  for (unsigned i = 0; i < 1024; i++) bucket[i] = -1;
  for (unsigned i = 0; i < CACHE_SLOTS; i++) { slot_tile[i] = 0; slot_used[i] = 0xFFFF; slot_next[i] = -1; }
}

static int cache_get(uint32_t t) {                 // slot holding unique tile t, loaded if needed
  int16_t *b = &bucket[t & 1023];
  for (int s = *b; s >= 0; s = slot_next[s])
    if (slot_tile[s] == t) { slot_used[s] = (uint16_t)vframe; return s; }
  for (unsigned n = 0; n < CACHE_SLOTS; n++) {      // clock: first slot not used by this frame
    unsigned s = hand; hand = (hand + 1) % CACHE_SLOTS;
    if (slot_used[s] == (uint16_t)vframe) continue;
    if (slot_used[s] != 0xFFFF) {                  // unlink the tile it held
      int16_t *p = &bucket[slot_tile[s] & 1023];
      while (*p != (int16_t)s) p = &slot_next[*p];
      *p = slot_next[s];
    }
    slot_tile[s] = (uint16_t)t; slot_used[s] = (uint16_t)vframe;
    slot_next[s] = *b; *b = (int16_t)s;
    mc_copy((volatile void *)(CACHE_BASE + s * 128), spr_cells_file + t * 128, 128);
    return (int)s;
  }
  return -1;                                       // more tiles in this frame than slots
}

// --- conversion of the sprites, once per frame ---
static int16_t sp_x[381], sp_top[381];
static uint8_t sp_rows[381], sp_zx[381], sp_zy[381], sp_layer[381];
static uint16_t sp_head[381];

// A layer takes any tall column without shrink (a background is drawn as columns of sprites,
// chained or not, sometimes split in several groups): the columns of one layer share the same
// position modulo 16 (the scroll of the layer) and each takes its own column of the 32 x 32 map.
static void layer_open(int layer, int x, int top) {
  mc_fill(TILEMAP(layer), 0, 32 * 32 * 4);
  mc_layer_scroll(layer, -(x & 15), -(top & 15));   // screen x = layer x - scroll
}

static void layer_column(int layer, int n) {
  volatile uint32_t *map = TILEMAP(layer);
  const uint16_t *smap = (const uint16_t *)spr_map_file;
  uint32_t count = spr_map_size / 2;
  int x = sp_x[n] & 0x1FF, top = sp_top[n] & 0x1FF, rows = sp_rows[n] > 32 ? 32 : sp_rows[n];
  int mx = (x >> 4) & 31;
  for (int j = 0; j < rows; j++) {
    uint32_t t0 = neo_vram[n * 64 + j * 2], t1 = neo_vram[n * 64 + j * 2 + 1];
    uint32_t num = t0 | ((t1 & 0xF0) << 12);
    if (num >= count) continue;
    map[(((top >> 4) + j) & 31) * 32 + mx] =
        MC_TILE(smap[num], t1 >> 8, ((t1 & 1) ? T_HFLIP : 0) | ((t1 & 2) ? T_VFLIP : 0));
  }
}

static uint32_t frames_short, tall_left;          // frames where tall columns had to stay sprites
static void video_frame(void) {
  vframe++;
  // 1. positions, sizes and groups: a sticky sprite continues the chain on its right, and so does a
  //    sprite of the same height and top, without shrink, exactly 16 pixels on the right of the
  //    previous one (backgrounds are drawn both ways)
  int px = 0, ptop = 0, prows = 0, pzy = 0, pw = 0, pzx = 0, head = 1;
  for (int n = 1; n < 381; n++) {
    uint32_t s2 = neo_vram[0x8000 + n], s3 = neo_vram[0x8200 + n], s4 = neo_vram[0x8400 + n];
    int zx = (s2 >> 8) & 15, x, top, rows, zy;
    if (s3 & 0x40) { x = (px + pw) & 0x1FF; top = ptop; rows = prows; zy = pzy; }
    else {
      x = s4 >> 7; top = (0x200 - (s3 >> 7) - 16) & 0x1FF; rows = s3 & 0x3F; zy = s2 & 0xFF;
      if (!(rows && rows == prows && top == ptop && zy == pzy && zx == 15 && pzx == 15 && x == ((px + 16) & 0x1FF)))
        head = n;
    }
    sp_x[n] = (int16_t)x; sp_top[n] = (int16_t)top; sp_rows[n] = (uint8_t)rows;
    sp_zx[n] = (uint8_t)zx; sp_zy[n] = (uint8_t)zy; sp_head[n] = (uint16_t)head; sp_layer[n] = 0;
    px = x; ptop = top; prows = rows; pzy = zy; pw = zx + 1; pzx = zx;
  }
  // 2. tall columns without shrink go to layers, the first ones (behind) on layer 3; a column that
  //    does not fit the current layer opens the next one, so the Neo Geo drawing order is kept
  int layer = 4, lx = 0, ly = 0;
  uint32_t used = 0;
  for (int n = 1; n < 381; n++) {
    if (sp_rows[n] < 8 || sp_zx[n] != 15 || sp_zy[n] != 0xFF) continue;
    int x = sp_x[n] & 0x1FF, top = sp_top[n] & 0x1FF, mx = (x >> 4) & 31;
    if (layer > 3 || (x & 15) != lx || (top & 15) != ly || (used >> mx) & 1) {
      if (layer == 1) { frames_short++; tall_left++; break; }   // no layer left: the rest stays as sprites
      layer--; lx = x & 15; ly = top & 15; used = 0;
      layer_open(layer, x, top);
    }
    used |= 1u << mx; sp_layer[n] = (uint8_t)layer;
    layer_column(layer, n);
  }
  uint32_t disp = DISP_ON | DISP_SPRITES | DISP_LAYER(0);
  for (int l = 3; l >= layer && l >= 1; l--) disp |= DISP_LAYER(l);
  // 3. the other columns, one sprite per visible tile; the Neo Geo draws higher numbers in front,
  //    MONOCORE lower numbers: go backwards
  const uint16_t *smap = (const uint16_t *)spr_map_file;
  uint32_t count = spr_map_size / 2, anim = (neo.frame / ((neo.lspc_mode >> 8) + 1)) & 7;
  int out = 0;
  for (int n = 380; n >= 1 && out < MC_SPRITES; n--) {
    if (sp_layer[n] || !sp_rows[n]) continue;
    int x = sp_x[n] >= 0x1E0 ? sp_x[n] - 0x200 : sp_x[n];
    if (x <= -16 || x >= 320) continue;
    int rows = sp_rows[n] > 32 ? 32 : sp_rows[n], zy = sp_zy[n], zx = sp_zx[n];
    for (int k = 0; k < rows && out < MC_SPRITES; k++) {
      int y = (sp_top[n] + ((k * 16 * (zy + 1)) >> 8)) & 0x1FF;
      if (y >= 0x1F0) y -= 0x200;
      if (y <= -16 || y >= 224) continue;
      uint32_t t0 = neo_vram[n * 64 + k * 2], t1 = neo_vram[n * 64 + k * 2 + 1];
      uint32_t num = t0 | ((t1 & 0xF0) << 12);
      if (t1 & 8) num = (num & ~7u) | anim; else if (t1 & 4) num = (num & ~3u) | (anim & 3);
      if (num >= count || smap[num] == 0) continue;
      int slot = cache_get(smap[num]);
      if (slot < 0) continue;
      sprite_t *s = &mc_spr[out++];
      s->x = (int16_t)x; s->y = (int16_t)y; s->tile = (uint16_t)(slot * 4);
      s->pal = (uint8_t)(t1 >> 8); s->w = 2; s->h = 2;
      s->attr = (uint8_t)(SA_ON | SA_PRIO(SPR_PRIO) | ((t1 & 1) ? SA_HFLIP : 0) | ((t1 & 2) ? SA_VFLIP : 0));
      s->zoomx = (uint16_t)((zx + 1) << 4); s->zoomy = (uint16_t)(zy + 1);
    }
  }
  mc_sprites_used(out);
  mc_copy(SPRITES, mc_spr, (uint32_t)out * sizeof(sprite_t));
  DISP_CTRL = disp;
}

#ifdef NEO_SOUND_TRACE                             // ROM for tools: every sound command in the debug console
void neo_sound_trace(uint32_t command) { mc_debug("SND %u %02X\n", neo.frame, command); }
#endif

// --- sound: clips rendered offline from the game's own Z80 driver (tools/sound/make_sounds.py) ---
// Every event plays on a voice of its own (sounds of a slot overlap, as in the driver); 14 xx stops
// the sounds started with the prefix xx, a music alone replaces the previous one, 01/03/04/0D/20
// stop everything, 05 fades out. Loops are made here (ADPCM does not loop): the vblank handler
// starts the loop part again when it is over.
typedef struct { uint32_t key, off, len, loop, format, rate, cuts; } snd_t;   // cuts: sounds it stops
#define SND_VOL 255
#define FADE_FRAMES 90
static struct { const snd_t *s; uint8_t prefix, music, looping; uint32_t age; } voice[PCM_VOICES];
static uint32_t snd_age, fade;

static const snd_t *snd_find(uint32_t key) {
  uint32_t n = *(const uint32_t *)sounds_tab_file;
  const snd_t *t = (const snd_t *)(sounds_tab_file + 4);
  for (uint32_t k = 0; k < n; k++) if (t[k].key == key) return &t[k];
  return 0;
}

static void voice_part(int v, int loop_part) {
  const snd_t *s = voice[v].s;
  uint32_t vol = fade ? SND_VOL * fade / FADE_FRAMES : SND_VOL;
  if (s->loop == PCM_NOLOOP) mc_pcm_play(v, sounds_file + s->off, s->len, (int)s->format, s->rate, PCM_NOLOOP, (int)vol, (int)vol);
  else if (!loop_part && s->loop) mc_pcm_play(v, sounds_file + s->off, s->loop, (int)s->format, s->rate, PCM_NOLOOP, (int)vol, (int)vol);
  else mc_pcm_play(v, sounds_file + s->off + s->loop / 2, s->len - s->loop, (int)s->format, s->rate, PCM_NOLOOP, (int)vol, (int)vol);
}

static void voice_stop(int v) { mc_pcm_stop(v); voice[v].s = 0; voice[v].looping = 0; }
static void snd_stop_all(void) { for (int v = 0; v < PCM_VOICES; v++) voice_stop(v); fade = 0; }

void neo_platform_sound(uint32_t prefix, uint32_t code) {
  if (!prefix) {
    if (code == 0x01 || code == 0x03 || code == 0x04 || code == 0x0D || code == 0x20) { snd_stop_all(); return; }
    if (code == 0x05) { fade = FADE_FRAMES; return; }
  }
  if (prefix == 0x14) {
    for (int v = 0; v < PCM_VOICES; v++) if (voice[v].s && voice[v].prefix == code) voice_stop(v);
    return;
  }
  if (prefix && (prefix < 0x18 || prefix > 0x1E)) return;     // 0A, 0E, 15: settings of the driver
  const snd_t *s = snd_find(prefix << 8 | code);
  if (!s) {
    if (prefix || code >= 0x20 || code == 0x02) mc_debug("SOUND %02X %02X missing: add it to tools/sound/sounds.txt\n", prefix, code);
    return;
  }
  if (!s->len) return;                                        // known, but silent
  const snd_t *t0 = (const snd_t *)(sounds_tab_file + 4);
  for (int v = 0; v < PCM_VOICES; v++)                        // the driver gives this sound their channels
    if (voice[v].s && (s->cuts >> (voice[v].s - t0) & 1)) voice_stop(v);
  int music = !prefix && code >= 0x21 && code != 0xFE && code != 0xFF && code < 0x60;
  if (music) for (int v = 0; v < PCM_VOICES; v++) if (voice[v].s && voice[v].music) voice_stop(v);
  int pick = -1;
  for (int v = 0; v < PCM_VOICES && pick < 0; v++) if (voice[v].s == s) pick = v;          // same sound: again
  for (int v = 0; v < PCM_VOICES && pick < 0; v++) if (!voice[v].s || !(PCM[v].ctrl & 1)) if (!voice[v].looping) pick = v;
  for (int v = 0; v < PCM_VOICES; v++)                                                      // else the oldest one-shot
    if (!voice[v].looping && (pick < 0 || voice[v].age < voice[pick].age)) pick = v;
  if (pick < 0) pick = 0;
  voice[pick].s = s; voice[pick].prefix = (uint8_t)prefix; voice[pick].music = (uint8_t)music;
  voice[pick].looping = s->loop != PCM_NOLOOP; voice[pick].age = ++snd_age;
  voice_part(pick, 0);
}

static void sound_frame(void) {                 // vblank: loops and fade-out
  for (int v = 0; v < PCM_VOICES; v++)
    if (voice[v].s && voice[v].looping && !(PCM[v].ctrl & 1)) voice_part(v, 1);
  if (fade) {
    if (--fade == 0) { snd_stop_all(); return; }
    uint32_t vol = SND_VOL * fade / FADE_FRAMES;
    for (int v = 0; v < PCM_VOICES; v++) if (voice[v].s) PCM[v].vol_l = PCM[v].vol_r = (uint8_t)vol;
  }
}

// --- inputs, interrupt ---
static void inputs(void) {
  uint32_t a = IN_P1, b = IN_P2;
  neo.p1 = (uint8_t)(a & 0xFF);                     // directions, then buttons 1-4 as A-D
  neo.p2 = (uint8_t)(b & 0xFF);
  neo.status_b = (uint8_t)(((a & IN_START) ? 1 : 0) | ((b & IN_START) ? 4 : 0));
}

static uint32_t conv_max, conv_sum;
static void vblank(void) {
  uint32_t t0 = (uint32_t)mc_cycles();
  inputs();
  sound_frame();
  video_frame();
  uint32_t t = (uint32_t)mc_cycles() - t0;       // conversion time, reported every 10 s
  conv_sum += t; if (t > conv_max) conv_max = t;
  if (vframe % 600 == 0) {
    mc_debug("frame %u: conversion %u cycles on average, %u at most; %u frames short of layers\n",
             vframe, conv_sum / 600, conv_max, frames_short);
    conv_sum = conv_max = 0; frames_short = 0;
  }
  neo_vblank(&cpu);
}

// --- translated program ---
uint32_t m68k_external(m68k_t *c, uint32_t pc) {
  uint32_t r = neo_bios(c, pc);
  if (r) return r;
  mc_debug("UNKNOWN %06X (frame %u): add it to entries.txt\n", pc, neo.frame);
  mc_text_layer(0, 3);
  mc_text(0, 2, 2, "UNKNOWN ADDRESS", 0);
  for (;;) wfi();
}
void m68k_stop(m68k_t *c) { while (c->ipl <= (c->srh & 7)) wfi(); }

int main(void) {
  mc_init();
  mc_video_mode(320, 224);
  mc_layer(0, fix_file, MC_TILE8 | MC_4BPP, 64, 32, 3);
  for (int l = 1; l <= 3; l++) mc_layer(l, spr_file, MC_TILE16 | MC_4BPP, 32, 32, 0);
  mc_layer_scroll(0, 0, 16);                         // the Neo Geo screen starts at fix row 2
  mc_sprite_sheet((const void *)CACHE_BASE);
  mc_background(0xFFF);                              // last colour of palette 255
  cache_init();
  neo_vram = (uint16_t *)CHRRAM_BASE;
  neo_rom = p1_file; neo_rom_size = p1_size;
  neo_reset(&cpu);
  mc_display(DISP_ON | DISP_LAYER(0) | DISP_SPRITES);
  mc_on_vblank(vblank);
  m68k_run(&cpu);
  return 0;
}
