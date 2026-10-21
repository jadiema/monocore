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
extern const uint8_t sounds_file[], sounds_tab_file[], music_file[], music_tab_file[];

#define VRAM_BYTES  0x11000u                       // 0x8800 words
#define DECOR_W     12                             // decor of the four fields: 96 x 144 pixels, 8 bpp,
#define DECOR_H     18                             // at the end of CHR RAM (see video_four)
#define DECOR_BYTES ((DECOR_W * DECOR_H + 1) * 64)  // + one empty cell for the rest of layer 1
#define CACHE_SLOTS ((MC_CHRRAM_SIZE - VRAM_BYTES - DECOR_BYTES) / 128)
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
static int four_mode;
#define HUD_PAL     208                            // palettes 208-254 are never used by the game:
#define DECOR_PAL   240                            // 208-211 the four players, 240-254 the decor window
#define DECOR_SLOTS 15                             // (palette 255 holds the backdrop colour, 0xFFF)
void neo_platform_color(uint32_t index, uint32_t color) {
  index &= 0xFFF;                                  // ours in the 4-player screen (kept in neo_pal)
  if (four_mode && index >= HUD_PAL * 16 && index < 255 * 16) return;
  PALETTE[index] = rgb555(color);
}

void neo_platform_bank(void) {
  const uint16_t *p = neo_pal[neo.pal_bank];
  for (int i = 0; i < 0x1000; i++) PALETTE[i] = rgb555(p[i]);
}
void neo_platform_fix(uint32_t x, uint32_t y, uint32_t e) {
  if (four_mode) return;                           // layer 0 shows the scores of the four players
  TILEMAP(0)[y * 64 + x] = MC_TILE(e & 0xFFF, e >> 12, 0);
}
static uint32_t idle_cycles, waits;                 // load, reported every 10 s
void neo_platform_wait(uint32_t frame) {   // sleep until the vblank (neo.frame changes in the handler:
  uint32_t t0 = (uint32_t)mc_cycles(); waits++;
  while (*(volatile uint32_t *)&neo.frame == frame) wfi();   // read it again on every turn)
  idle_cycles += (uint32_t)mc_cycles() - t0;
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

// --- four players (DESIGN_4P.md, step 4): the four fields side by side ---
// When tasks 7 and 8 run (patches/four.s), the screen is 384 pixels wide and every field is shown
// at 5/8 of its size (a 16-pixel tile is 10 pixels: no seam between the tiles of a character).
// Where the sprites of a field are (patches/band.s): players 1 and 2 on the Neo Geo screen, left
// and right; players 3 and 4 in the band 256 lines below, on the right, player 4 256 pixels
// further. Each field is the 152 useful pixels of its half of the Neo Geo screen.
#define FOUR_W      384
#define FOUR_SLOT   96                             // width of a field on the screen
#define FOUR_TOP    22                             // first line of the fields
#define FOUR_SCALE  160                            // 5/8 in 8.8
#define LIST_FIRST  118                            // the list of the frame starts at this sprite: below, the fixed decor
#define DECOR_BASE  (CHRRAM_BASE + MC_CHRRAM_SIZE - DECOR_BYTES)

static inline uint32_t ram16(uint32_t a) { a &= 0xFFFF; return neo_ram[a] | neo_ram[a + 1] << 8; }
// tasks 7 and 8 are the fields of players 3 and 4 (bytes FIELD7 and FIELD8, patches/band.s: the
// game uses these task numbers for other things on other screens)
static int four_running(void) { return neo_ram[0xF65C ^ 1] | neo_ram[0xF65D ^ 1]; }

// field (0-3) and position in it of a tile at Neo Geo (x, y) in the 512 x 512 space; -1: none
static int four_field(int x, int y, int *lx, int *ly) {
  int yy = y & 0x1FF, xx = x & 0x1FF, k;
  if (yy < 240 || yy >= 0x1F0) {                   // the Neo Geo screen: players 1 and 2
    int xs = xx >= 0x1E0 ? xx - 0x200 : xx;
    k = xs + 8 < 160 ? 0 : 1;
    *lx = xs - (k ? 164 : 4); *ly = yy >= 0x1F0 ? yy - 0x200 : yy;
  } else {                                         // the band: players 3 and 4
    *ly = yy - 256;
    if (xx >= 0x60 && xx < 0x160) { k = 2; *lx = xx - 164; }
    else { int xp = (xx - 256) & 0x1FF; if (xp >= 0x1E0) xp -= 0x200; k = 3; *lx = xp - 164; }
  }
  if (*lx <= -16 || *lx >= 152 || *ly <= -16 || *ly >= 224) return -1;
  return k;
}

// The decor of a field (the fixed sprites, below the list of the frame): the layers of the
// MONOCORE do not shrink, so the decor of the left field is drawn once, at 5/8, into 12 x 18
// cells of 8 x 8 pixels in 8 bpp in CHR RAM, and layer 1 shows them behind each of the four
// fields (no sprite: sprites of the decor, the last ones of the table, were the first ones left
// out on the lines with more than 64 sprites, and the decor disappeared there). Its pixels are
// slot * 16 + colour: the up to 16 palettes of the decor are copied into the palettes 240-255
// every frame (the decor follows the fades of the game). Drawn again when the decor changes.
static const uint8_t zoom_x[16][16] = {   // pixels kept by the horizontal shrink of the Neo Geo
  {0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0}, {0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0},
  {0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0}, {0,0,1,0,1,0,0,0,1,0,0,0,1,0,0,0},
  {0,0,1,0,1,0,0,0,1,0,0,0,1,0,1,0}, {0,0,1,0,1,0,1,0,1,0,0,0,1,0,1,0},
  {0,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0}, {1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0},
  {1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0}, {1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,0},
  {1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1}, {1,0,1,1,1,0,1,1,1,1,1,0,1,0,1,1},
  {1,0,1,1,1,0,1,1,1,1,1,0,1,1,1,1}, {1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1},
  {1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1}, {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}};
static uint8_t decor_slot[256];                    // Neo Geo palette -> slot + 1 (0: none yet)
static uint8_t decor_pal_of[16], decor_slots;
static uint32_t decor_sign = 1;

// one line of the decor: the fixed sprites on the Neo Geo line sl, x 4-155 (the left field)
static void decor_line(int sl, uint16_t *line) {
  const uint16_t *smap = (const uint16_t *)spr_map_file;
  uint32_t count = spr_map_size / 2;
  for (int x = 0; x < 152; x++) line[x] = 0xFFFF;
  int px = 0, py = 0, prows = 0, pzy = 0, pw = 0;
  for (int n = 1; n < LIST_FIRST; n++) {
    uint32_t s2 = neo_vram[0x8000 + n], s3 = neo_vram[0x8200 + n], s4 = neo_vram[0x8400 + n];
    int zx = (s2 >> 8) & 15, zy, rows, sx, sy;
    if (s3 & 0x40) { sx = (px + pw) & 0x1FF; sy = py; rows = prows; zy = pzy; }
    else { sx = s4 >> 7; sy = 0x200 - (s3 >> 7); rows = s3 & 0x3F; zy = s2 & 0xFF; }
    px = sx; py = sy; prows = rows; pzy = zy; pw = zx + 1;
    if (!rows) continue;
    int x = sx >= 0x1E0 ? sx - 0x200 : sx;
    if (x >= 156 || x <= 4 - 16) continue;
    int sline = (sl - sy) & 0x1FF, zline = sline & 0xFF, invert = sline & 0x100;
    if (invert) zline ^= 0xFF;
    if (rows > 0x20) {
      zline %= (zy + 1) << 1;
      if (zline > zy) { zline = ((zy + 1) << 1) - 1 - zline; invert = !invert; }
    } else if (sline >= (rows << 4) || zline > zy) continue;
    int src = zy == 0xFF ? zline : zline * 256 / (zy + 1);
    int tile = (src >> 4) & 0x1F, ty = src & 15;
    if (invert) { ty ^= 15; tile ^= 0x1F; }
    uint32_t t0 = neo_vram[n * 64 + tile * 2], t1 = neo_vram[n * 64 + tile * 2 + 1];
    uint32_t num = t0 | ((t1 & 0xF0) << 12);
    if (t1 & 2) ty ^= 15;
    if (num >= count || !smap[num]) continue;
    const uint8_t *row = spr_file + smap[num] * 128 + ty * 8;
    for (int k = 0; k < 16; k++) {
      int kk = (t1 & 1) ? 15 - k : k;
      if (!zoom_x[zx][kk]) continue;
      int p = (row[kk >> 1] >> ((kk & 1) * 4)) & 15, lx = x - 4;
      if (p && lx >= 0 && lx < 152) line[lx] = (uint16_t)(((t1 >> 8) << 4) | p);
      x++;
    }
  }
}

static void decor_draw(void) {
  static uint16_t line[152];
  static uint8_t cells[DECOR_W * 64];
  uint32_t opaque = 0;
  for (int i = 0; i < 256; i++) decor_slot[i] = 0;
  decor_slots = 0;
  for (int ty = 0; ty < DECOR_H * 8; ty++) {
    int sy = ty * 8 / 5;
    if (sy < 224) decor_line(sy + 16, line); else for (int x = 0; x < 152; x++) line[x] = 0xFFFF;
    for (int tx = 0; tx < DECOR_W * 8; tx++) {
      int sx = tx * 8 / 5; uint32_t v = sx < 152 ? line[sx] : 0xFFFF; uint8_t b = 0;
      if (v != 0xFFFF) {
        uint32_t pal = v >> 4, slot = decor_slot[pal];
        if (!slot && decor_slots < DECOR_SLOTS) { decor_pal_of[decor_slots] = (uint8_t)pal; slot = decor_slot[pal] = ++decor_slots; }
        if (slot) b = (uint8_t)(((slot - 1) << 4) | (v & 15));
        opaque++;
      }
      cells[(tx >> 3) * 64 + (ty & 7) * 8 + (tx & 7)] = b;
    }
    if ((ty & 7) == 7) mc_copy((volatile void *)(DECOR_BASE + (ty >> 3) * DECOR_W * 64), cells, sizeof cells);
  }
  mc_debug("DECOR frame %u: %u palettes, %u%% opaque\n", neo.frame, decor_slots, opaque * 100 / (DECOR_W * 8 * DECOR_H * 8));
}

// An empty decor (the game clears or moves it away, at the end of a round of players 1 and 2) does
// not replace the one shown. The signature tells a decor from another one (the palettes, heights
// and shrink of its sprites), not where it is nor its tiles: the town scrolls and is animated, and
// drawing it again at every step took 2.8 million cycles every 12 frames; the small decor is a
// still picture of it.
static int decor_empty(void) {                    // one line in 16 of the decor, as it would be drawn:
  static uint16_t line[152];                       // less than a quarter covered is no decor
  uint32_t full = 0, all = 0;
  for (int ty = 0; ty < DECOR_H * 8; ty += 16) {
    decor_line(ty * 8 / 5 + 16, line);
    for (int x = 0; x < 152; x++) { all++; full += line[x] != 0xFFFF; }
  }
  return full * 4 < all;
}
static void decor_frame(void) {
  uint32_t h = 2166136261u;                        // the decor changes: draw it again
  for (int n = 1; n < LIST_FIRST; n++) {
    h = (h ^ neo_vram[0x8000 + n]) * 16777619u; h = (h ^ (neo_vram[0x8200 + n] & 0x7F)) * 16777619u;
    h = (h ^ (neo_vram[n * 64 + 1] >> 8)) * 16777619u;
  }
  if (h != decor_sign) { decor_sign = h; if (!decor_empty()) decor_draw(); }
  const uint16_t *p = neo_pal[neo.pal_bank];        // its palettes, with the fades of the game
  for (int k = 0; k < decor_slots; k++)
    for (int c = 0; c < 16; c++) PALETTE[(DECOR_PAL + k) * 16 + c] = rgb555(p[decor_pal_of[k] * 16 + c]);
}

// The names and scores of the four players, under the fields, on layer 0 with the font of the
// game's fix layer (ASCII codes). Scores: 32-bit binary, players 1 and 2 at $1057DC / $1057E0,
// players 3 and 4 at $10F620 / $10F624 (patches/four.s).
static const uint32_t score_at[4] = {0x1057DC, 0x1057E0, 0x10F620, 0x10F624};
static const uint32_t hud_rgb[4] = {0x50C8FF, 0xFF6E6E, 0x78E678, 0xFAC83C};
static void hud_text(int col, int row, const char *t, int pal) {
  for (; *t; t++, col++) TILEMAP(0)[row * 64 + col] = MC_TILE((uint8_t)*t, pal, 0);
}
#define FIX_EMPTY 0xFF                             // the empty tile of the fix layer (the game clears with it)
static void decor_layer_open(void) {             // layer 1: the decor cells, four times side by side
  mc_layer(1, MC_CHR(DECOR_BASE - CHRRAM_BASE), MC_TILE8 | MC_8BPP, 64, 32, 0);
  mc_layer_scroll(1, 0, -FOUR_TOP);                // screen y = layer y + FOUR_TOP
  mc_fill((volatile void *)(DECOR_BASE + DECOR_W * DECOR_H * 64), 0, 64);   // the empty cell
  for (int y = 0; y < 32; y++)
    for (int x = 0; x < 64; x++) {
      int f = x / DECOR_W, cx = x % DECOR_W;
      uint32_t t = (f < 4 && y < DECOR_H) ? (uint32_t)(y * DECOR_W + cx) : DECOR_W * DECOR_H;
      TILEMAP(1)[y * 64 + x] = MC_TILE(t, DECOR_PAL, 0);
    }
}
static void hud_open(void) {
  decor_layer_open();
  for (int i = 0; i < 64 * 32; i++) TILEMAP(0)[i] = MC_TILE(FIX_EMPTY, 0, 0);
  for (int k = 0; k < 4; k++) {                    // the font: 1 = the letter, 2 = its box (opaque)
    uint32_t c = hud_rgb[k];
    uint16_t v = (uint16_t)(((c >> 19) << 10) | (((c >> 11) & 31) << 5) | ((c >> 3) & 31));
    for (int i = 1; i < 16; i++) PALETTE[(HUD_PAL + k) * 16 + i] = i == 1 ? v : 0;
    char name[] = "PLAYER 1"; name[7] = (char)('1' + k);
    hud_text(k * 12 + 2, 23, name, HUD_PAL + k);
  }
}
static void hud_frame(void) {
  for (int k = 0; k < 4; k++) {
    uint32_t v = ram16(score_at[k]) << 16 | ram16(score_at[k] + 2);
    char t[9]; t[8] = 0;
    for (int i = 7; i >= 0; i--) { t[i] = (char)(v || i == 7 ? '0' + v % 10 : ' '); v /= 10; }
    hud_text(k * 12 + 2, 25, t, HUD_PAL + k);
  }
}
static void hud_close(void) {                      // the game's fix layer again, and layer 1
  mc_layer(1, spr_file, MC_TILE16 | MC_4BPP, 32, 32, 0);
  for (uint32_t x = 0; x < 40; x++)
    for (uint32_t y = 0; y < 32; y++) {
      uint32_t e = neo_vram[0x7000 + x * 32 + y];
      TILEMAP(0)[y * 64 + x] = MC_TILE(e & 0xFFF, e >> 12, 0);
    }
  for (uint32_t x = 40; x < 64; x++) for (uint32_t y = 0; y < 32; y++) TILEMAP(0)[y * 64 + x] = MC_TILE(FIX_EMPTY, 0, 0);
}

static void video_four(void) {
  const uint16_t *smap = (const uint16_t *)spr_map_file;
  uint32_t count = spr_map_size / 2, anim = (neo.frame / ((neo.lspc_mode >> 8) + 1)) & 7;
  int out = 0;
  for (int n = 380; n >= LIST_FIRST && out < MC_SPRITES; n--) {
    if (!sp_rows[n]) continue;
    int rows = sp_rows[n] > 32 ? 32 : sp_rows[n], zy = sp_zy[n], zx = sp_zx[n];
    for (int k = 0; k < rows && out < MC_SPRITES; k++) {
      int lx, ly, f = four_field(sp_x[n], sp_top[n] + ((k * 16 * (zy + 1)) >> 8), &lx, &ly);
      if (f < 0) continue;
      uint32_t t0 = neo_vram[n * 64 + k * 2], t1 = neo_vram[n * 64 + k * 2 + 1];
      uint32_t num = t0 | ((t1 & 0xF0) << 12);
      if (t1 & 8) num = (num & ~7u) | anim; else if (t1 & 4) num = (num & ~3u) | (anim & 3);
      if (num >= count || smap[num] == 0) continue;
      int slot = cache_get(smap[num]);
      if (slot < 0) continue;
      sprite_t *s = &mc_spr[out++];
      s->x = (int16_t)(f * FOUR_SLOT + 1 + ((lx * FOUR_SCALE) >> 8));
      s->y = (int16_t)(FOUR_TOP + ((ly * FOUR_SCALE) >> 8));
      s->tile = (uint16_t)(slot * 4);
      s->pal = (uint8_t)(t1 >> 8); s->w = 2; s->h = 2;
      s->attr = (uint8_t)(SA_ON | SA_PRIO(SPR_PRIO) | ((t1 & 1) ? SA_HFLIP : 0) | ((t1 & 2) ? SA_VFLIP : 0));
      s->zoomx = (uint16_t)((((zx + 1) << 4) * FOUR_SCALE) >> 8);
      s->zoomy = (uint16_t)(((zy + 1) * FOUR_SCALE) >> 8);
    }
  }
  decor_frame();                                   // the decor, on layer 1
  mc_sprites_used(out);
  mc_copy(SPRITES, mc_spr, (uint32_t)out * sizeof(sprite_t));
  hud_frame();
  DISP_CTRL = DISP_ON | DISP_SPRITES | DISP_LAYER(0) | DISP_LAYER(1);
}

static uint32_t frames_short, tall_left;          // frames where tall columns had to stay sprites
static void video_frame(void) {
  vframe++;
  int four = four_running();
  if (four != four_mode) {
    four_mode = four; mc_video_mode(four ? FOUR_W : 320, 224);
    if (four) hud_open();
    else { neo_platform_bank(); hud_close(); }      // the palettes 208-255 and the fix layer of the game again
    decor_sign = 1;
    mc_debug("FOUR %d frame %u tasks %04X%04X\n", four, neo.frame, ram16(0x1034C6), ram16(0x1034C8));
  }
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
  if (four_mode) { video_four(); return; }
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
// stop everything, 05 fades out. The clips are PCM (16-bit, or 8-bit for musics), musics loop in
// hardware (LOOP).
typedef struct { uint32_t key, off, len, loop, format, rate, vol, cuts; } snd_t;   // cuts: sounds it stops
#define FADE_FRAMES 90
#define SFX_GAIN 210            /* /256: room for the music and for sounds playing together */
static struct { const snd_t *s; uint8_t prefix, music, looping; uint32_t age; } voice[PCM_VOICES];
static uint32_t snd_age, fade;

// Musics: original pieces made for the port (tools/sound/make_music.py) in place of the game's,
// on voice 0, looping in hardware. A music event replaces the music; every "14 xx" stops it (the
// game sends one at each change of screen), and so do the stops of everything and the fade-out.
typedef struct { uint32_t key, off_l, off_r, len; } mus_t;
#define MUSIC_VOICE 0
#define MUSIC_VOL 195
static const mus_t *music_now;

static const mus_t *music_find(uint32_t key) {
  uint32_t n = *(const uint32_t *)music_tab_file;
  const mus_t *t = (const mus_t *)(music_tab_file + 4);
  for (uint32_t k = 0; k < n; k++) if (t[k].key == key) return &t[k];
  return 0;
}
static void music_play(const mus_t *m) {
  music_now = m;
  mc_pcm_play(MUSIC_VOICE, music_file + m->off_l, m->len, PCM_S16, 22050, 0, MUSIC_VOL, MUSIC_VOL);
}
static void music_stop(void) { if (music_now) mc_pcm_stop(MUSIC_VOICE); music_now = 0; }

static const snd_t *snd_find(uint32_t key) {
  uint32_t n = *(const uint32_t *)sounds_tab_file;
  const snd_t *t = (const snd_t *)(sounds_tab_file + 4);
  for (uint32_t k = 0; k < n; k++) if (t[k].key == key) return &t[k];
  return 0;
}

static void voice_part(int v, int loop_part) {
  (void)loop_part;
  const snd_t *s = voice[v].s;
  uint32_t vol = s->vol * SFX_GAIN / 256;
  if (fade) vol = vol * fade / FADE_FRAMES;
  mc_pcm_play(v, sounds_file + s->off, s->len, (int)s->format, s->rate, s->loop, (int)vol, (int)vol);
}

static void voice_stop(int v) { mc_pcm_stop(v); voice[v].s = 0; voice[v].looping = 0; }
static void snd_stop_all(void) { for (int v = 1; v < PCM_VOICES; v++) voice_stop(v); music_stop(); fade = 0; }

void neo_platform_sound(uint32_t prefix, uint32_t code) {
  if (!prefix) {
    if (code == 0x01 || code == 0x03 || code == 0x04 || code == 0x0D || code == 0x20) { snd_stop_all(); return; }
    if (code == 0x05) { fade = FADE_FRAMES; return; }
  }
  if (prefix == 0x14) {
    for (int v = 1; v < PCM_VOICES; v++) if (voice[v].s && voice[v].prefix == code) voice_stop(v);
    music_stop();
    return;
  }
  if (prefix && (prefix < 0x18 || prefix > 0x1E)) return;     // 0A, 0E, 15: settings of the driver
  const mus_t *m = music_find(prefix << 8 | code);
  if (m) { if (!music_now || m->off_l != music_now->off_l) music_play(m); return; }   // same piece: goes on
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
  if (music) for (int v = 1; v < PCM_VOICES; v++) if (voice[v].s && voice[v].music) voice_stop(v);
  int pick = -1;
  for (int v = 1; v < PCM_VOICES && pick < 0; v++) if (voice[v].s == s) pick = v;          // same sound: again
  for (int v = 1; v < PCM_VOICES && pick < 0; v++) if (!voice[v].s || !(PCM[v].ctrl & 1)) if (!voice[v].looping) pick = v;
  if (pick < 0)                                                                             // else the oldest one-shot
    for (int v = 1; v < PCM_VOICES; v++)
      if (!voice[v].looping && (pick < 0 || voice[v].age < voice[pick].age)) pick = v;
  if (pick < 0) pick = 1;
  voice[pick].s = s; voice[pick].prefix = (uint8_t)prefix; voice[pick].music = (uint8_t)music;
  voice[pick].looping = s->loop != PCM_NOLOOP; voice[pick].age = ++snd_age;
  voice_part(pick, 0);
}

static void sound_frame(void) {                 // vblank: fade-out
  if (fade) {
    if (--fade == 0) { snd_stop_all(); return; }
    for (int v = 1; v < PCM_VOICES; v++)
      if (voice[v].s) PCM[v].vol_l = PCM[v].vol_r = (uint8_t)(voice[v].s->vol * SFX_GAIN / 256 * fade / FADE_FRAMES);
    if (music_now) PCM[MUSIC_VOICE].vol_l = PCM[MUSIC_VOICE].vol_r = (uint8_t)(MUSIC_VOL * fade / FADE_FRAMES);
  }
}

// --- inputs, interrupt ---
static void inputs(void) {
  uint32_t a = IN_P1, b = IN_P2;
  neo.p1 = (uint8_t)(a & 0xFF);                     // directions, then buttons 1-4 as A-D
  neo.p2 = (uint8_t)(b & 0xFF);
  neo.p3 = (uint8_t)(IN_P3 & 0xFF);                 // players 3 and 4 (patches/four.s)
  neo.p4 = (uint8_t)(IN_P4 & 0xFF);
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
  if (vframe % 600 == 0) {                         // per frame: 833 333 cycles at 50 MHz and 60 Hz
    uint32_t idle = idle_cycles > conv_sum ? idle_cycles - conv_sum : 0;   // the conversion runs while the game waits
    mc_debug("frame %u: conversion %u cycles on average, %u at most; game %u%% of the time, %u of 600 frames on time%s\n",
             vframe, conv_sum / 600, conv_max, 100 - (uint32_t)((uint64_t)idle * 100 / (600ull * CPU_HZ / 60)),
             waits, four_mode ? " (4 players)" : "");
    conv_sum = conv_max = 0; frames_short = 0; idle_cycles = 0; waits = 0;
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
