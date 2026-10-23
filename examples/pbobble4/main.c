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
#define DECOR_W     14                             // decor of the four fields, at most (cells of 8 x 8,
#define DECOR_H     30                             // 8 bpp, at the end of CHR RAM; see the layouts, arena.h)
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
static int arena_pending, arena_players = 4, arena_restart, arena_guide;   // the ARENA mode (arena.h)
static uint32_t arena_wait;
static int four_mode, four_last = 381;          // four_last: past the last sprite of the frame list
extern uint32_t neo_list_last;                  // neort.c: the last sprite the list was sent to
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
// During a game of two, the players 3 and 4 who joined (with their start) get a reminder on the
// last line of the fix layer (row 29): P3 READY on the left, P4 READY on the right; their field
// comes at the next round. At the title and in the demo, a box at the bottom right invites to the
// 4-player game (start on pad 3 or 4). Both in palette 214 (never used by the game), and their
// cells go back to the game's fix layer when they end.
static uint8_t hint_on[2], box_on;
#define HINT_ROW 29
#define TEXT_PAL 214                               // never used by the game: yellow letters on black
#define BOX_X    26                                // the box of the title: 13 x 5 cells, bottom right
#define BOX_Y    24
static int menu_drawn_cell(uint32_t x, uint32_t y);
static int hint_cell(uint32_t x, uint32_t y) {
  if (menu_drawn_cell(x, y)) return 1;
  if (box_on && x >= BOX_X && x < BOX_X + 13 && y >= BOX_Y && y < BOX_Y + 5) return 1;
  return y == HINT_ROW && ((hint_on[0] && x >= 1 && x < 9) || (hint_on[1] && x >= 31 && x < 39));
}
static void fix_back(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h) {   // cells back to the game
  for (uint32_t y = y0; y < y0 + h; y++)
    for (uint32_t x = x0; x < x0 + w; x++) {
      uint32_t e = neo_vram[0x7000 + x * 32 + y];
      TILEMAP(0)[y * 64 + x] = MC_TILE(e & 0xFFF, e >> 12, 0);
    }
}
void neo_platform_fix(uint32_t x, uint32_t y, uint32_t e) {
  if (four_mode) return;                           // layer 0 shows the scores of the four players
  if (hint_cell(x, y)) return;                     // the reminder of players 3 and 4 is there
  TILEMAP(0)[y * 64 + x] = MC_TILE(e & 0xFFF, e >> 12, 0);
}
static uint32_t idle_cycles, waits;                 // load, reported every 10 s
extern void _start(void);
void neo_platform_wait(uint32_t frame) {   // sleep until the vblank (neo.frame changes in the handler:
  uint32_t t0 = (uint32_t)mc_cycles(); waits++;
  while (*(volatile uint32_t *)&neo.frame == frame) {   // read it again on every turn)
    wfi();
    if (*(volatile int *)&arena_restart) { mc_irq_off(); IRQ_ENABLE = 0; _start(); }   // the end of an arena:
  }                                                // the console starts again, as at power on
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

static int arena_on;
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
// When tasks 7 and 8 run (patches/four.s), the screen is 384 x 288, the four fields in 2 x 2 side
// by side, every field shown at 11/16 of its size (a 16-pixel tile is 11 pixels: no seam between
// the tiles of a character): 152 x 208 pixels of the Neo Geo field (all but its header) make
// 104 x 143; names and scores in two panels, left and right.
// Where the sprites of a field are (patches/band.s): players 1 and 2 on the Neo Geo screen, left
// and right; players 3 and 4 in the band 256 lines below, on the right, player 4 256 pixels
// further. Each field is the 152 useful pixels of its half of the Neo Geo screen.
#define FOUR_W      384                            // 2 x 2 fields side by side: each line of the screen
#define FOUR_H      288                            // crosses two fields only (64 sprites per line at most)
#define FOUR_TOP    16                             // the Neo Geo lines shown: 16-223 (all the field but its
#define FOUR_LINES  208                            // header: the ceiling down to the launcher and the dragon)
// Two layouts of the four fields, DIP switch 1 (read every frame):
// - off, 2 x 2: player 1 top left, 2 top right, 3 bottom left, 4 bottom right, 11/16 both ways (a
//   16-pixel tile is 11 x 11): the largest two fields stacked in 288 lines; panels left and right.
// - on, 4 x 1, for a 16:9 screen (the 384 x 288 picture stretched to 16:9, as the MiSTer does in
//   full screen): the fields side by side, 10/16 wide and 14/16 high (a tile is 10 x 14), which the
//   stretch makes round again (as on the Neo Geo): 22 % larger on the screen than 2 x 2; names and
//   scores above, OUT / WINNER and the rounds won below. Each line then crosses the four fields.
typedef struct {
  int16_t x[4], y[4];                              // top left of each field on the screen
  uint16_t sx, sy;                                 // scale, 8.8
  uint8_t dw, dh, dn, dd;                          // decor: cells; x: source = screen * 16 / dn ...
  uint8_t en, ed;                                  // ... y: source = screen * en / ed
  uint8_t col[4], row[4], at_state, at_wins;       // panels: name at row, score row + 2
  uint8_t ox;                                      // the field starts ox source pixels in (centred)
} layout_t;
static const layout_t layouts[2] = {
  {{80, 192, 80, 192}, {0, 0, 144, 144}, 176, 176, 14, 18, 11, 0, 16, 11,
   {0, 38, 0, 38}, {2, 2, 20, 20}, 5, 7, 0},
  {{0, 96, 192, 288}, {40, 40, 40, 40}, 160, 224, 12, 23, 10, 0, 16, 14,
   {2, 14, 26, 38}, {1, 1, 1, 1}, 29, 31, 8},     // the frame and the texts centred in the 96 pixels
};
static const layout_t *L = &layouts[0];
static int four_layout;                            // 0: 2 x 2, 1: 4 x 1
// Where each player is: in 4 x 1, only the players present (1, 2, and 3 or 4 if they joined) get a
// column, side by side and centred (three players: three columns); in 2 x 2, the four places.
static uint32_t conv_miss, conv_out_max, conv_miss_frames;   // the 4-player conversion, measured
static int16_t fx[4], fy[4];
static uint8_t fcol[4], frow[4], fon[4], fcount;
static int place_fields(void) {                   // 1 when the places changed
  uint8_t on[4] = {1, 1, (uint8_t)(neo_ram[0xF614 ^ 1] || neo_ram[0xF65C ^ 1]), (uint8_t)(neo_ram[0xF615 ^ 1] || neo_ram[0xF65D ^ 1])};
  int n = 0, changed = 0;
  for (int k = 0; k < 4; k++) n += on[k];
  if (!four_layout) for (int k = 0; k < 4; k++) on[k] = 1, n = 4;
  int x0 = four_layout ? (384 - n * 96) / 2 : 0, i = 0;
  for (int k = 0; k < 4; k++) {
    int16_t x = four_layout ? (int16_t)(x0 + 96 * i) : L->x[k];
    uint8_t col = four_layout ? (uint8_t)(x / 8 + 2) : L->col[k];
    if (on[k] != fon[k] || x != fx[k]) changed = 1;
    fon[k] = on[k]; fx[k] = x; fy[k] = L->y[k]; fcol[k] = col; frow[k] = L->row[k];
    i += on[k];
  }
  if (n != fcount) changed = 1;
  fcount = (uint8_t)n;
  return changed;
}
#define LIST_FIRST  118                            // the list of the frame starts at this sprite: below, the fixed decor
#define DECOR_BASE  (CHRRAM_BASE + MC_CHRRAM_SIZE - DECOR_BYTES)

static inline uint32_t ram16(uint32_t a) { a &= 0xFFFF; return neo_ram[a] | neo_ram[a + 1] << 8; }
// tasks 7 and 8 are the fields of players 3 and 4 (bytes FIELD7 and FIELD8, patches/band.s: the
// game uses these task numbers for other things on other screens)
// The screen stays the 4-player one 3 s after the fields end, for the result of the round (WINNER,
// OUT): FOURROUND ($10F616) says the round had a field for player 3 or 4.
static uint32_t four_ended;
static int four_running(void) {
  if (arena_on) return 1;                          // the arena uses the 384 x 288 screen
  if (neo_ram[0xF65C ^ 1] | neo_ram[0xF65D ^ 1]) { four_ended = neo.frame; return 1; }
  return neo_ram[0xF616 ^ 1] && four_mode && neo.frame - four_ended < 180;
}

// field (0-3) and position in it of a tile at Neo Geo (x, y) in the 512 x 512 space; -1: none
static int four_field(int x, int y, int *lx, int *ly) {
  int yy = y & 0x1FF, xx = x & 0x1FF, k;
  if (yy < 240 || yy >= 0x1F0) {                   // the Neo Geo screen: players 1 and 2
    int xs = xx >= 0x1E0 ? xx - 0x200 : xx;
    k = xs + 8 < 160 ? 0 : 1;
    *lx = xs - (k ? 148 : 4); *ly = yy >= 0x1F0 ? yy - 0x200 : yy;   // the frames: 24-151, 168-295
  } else {                                         // the band: players 3 and 4
    *ly = yy - 256;
    if (xx >= 0x60 && xx < 0x160) { k = 2; *lx = xx - 148; }
    else { int xp = (xx - 256) & 0x1FF; if (xp >= 0x1E0) xp -= 0x200; k = 3; *lx = xp - 148; }
  }
  *ly -= FOUR_TOP;
  if (*lx <= -16 || *lx >= 152 || *ly <= -16 || *ly >= FOUR_LINES) return -1;
  return k;
}

// The decor of a field (the fixed sprites, below the list of the frame): the layers of the
// MONOCORE do not shrink, so the decor of the left field is drawn once, at 11/16, into 14 x 18
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
  for (int x = 0; x < 168; x++) line[x] = 0xFFFF;
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
      if (p && lx >= 0 && lx < 168) line[lx] = (uint16_t)(((t1 >> 8) << 4) | p);
      x++;
    }
  }
}

// The decor drawn by rows of cells, two per frame (one row is about 180 000 cycles: the whole decor
// in one frame took up to 4.2 million, five frames in a row without the game). reset: a new decor,
// its palettes are taken again.
static void decor_rows(int r0, int r1, int reset) {
  static uint16_t line[168];                       // the field and the column on its right
  static uint8_t cells[DECOR_W * 64];
  if (reset) { for (int i = 0; i < 256; i++) decor_slot[i] = 0; decor_slots = 0; }
  if (r1 > L->dh) r1 = L->dh;
  for (int ty = r0 * 8; ty < r1 * 8; ty++) {
    int sy = FOUR_TOP + ty * L->en / L->ed;
    if (sy < 224) decor_line(sy + 16, line); else for (int x = 0; x < 168; x++) line[x] = 0xFFFF;
    for (int tx = 0; tx < L->dw * 8; tx++) {
      int sx = L->ox + tx * 16 / L->dn;               // (2 x 2: the field only, as before)
      uint32_t v = sx < (L->ox ? 168 : 152) ? line[sx] : 0xFFFF; uint8_t b = 0;
      if (v != 0xFFFF) {
        uint32_t pal = v >> 4, slot = decor_slot[pal];
        if (!slot && decor_slots < DECOR_SLOTS) { decor_pal_of[decor_slots] = (uint8_t)pal; slot = decor_slot[pal] = ++decor_slots; }
        if (slot) b = (uint8_t)(((slot - 1) << 4) | (v & 15));
      }
      cells[(tx >> 3) * 64 + (ty & 7) * 8 + (tx & 7)] = b;
    }
    if ((ty & 7) == 7) mc_copy((volatile void *)(DECOR_BASE + (ty >> 3) * L->dw * 64), cells, L->dw * 64);
  }
}

// An empty decor (the game clears or moves it away, at the end of a round of players 1 and 2) does
// not replace the one shown. The signature tells a decor from another one (the palettes, heights
// and shrink of its sprites), not where it is nor its tiles: the town scrolls and is animated, and
// drawing it again at every step took 2.8 million cycles every 12 frames; the small decor is a
// still picture of it.
static int decor_empty(void) {                    // one line in 16 of the decor, as it would be drawn:
  static uint16_t line[168];                       // less than a quarter covered is no decor
  uint32_t full = 0, all = 0;
  for (int ty = 0; ty < L->dh * 8; ty += 16) {
    decor_line(FOUR_TOP + ty * L->en / L->ed + 16, line);
    for (int x = 0; x < 152; x++) { all++; full += line[x] != 0xFFFF; }
  }
  return full * 4 < all;
}
// A new decor comes after an empty one (the game clears the decor between two rounds): it is drawn
// at once, by rows, its palettes taken again. The same decor moving (an animated decor: the sun of
// the sunset, the clouds of the beach) is drawn again at most every 2 s, or it would be drawn at
// every frame and leave no time to the game.
static int decor_row = -1, decor_reset, decor_was_empty = 1;
static uint32_t decor_started;
static void decor_force(void) { decor_sign = 1; decor_was_empty = 1; }
static void decor_frame(void) {
  uint32_t h = 2166136261u, shown = 0;
  for (int n = 1; n < LIST_FIRST; n++) {
    h = (h ^ neo_vram[0x8000 + n]) * 16777619u; h = (h ^ (neo_vram[0x8200 + n] & 0x7F)) * 16777619u;
    h = (h ^ (neo_vram[n * 64 + 1] >> 8)) * 16777619u;
    shown += (neo_vram[0x8200 + n] & 0x3F) != 0;
  }
  if (shown < 8) decor_was_empty = 1;              // (almost) no decor sprite: cleared
  if (h != decor_sign && decor_row < 0 && (decor_was_empty || vframe - decor_started >= 120)) {
    decor_sign = h;
    if (decor_empty()) decor_was_empty = 1;
    else {
      decor_row = 0; decor_reset = decor_was_empty; decor_was_empty = 0; decor_started = vframe;
      if (decor_reset) mc_debug("DECOR frame %u: a new decor\n", neo.frame);
    }
  }
  if (decor_row >= 0) {
    decor_rows(decor_row, decor_row + 2, decor_row == 0 && decor_reset);
    decor_row += 2;
    if (decor_row >= L->dh) decor_row = -1;
  }
  const uint16_t *p = neo_pal[neo.pal_bank];        // its palettes, with the fades of the game
  for (int k = 0; k < decor_slots; k++)
    for (int c = 0; c < 16; c++) PALETTE[(DECOR_PAL + k) * 16 + c] = rgb555(p[decor_pal_of[k] * 16 + c]);
}

// The names and scores of the four players, under the fields, on layer 0 with the font of the
// game's fix layer (ASCII codes). Scores: 32-bit binary, players 1 and 2 at $1057DC / $1057E0,
// players 3 and 4 at $10F620 / $10F624 (patches/four.s).
static const uint32_t score_at[4] = {0x1057DC, 0x1057E0, 0x10F620, 0x10F624};
static const uint32_t hud_rgb[4] = {0x78E678, 0x50A8FF, 0xFF6060, 0xFAD23C};   // the colours of the dragons
static void hud_text(int col, int row, const char *t, int pal) {
  for (; *t; t++, col++) TILEMAP(0)[row * 64 + col] = MC_TILE((uint8_t)*t, pal, 0);
}
#define FIX_EMPTY 0xFF                             // the empty tile of the fix layer (the game clears with it)
static void decor_layer_open(void) {             // layer 1: the decor cells behind each of the four fields
  mc_layer(1, MC_CHR(DECOR_BASE - CHRRAM_BASE), MC_TILE8 | MC_8BPP, 64, 64, 0);
  mc_layer_scroll(1, 0, 0);
  mc_fill((volatile void *)(DECOR_BASE + DECOR_W * DECOR_H * 64), 0, 64);   // the empty cell
  for (int i = 0; i < 64 * 64; i++) TILEMAP(1)[i] = MC_TILE(DECOR_W * DECOR_H, DECOR_PAL, 0);
  for (int f = 0; f < 4; f++)
    for (int cy = 0; cy < L->dh; cy++)
      for (int cx = 0; cx < L->dw; cx++)
        if (fon[f]) TILEMAP(1)[(fy[f] / 8 + cy) * 64 + fx[f] / 8 + cx] = MC_TILE(cy * L->dw + cx, DECOR_PAL, 0);
}
// two panels, left and right of the fields (margins of 72 pixels, cells of 8): players 1 and 3 on
// the left, 2 and 4 on the right, each facing its field: name, score, WINNER or OUT
static void hud_open(void) {
  decor_layer_open();
  mc_layer(0, fix_file, MC_TILE8 | MC_4BPP, 64, 64, 3);   // the whole 384 x 288 screen
  mc_layer_scroll(0, 0, 0);
  for (int i = 0; i < 64 * 64; i++) TILEMAP(0)[i] = MC_TILE(FIX_EMPTY, 0, 0);
  for (int k = 0; k < 4; k++) {                    // the font: 1 = the letter, 2 = its box (opaque)
    uint32_t c = hud_rgb[k];
    uint16_t v = (uint16_t)(((c >> 19) << 10) | (((c >> 11) & 31) << 5) | ((c >> 3) & 31));
    for (int i = 1; i < 16; i++) PALETTE[(HUD_PAL + k) * 16 + i] = i == 1 ? v : 0;
    char name[] = "PLAYER 1"; name[7] = (char)('1' + k);
    if (fon[k]) hud_text(fcol[k], frow[k], name, HUD_PAL + k);
  }
}
// WINNER and OUT under the scores (patches/rules.s: OUT bytes at $10F660, the structure of the
// winner at $10F668): the banners of the game are drawn for two players only
static void hud_frame(void) {
  uint32_t w = ram16(0x10F668) << 16 | ram16(0x10F66A), winner = w ? ram16(w + 2) : 99;
  for (int k = 0; k < 4; k++) {
    if (!fon[k]) continue;                         // not present (4 x 1): no column
    int nofield = k >= 2 && !neo_ram[(0xF65C + k - 2) ^ 1];  // no field for player 3 or 4 (yet, or any more)
    int joined = k >= 2 && neo_ram[(0xF614 + k - 2) ^ 1];   // it has joined: a field at the next round
    const char *state = (uint32_t)k == winner ? "WINNER! " : neo_ram[(0xF660 + k) ^ 1] ? "OUT     " :
                        !nofield ? "        " : joined ? "READY   " : (vframe >> 5) & 1 ? "PRESS   " : "  START ";
    hud_text(fcol[k], frow[k] + L->at_state, state, HUD_PAL + k);
    uint32_t won = neo_ram[(0xF6B0 + k) ^ 1];        // rounds won (patches/rules.s, WINS)
    char t[9] = "WINS    ";
    if (won >= 10) t[6] = (char)('0' + won / 10);
    t[7] = (char)('0' + won % 10);
    hud_text(fcol[k], frow[k] + L->at_wins, nofield && !won ? "        " : t, HUD_PAL + k);
  }
  for (int k = 0; k < 4; k++) {
    if (!fon[k]) continue;                         // not present (4 x 1): no column
    uint32_t v = ram16(score_at[k]) << 16 | ram16(score_at[k] + 2);
    char t[9]; t[8] = 0;
    for (int i = 7; i >= 0; i--) { t[i] = (char)(v || i == 7 ? '0' + v % 10 : ' '); v /= 10; }
    hud_text(fcol[k], frow[k] + 2, t, HUD_PAL + k);
  }
}
static void hud_close(void) {                      // the game's fix layer again, and layer 1
  mc_layer(1, spr_file, MC_TILE16 | MC_4BPP, 32, 32, 0);
  mc_layer(0, fix_file, MC_TILE8 | MC_4BPP, 64, 32, 3);
  mc_layer_scroll(0, 0, 16);
  for (uint32_t x = 0; x < 40; x++)
    for (uint32_t y = 0; y < 32; y++) {
      uint32_t e = neo_vram[0x7000 + x * 32 + y];
      TILEMAP(0)[y * 64 + x] = MC_TILE(e & 0xFFF, e >> 12, 0);
    }
  for (uint32_t x = 40; x < 64; x++) for (uint32_t y = 0; y < 32; y++) TILEMAP(0)[y * 64 + x] = MC_TILE(FIX_EMPTY, 0, 0);
}

// The dragons: player 1 has Bub (green, palette 16), players 2, 3 and 4 Bob (blue, palette 21,
// used by the dragon only): the dragon of player 3 takes palette 212, red, of player 4 palette
// 213, yellow: copies of palette 21 made every frame (with the fades of the game) where the blue
// colours only take the hue of the player.
#define DRAGON_PAL  21
#define DRAGON3_PAL 212
#define DRAGON4_PAL 213
static uint16_t hue_to(uint16_t c, int target) {   // RGB555; target hue in 0-1535 (6 x 256)
  int r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
  int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
  int d = mx - mn;
  if (d < 6 || mx != b) return c;                  // greys, whites, blacks and the non-blue colours stay
  int h = 1024 + ((r - g) * 256) / d;              // blue sector (768-1279 around 1024)
  if (h < 768 || h > 1279) return c;
  h = target; int sector = h >> 8, f = h & 255;
  int p = mn, q = mx - (d * f) / 256, t = mn + (d * f) / 256, v = mx;
  switch (sector) {
  case 0: r = v; g = t; b = p; break;  case 1: r = q; g = v; b = p; break;
  case 2: r = p; g = v; b = t; break;  case 3: r = p; g = q; b = v; break;
  case 4: r = t; g = p; b = v; break;  default: r = v; g = p; b = q; break;
  }
  return (uint16_t)((r << 10) | (g << 5) | b);
}
static void dragons_frame(void) {
  const uint16_t *p = neo_pal[neo.pal_bank];
  for (int i = 0; i < 16; i++) {
    uint16_t c = rgb555(p[DRAGON_PAL * 16 + i]);
    PALETTE[DRAGON3_PAL * 16 + i] = hue_to(c, 0);         // red
    PALETTE[DRAGON4_PAL * 16 + i] = hue_to(c, 230);       // yellow
  }
}

static void video_four(void) {
  const uint16_t *smap = (const uint16_t *)spr_map_file;
  uint32_t count = spr_map_size / 2, anim = (neo.frame / ((neo.lspc_mode >> 8) + 1)) & 7;
  int out = 0;
  for (int n = four_last - 1; n >= LIST_FIRST && out < MC_SPRITES; n--) {
    if (!sp_rows[n]) continue;
    int rows = sp_rows[n] > 32 ? 32 : sp_rows[n], zy = sp_zy[n], zx = sp_zx[n];
    for (int k = 0; k < rows && out < MC_SPRITES; k++) {
      int lx, ly, f = four_field(sp_x[n], sp_top[n] + ((k * 16 * (zy + 1)) >> 8), &lx, &ly);
      if (f < 0) continue;
      uint32_t t0 = neo_vram[n * 64 + k * 2], t1 = neo_vram[n * 64 + k * 2 + 1];
      // the little character between the two fields of the Neo Geo screen and the counters of the
      // rounds under it (player 1 against player 2: they mean nothing with four players, and the
      // character fell half in each field): the central band under line 176, the counters
      // (palettes 116, 122) under line 208
      // and the result of the game, drawn for players 1 and 2 (WON, LOST, PLAYER n WON, PASSAGE
      // TIME: palettes 117-119, 121-123, never used by the fields, measured): the panels say it
      { int nx = sp_x[n] & 0x1FF, ny = (sp_top[n] + ((k * 16 * (zy + 1)) >> 8)) & 0x1FF;
        uint32_t pl = t1 >> 8;
        if (ny >= 176 && ny < 240 && ((nx >= 144 && nx < 176) ||
            (ny >= 208 && nx >= 120 && nx < 200 && (pl == 116 || pl == 122)))) continue;
        if ((ny < 240 || ny >= 0x1F0) && pl >= 117 && pl <= 123 && pl != 120) continue; }
      uint32_t num = t0 | ((t1 & 0xF0) << 12);
      if (t1 & 8) num = (num & ~7u) | anim; else if (t1 & 4) num = (num & ~3u) | (anim & 3);
      if (num >= count || smap[num] == 0) continue;
      int slot = cache_get(smap[num]);
      if (slot < 0) { conv_miss++; continue; }
      uint32_t pal = t1 >> 8;
      if (pal == DRAGON_PAL && f >= 2) pal = f == 2 ? DRAGON3_PAL : DRAGON4_PAL;
      sprite_t *s = &mc_spr[out++];
      if (!fon[f]) continue;
      s->x = (int16_t)(fx[f] + (((lx - L->ox) * L->sx) >> 8));
      s->y = (int16_t)(fy[f] + ((ly * L->sy) >> 8));
      s->tile = (uint16_t)(slot * 4);
      s->pal = (uint8_t)pal; s->w = 2; s->h = 2;
      s->attr = (uint8_t)(SA_ON | SA_PRIO(SPR_PRIO) | ((t1 & 1) ? SA_HFLIP : 0) | ((t1 & 2) ? SA_VFLIP : 0));
      s->zoomx = (uint16_t)((((zx + 1) << 4) * L->sx) >> 8);
      s->zoomy = (uint16_t)(((zy + 1) * L->sy) >> 8);
      // 4 x 1: each line crosses the four fields, and the rows of bubbles overlap by 2 of their 16
      // lines: a line could cross two rows in each field, 64 sprites. The bubbles (one tile,
      // palettes 28-36) are 12 lines high there, centred: rows touch, a line crosses one.
      if (four_layout && pal >= 28 && pal <= 36 && zy == 255) { s->zoomy = 192; s->y += 1; }
    }
  }
  if (conv_miss) conv_miss_frames++;
  if (out > conv_out_max) conv_out_max = out;
  if (vframe % 600 == 0) { mc_debug("CONV4 frame %u: sprites up to %u, %u frames lost tiles (cache)\n", vframe, conv_out_max, conv_miss_frames); conv_out_max = 0; conv_miss_frames = 0; }
  conv_miss = 0;
  decor_frame();                                   // the decor, on layer 1
  dragons_frame();
  mc_sprites_used(out);
  mc_copy(SPRITES, mc_spr, (uint32_t)out * sizeof(sprite_t));
  hud_frame();
  DISP_CTRL = DISP_ON | DISP_SPRITES | DISP_LAYER(0) | DISP_LAYER(1);
}

#include "arena.h"

static uint32_t frames_short, tall_left;          // frames where tall columns had to stay sprites
static void video_frame(void) {
  vframe++;
  int four = four_running(), lay = DIPSW & 1;
  if (four && four_mode && lay != four_layout) {     // DIP switch 1 changed during a game
    four_layout = lay; L = &layouts[lay];
    place_fields(); hud_open(); decor_force();
  } else if (four && four_mode && !arena_on && place_fields()) hud_open();   // a player came or went
  if (four != four_mode) {
    four_layout = lay; L = &layouts[lay];
    four_mode = four; mc_video_mode(four ? FOUR_W : 320, four ? FOUR_H : 224);
    if (four) { place_fields(); hud_open(); }
    else { neo_platform_bank(); hud_close(); }      // the palettes 208-255 and the fix layer of the game again
    decor_force();
    mc_debug("FOUR %d frame %u tasks %04X%04X\n", four, neo.frame, ram16(0x1034C6), ram16(0x1034C8));
  }
  // 1. positions, sizes and groups: a sticky sprite continues the chain on its right, and so does a
  //    sprite of the same height and top, without shrink, exactly 16 pixels on the right of the
  //    previous one (backgrounds are drawn both ways)
  int px = 0, ptop = 0, prows = 0, pzy = 0, pw = 0, pzx = 0, head = 1;
  // with four players only the frame list counts (the decor is drawn from the video RAM itself):
  // from sprite 118 to the last one the native sending of the list wrote (neort.c)
  int first = 1, last = 381;
  if (four_mode && !arena_on) { first = LIST_FIRST; last = (int)neo_list_last + 1; if (last > 381) last = 381; if (last < first) last = first; }
  four_last = last;
  for (int n = first; n < last; n++) {
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
  if (four_mode && !arena_on) { video_four(); return; }
  // the arena (arena.h): the game's own decor (sprites 1-117: the stage, its clouds moving) drawn as
  // on the 2-player screen, shifted under the board; the arena's sprites in front
  int ox = arena_on ? AR_DX : 0, oy = arena_on ? AR_DY : 0, nmax = arena_on ? LIST_FIRST : 381;
  // The game's decor is drawn in groups of columns across the screen, back to front: the far
  // background (sky and clouds, scrolling), the landscape, then the frames of the two fields and
  // their floors (tiles common to every stage). In the arena the last group is not drawn: one
  // landscape behind the whole board.
  if (arena_on) {
    int prev = -1000;
    for (int n = 1; n < LIST_FIRST; n++) {
      if (!sp_rows[n] || (neo_vram[0x8200 + n] & 0x40)) continue;   // the head of a column
      int x = sp_x[n] & 0x1FF; if (x >= 0x1E0) x -= 0x200;
      if (x < prev - 64) nmax = n;                 // back to the left: a new group
      prev = x;
    }
  }
  // 2. tall columns without shrink go to layers, the first ones (behind) on layer 3; a column that
  //    does not fit the current layer opens the next one, so the Neo Geo drawing order is kept
  int layer = 4, lx = 0, ly = 0;
  uint32_t used = 0;
  for (int n = 1; n < nmax; n++) {
    if (sp_rows[n] < 8 || sp_zx[n] != 15 || sp_zy[n] != 0xFF) continue;
    int x = sp_x[n] & 0x1FF, top = sp_top[n] & 0x1FF, mx = (x >> 4) & 31;
    if (layer > 3 || (x & 15) != lx || (top & 15) != ly || (used >> mx) & 1) {
      if (layer == (arena_on ? 2 : 1)) { frames_short++; tall_left++; break; }   // no layer left (arena: 1 is its own)
      layer--; lx = x & 15; ly = top & 15; used = 0;
      layer_open(layer, x, top);
      if (arena_on) mc_layer_scroll(layer, -(x & 15) - ox, -(top & 15) - oy);
    }
    used |= 1u << mx; sp_layer[n] = (uint8_t)layer;
    layer_column(layer, n);
  }
  uint32_t disp = DISP_ON | DISP_SPRITES | DISP_LAYER(0) | (arena_on ? DISP_LAYER(1) : 0);   // (arena: 0 its scores, 1 its launchers)
  for (int l = 3; l >= layer && l >= (arena_on ? 2 : 1); l--) disp |= DISP_LAYER(l);
  // 3. the other columns, one sprite per visible tile; the Neo Geo draws higher numbers in front,
  //    MONOCORE lower numbers: go backwards
  const uint16_t *smap = (const uint16_t *)spr_map_file;
  uint32_t count = spr_map_size / 2, anim = (neo.frame / ((neo.lspc_mode >> 8) + 1)) & 7;
  int out = 0;
  if (arena_on) arena_frame(&out);                 // its bubbles, launchers, dragons in front
  for (int n = nmax - 1; n >= 1 && out < MC_SPRITES; n--) {
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
      if (slot < 0) { conv_miss++; continue; }
      sprite_t *s = &mc_spr[out++];
      s->x = (int16_t)(x + ox); s->y = (int16_t)(y + oy); s->tile = (uint16_t)(slot * 4);
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
static void menu_inputs(void);                     // the menu of the title (below)
static void inputs(void) {
  uint32_t a = IN_P1, b = IN_P2;
  neo.p1 = (uint8_t)(a & 0xFF);                     // directions, then buttons 1-4 as A-D
  neo.p2 = (uint8_t)(b & 0xFF);
  neo.p3 = (uint8_t)(IN_P3 & 0xFF);                 // players 3 and 4 (patches/four.s)
  neo.p4 = (uint8_t)(IN_P4 & 0xFF);
  neo.st34 = (uint8_t)(((IN_P3 >> 10) & 1) | (((IN_P4 >> 10) & 1) << 1));   // their starts: they join
  neo.status_b = (uint8_t)(((a & IN_START) ? 1 : 0) | ((b & IN_START) ? 4 : 0));
  menu_inputs();
  if (arena_on) { neo.p1 = neo.p2 = neo.p3 = neo.p4 = 0; neo.status_b = 0; neo.st34 = 0; }   // the arena's
}

// --- the menu of the title: a start on any pad at the title or in the demo (BIOS USER_MODE 1)
// opens it; any pad chooses the number of players (1-4) and the level (EASY, NORMAL, HARD: the
// game's DIFFICULTY setting at $10FD8A, LEVEL 2, 4 or 7 of 8), START or button 1 plays, button 2
// goes back. 1 player: the game's own start of player 1 (against the computer). 2-4 players: the
// start of players 1 and 2 together (the game of two, where the rounds follow one another),
// players 3 and 4 joined as chosen, and the menus of the game (GAME SELECT...) validated with
// button 1 until the first round starts. The game sees no input while the menu is open.
static int menu_open, menu_pad, menu_sel, menu_players = 4, menu_level = 1, menu_drawn, menu_mode, menu_guide;
static uint32_t pad_prev[4], start_force, start_mask, auto_frames;
static const uint8_t level_dip[3] = {1, 3, 6};
static const char *const level_name[3] = {"  EASY  ", " NORMAL ", "  HARD  "};
#define MENU_X 7
#define MENU_Y 8
#define MENU_W 26
#define MENU_H 17
static int menu_drawn_cell(uint32_t x, uint32_t y) {
  return menu_drawn && x >= MENU_X && x < MENU_X + MENU_W && y >= MENU_Y && y < MENU_Y + MENU_H;
}
static void menu_inputs(void) {
  uint32_t in[4] = {IN_P1, IN_P2, IN_P3, IN_P4};
  int title = neo_ram[0xFDAF ^ 1] == 1 && !four_mode;
  if (!title) menu_open = 0;
  if (title && !menu_open && !start_force && !auto_frames)
    for (int k = 0; k < 4; k++)
      if (in[k] & ~pad_prev[k] & IN_START) { menu_open = 2; menu_pad = k; menu_sel = 0; break; }
  if (menu_open == 2) {                            // the start that opened it is not a choice
    menu_open = 1;
    neo.p1 = neo.p2 = neo.p3 = neo.p4 = 0; neo.status_b = 0; neo.st34 = 0;
  } else if (menu_open) {
    uint32_t e = 0;                                // any pad drives the menu
    for (int k = 0; k < 4; k++) e |= in[k] & ~pad_prev[k];
    if ((e & IN_UP) && menu_sel > 0) menu_sel--;
    if ((e & IN_DOWN) && menu_sel < 3) menu_sel++;
    int d = (e & IN_RIGHT) ? 1 : (e & IN_LEFT) ? -1 : 0;
    if (d && menu_sel == 0) menu_mode ^= 1;
    if (d && menu_sel == 1) menu_players = (menu_players + d + 3) % 4 + 1;
    if (d && menu_sel == 2) menu_level = (menu_level + d + 3) % 3;
    if (d && menu_sel == 3) menu_guide ^= 1;
    if (e & IN_B(2)) menu_open = 0;
    else if (e & (IN_START | IN_B(1))) {
      menu_open = 0;
      neo_ram[0xFD8A ^ 1] = level_dip[menu_level];     // DIFFICULTY
      if (menu_mode) {                               // ARENA: a game of two loads the graphics
        arena_pending = 1; arena_wait = 0; arena_players = menu_players; arena_guide = menu_guide;
        neo_ram[0xF614 ^ 1] = neo_ram[0xF615 ^ 1] = 0;
        start_mask = 5; start_force = 8; auto_frames = 1200;
      } else {
        neo_ram[0xF614 ^ 1] = menu_players >= 3;         // JOIN3, JOIN4 (patches/four.s)
        neo_ram[0xF615 ^ 1] = menu_players == 4;
        start_mask = menu_players == 1 ? 1 : 5; start_force = 8;
        auto_frames = menu_players == 1 ? 0 : 1200;
      }
    }
    neo.p1 = neo.p2 = neo.p3 = neo.p4 = 0; neo.status_b = 0; neo.st34 = 0;   // the game sees nothing
  }
  for (int k = 0; k < 4; k++) pad_prev[k] = in[k];
  if (start_force) { start_force--; neo.status_b |= (uint8_t)start_mask; }
  if (auto_frames) {                               // the menus of the game, until the first round
    auto_frames--;
    if (ram16(0x1034C8) & 0x20) auto_frames = 0;   // task 5 runs: the field of player 1
    else if (!start_force && auto_frames % 30 < 4) { neo.p1 |= 0x10; neo.p2 |= 0x10; }
  }
}

static void hint_frame(void) {
  int game = neo_ram[0xFDAF ^ 1] == 2 && !four_mode;   // BIOS USER_MODE 2: a game
  if (!four_mode) { PALETTE[TEXT_PAL * 16 + 1] = (31 << 10) | (28 << 5) | 6; PALETTE[TEXT_PAL * 16 + 2] = 0; }
  if (menu_open) {
    char l[MENU_H][MENU_W + 1];
    for (int r = 0; r < MENU_H; r++) { for (int i = 0; i < MENU_W; i++) l[r][i] = ' '; l[r][MENU_W] = 0; }
    #define PUT(r, c, str) do { const char *q_ = (str); for (int i_ = 0; q_[i_]; i_++) l[r][(c) + i_] = q_[i_]; } while (0)
    PUT(1, 3, "BUST-A-MOVE AGAIN 4P");
    PUT(3, 3, "MODE"); PUT(3, 13, "<"); PUT(3, 14, menu_mode ? " ARENA  " : " VERSUS "); PUT(3, 22, ">");
    PUT(5, 3, "PLAYERS"); PUT(5, 13, "<    >"); l[5][16] = (char)('0' + menu_players);
    PUT(7, 3, "LEVEL"); PUT(7, 13, "<"); PUT(7, 14, level_name[menu_level]); PUT(7, 22, ">");
    PUT(9, 3, "GUIDE"); PUT(9, 13, "<"); PUT(9, 14, menu_guide ? "   ON   " : "   OFF  "); PUT(9, 22, ">");
    if ((vframe >> 4) & 1) l[3 + 2 * menu_sel][1] = '>';   // the line chosen
    PUT(11, 3, "UP/DOWN    : LINE");
    PUT(12, 3, "LEFT/RIGHT : CHANGE");
    PUT(13, 3, "START      : PLAY");
    PUT(14, 3, "BUTTON 2   : BACK");
    #undef PUT
    for (int r = 0; r < MENU_H; r++)
      for (int i = 0; i < MENU_W; i++) TILEMAP(0)[(MENU_Y + r) * 64 + MENU_X + i] = MC_TILE((uint8_t)l[r][i], TEXT_PAL, 0);
    menu_drawn = 1;
  } else if (menu_drawn) { menu_drawn = 0; fix_back(MENU_X, MENU_Y, MENU_W, MENU_H); }
  // the title and the demo (not a game): a box, bottom right, for the menu
  int box = !four_mode && !menu_open && neo_ram[0xFDAF ^ 1] == 1;     // USER_MODE 1: the title, the demo
  if (box) {
    static const char *const lines[5] = {"             ", " 1-4 PLAYERS ", " PRESS START ", "  ANY PAD    ", "             "};
    for (uint32_t r = 0; r < 5; r++)
      for (uint32_t i = 0; i < 13; i++) {
        char ch = lines[r][i];
        if (r == 2 && (vframe >> 5) & 1) ch = ' ';  // PRESS START blinks
        TILEMAP(0)[(BOX_Y + r) * 64 + BOX_X + i] = MC_TILE((uint8_t)ch, TEXT_PAL, 0);
      }
  } else if (box_on) fix_back(BOX_X, BOX_Y, 13, 5);
  box_on = (uint8_t)box;
  for (int k = 0; k < 2; k++) {
    int on = game && neo_ram[(0xF614 + k) ^ 1];      // JOIN3, JOIN4 (patches/four.s)
    uint32_t x0 = k ? 31 : 1;
    if (on) {
      char t[] = "P3 READY"; t[1] = (char)('3' + k);
      for (uint32_t i = 0; i < 8; i++) TILEMAP(0)[HINT_ROW * 64 + x0 + i] = MC_TILE((uint8_t)t[i], TEXT_PAL, 0);
    } else if (hint_on[k] && !four_mode) fix_back(x0, HINT_ROW, 8, 1);   // give the cells back
    hint_on[k] = (uint8_t)on;
  }
}

static uint32_t conv_max, conv_sum;
static void vblank(void) {
  uint32_t t0 = (uint32_t)mc_cycles();
  inputs();
  sound_frame();
  video_frame();
  hint_frame();
  uint32_t t = (uint32_t)mc_cycles() - t0;       // conversion time, reported every 10 s
  conv_sum += t; if (t > conv_max) conv_max = t;
  if (vframe % 600 == 0) {                         // per frame: 833 333 cycles at 50 MHz and 60 Hz
    uint32_t idle = idle_cycles > conv_sum ? idle_cycles - conv_sum : 0;   // the conversion runs while the game waits
    mc_debug("frame %u: conversion %u cycles on average, %u at most; game %u%% of the time, %u of 600 frames on time%s\n",
             vframe, conv_sum / 600, conv_max, 100 - (uint32_t)((uint64_t)idle * 100 / (600ull * CPU_HZ / 60)),
             waits, four_mode ? " (4 players)" : "");
    conv_sum = conv_max = 0; frames_short = 0; idle_cycles = 0; waits = 0;
  }
  if (arena_pending && !arena_on && (ram16(0x1034C8) & 0x20)) {   // the game's round is loaded (decor,
    ar_scan();                                     // palettes): its bubbles noted, then the arena
    if (++arena_wait > 90) { arena_pending = 0; arena_start(); }
  }
  neo_vblank(&cpu);                                // (arena: the game runs on, its fields suspended)
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
