// MONOCORE-1 "mgfx": graphics banks of MONOPIXEL (see mgfx.h and MGFX.md).
#include <string.h>
#include "mc.h"
#include "mgfx.h"

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

void mg_frame_pos(const mg_sprite_t *s, int frame, int *cx, int *cy) {
  int x = s->x, y = s->y, w = s->w ? s->w : 1, h = s->h ? s->h : 1;
  for (int f = 1; f <= frame; f++) {
    x += w;
    if (x + w > MG_SHEET_W) { x = 0; y += h; }
  }
  *cx = x; *cy = y;
}

// ---------------------------------------------------------------- packing
uint32_t mg_pack_bound(uint32_t n) { return n + n / 128 + 2; }
// dst may be NULL: the packed size is then only counted
uint32_t mg_pack(const uint8_t *src, uint32_t n, uint8_t *dst) {
  uint32_t i = 0, o = 0, lit = 0;            // lit: start of the pending literal bytes
#define EMIT(v) do { if (dst) dst[o] = (uint8_t)(v); o++; } while (0)
#define EMITN(p, k) do { if (dst) memcpy(dst + o, (p), (k)); o += (k); } while (0)
  while (i < n) {
    uint32_t r = 1;
    while (i + r < n && r < 130 && src[i + r] == src[i]) r++;
    if (r >= 3) {
      while (lit < i) {                      // the literals first
        uint32_t k = i - lit > 128 ? 128 : i - lit;
        EMIT(k - 1); EMITN(src + lit, k); lit += k;
      }
      EMIT(r + 125); EMIT(src[i]);
      i += r; lit = i;
    } else {
      i += r;
      if (i - lit >= 128) { EMIT(127); EMITN(src + lit, 128); lit += 128; }
    }
  }
  while (lit < n) {
    uint32_t k = n - lit > 128 ? 128 : n - lit;
    EMIT(k - 1); EMITN(src + lit, k); lit += k;
  }
#undef EMIT
#undef EMITN
  return o;
}
int mg_unpack(const uint8_t *src, uint32_t len, volatile uint8_t *dst, uint32_t n) {
  uint32_t i = 0, o = 0;
  while (i < len && o < n) {
    uint32_t c = src[i++];
    if (c < 128) {
      if (i + c + 1 > len || o + c + 1 > n) return -1;
      for (uint32_t k = 0; k <= c; k++) dst[o++] = src[i++];
    } else {
      if (i >= len || o + c - 125 > n) return -1;
      uint8_t v = src[i++];
      for (uint32_t k = 0; k < c - 125; k++) dst[o++] = v;
    }
  }
  return o == n && i == len ? 0 : -1;
}

// ---------------------------------------------------------------- reading a bank
int mg_open(mg_bank_t *b, const void *data, uint32_t max) {
  memset(b, 0, sizeof *b);
  const uint8_t *p = data;
  const mg_header_t *h = data;
  if (max < sizeof *h || h->magic != MG_MAGIC || h->size > max || h->size < sizeof *h) return -1;
  if (h->n_pages < 1 || h->n_pages > MG_PAGES || h->n_pals < 1 || h->n_pals > MG_PALS ||
      h->n_sprites > MG_SPRITES || h->n_maps > MG_MAPS) return -1;
  uint32_t o = sizeof *h, end = h->size;
  b->h = h;
  b->pals = (const uint16_t *)(p + o); o += h->n_pals * 32;
  b->spr = (const mg_sprite_t *)(p + o); o += h->n_sprites * sizeof(mg_sprite_t);
  b->maps = (const mg_map_t *)(p + o); o += h->n_maps * sizeof(mg_map_t);
  if (o + 4 > end) return -1;
  b->sheet_len = rd32(p + o); b->sheet = p + o + 4; o += 4 + b->sheet_len;
  if (o > end) return -1;
  for (int m = 0; m < h->n_maps; m++) {
    const mg_map_t *mp = &b->maps[m];
    if (mp->wcode > 2 || mp->hcode > 2 || MG_MAP_W(mp) * MG_MAP_H(mp) > MG_MAP_CELLS) return -1;
    if (o + 4 > end) return -1;
    b->map_len[m] = rd32(p + o); b->map_data[m] = p + o + 4; o += 4 + b->map_len[m];
    if (o > end) return -1;
  }
  for (int s = 0; s < h->n_sprites; s++) {
    const mg_sprite_t *sp = &b->spr[s];
    if (sp->w < 1 || sp->w > 8 || sp->h < 1 || sp->h > 8 || sp->x >= MG_SHEET_W || sp->frames < 1 || sp->frames > MG_FRAMES) return -1;
  }
  b->chr = b->chr16 = b->chr_spr = 0xFFFFFFFFu;
  return 0;
}
int mg_check(const void *data, uint32_t max) { mg_bank_t b; return mg_open(&b, data, max); }
const char *mg_name(const mg_bank_t *b) {
  static char s[17];
  memcpy(s, b->h->name, 16); s[16] = 0;
  return s;
}

void mg_make_tiles16(volatile const uint8_t *sheet, volatile uint8_t *out, int count) {
  volatile const uint32_t *src = (volatile const uint32_t *)sheet;
  volatile uint32_t *dst = (volatile uint32_t *)out;
  for (int t = 0; t < count; t++) {
    int cx = (t % 8) * 2, cy = (t / 8) * 2;
    for (int r = 0; r < 16; r++) {
      int cell = (cy + (r >> 3)) * MG_SHEET_W + cx, row = r & 7;
      dst[t * 32 + r * 2] = src[cell * 8 + row];            // left half: 8 pixels = one word
      dst[t * 32 + r * 2 + 1] = src[(cell + 1) * 8 + row];
    }
  }
}

static uint32_t sprite_cells(const mg_bank_t *b) {
  uint32_t n = 0;
  for (int s = 0; s < b->h->n_sprites; s++) n += b->spr[s].w * b->spr[s].h * b->spr[s].frames;
  return n;
}
uint32_t mg_load_size(const mg_bank_t *b, int what) {
  uint32_t pages = b->h->n_pages, n = pages * MG_PAGE_BYTES;
  if (what & MG_LOAD_TILES16) n += pages * 64 * 128;
  if (what & MG_LOAD_SPRITES) n += sprite_cells(b) * MG_CELL_BYTES;
  return n;
}
void mg_palettes(const mg_bank_t *b, int first_pal) {
  for (int i = 0; i < b->h->n_pals * 16; i++) {
    uint16_t c = (uint16_t)(((const uint8_t *)b->pals)[2 * i] | (((const uint8_t *)b->pals)[2 * i + 1] << 8));
    PALETTE[((first_pal * 16) + i) & 0xFFF] = c;
  }
}
uint32_t mg_load(mg_bank_t *b, uint32_t off, int first_pal, int what) {
  off = (off + 31) & ~31u;
  if (off + mg_load_size(b, what) > MC_CHRRAM_SIZE) return 0;
  uint32_t pages = b->h->n_pages;
  volatile uint8_t *sheet = MC_CHRRAM + off;
  if (mg_unpack(b->sheet, b->sheet_len, sheet, pages * MG_PAGE_BYTES)) return 0;
  b->chr = off; off += pages * MG_PAGE_BYTES;
  b->pal = first_pal;
  mg_palettes(b, first_pal);
  if (what & MG_LOAD_TILES16) {
    b->chr16 = off;
    mg_make_tiles16(sheet, MC_CHRRAM + off, (int)pages * 64);
    off += pages * 64 * 128;
  }
  if (what & MG_LOAD_SPRITES) {
    b->chr_spr = off;
    for (int s = 0; s < b->h->n_sprites; s++) {
      const mg_sprite_t *sp = &b->spr[s];
      b->cell[s] = (uint16_t)(off / MG_CELL_BYTES);
      for (int f = 0; f < sp->frames; f++) {
        int cx, cy;
        mg_frame_pos(sp, f, &cx, &cy);
        for (int y = 0; y < sp->h; y++) {
          uint32_t cell = (uint32_t)(cy + y) * MG_SHEET_W + cx;
          if (cell + sp->w <= pages * MG_PAGE_CELLS) mc_copy(MC_CHRRAM + off, (const void *)(sheet + cell * MG_CELL_BYTES), sp->w * MG_CELL_BYTES);
          else mc_fill(MC_CHRRAM + off, 0, sp->w * MG_CELL_BYTES);   // outside the sheet: empty
          off += sp->w * MG_CELL_BYTES;
        }
      }
    }
    mc_sprite_sheet(MC_CHR(0));
  }
  return off;
}

// ---------------------------------------------------------------- sprites
int mg_sprite_count(const mg_bank_t *b) { return b->h->n_sprites; }
const mg_sprite_t *mg_sprite_def(const mg_bank_t *b, int s) { return s >= 0 && s < b->h->n_sprites ? &b->spr[s] : 0; }
int mg_sprite_find(const mg_bank_t *b, const char *name) {
  for (int s = 0; s < b->h->n_sprites; s++) {
    int i = 0;
    for (; i < 8; i++) {
      if (name[i] != b->spr[s].name[i]) break;
      if (!name[i]) return s;
    }
    if (i == 8 && !name[8]) return s;
  }
  return -1;
}
int mg_sprite_cell(const mg_bank_t *b, int s, int frame) {
  const mg_sprite_t *sp = mg_sprite_def(b, s);
  if (!sp) return 0;
  frame = frame < 0 ? 0 : frame >= sp->frames ? sp->frames - 1 : frame;
  return b->cell[s] + frame * sp->w * sp->h;
}
int mg_sprite_frame(const mg_sprite_t *sp, uint32_t t) {
  if (!sp || sp->frames < 2 || !sp->speed) return 0;
  uint32_t k = t / sp->speed;
  if (sp->flags & MG_SF_PINGPONG) {
    uint32_t period = 2 * (sp->frames - 1);
    k %= period;
    return (int)(k < sp->frames ? k : period - k);
  }
  return (int)(k % sp->frames);
}
int mg_frame_at(const mg_bank_t *b, int s, uint32_t t) { return mg_sprite_frame(mg_sprite_def(b, s), t); }
void mg_sprite(const mg_bank_t *b, int hw, int s, int frame, int x, int y, int prio) {
  const mg_sprite_t *sp = mg_sprite_def(b, s);
  if (!sp) { mc_sprite_hide(hw); return; }
  mc_sprite(hw, x, y, mg_sprite_cell(b, s, frame), sp->w, sp->h, b->pal + sp->pal, prio);
  mc_sprite_zoom(hw, 0x100, 0x100);
}
int mg_sprite_w(const mg_bank_t *b, int s) { const mg_sprite_t *sp = mg_sprite_def(b, s); return sp ? sp->w * 8 : 0; }
int mg_sprite_h(const mg_bank_t *b, int s) { const mg_sprite_t *sp = mg_sprite_def(b, s); return sp ? sp->h * 8 : 0; }

// ---------------------------------------------------------------- maps
int mg_map_count(const mg_bank_t *b) { return b->h->n_maps; }
int mg_map_w(const mg_bank_t *b, int m) { return m >= 0 && m < b->h->n_maps ? MG_MAP_W(&b->maps[m]) : 0; }
int mg_map_h(const mg_bank_t *b, int m) { return m >= 0 && m < b->h->n_maps ? MG_MAP_H(&b->maps[m]) : 0; }
int mg_map_tile(const mg_bank_t *b, int m) { return m >= 0 && m < b->h->n_maps && b->maps[m].tile16 ? 16 : 8; }
int mg_map_read(const mg_bank_t *b, int m, uint16_t *out) {
  if (m < 0 || m >= b->h->n_maps) return -1;
  return mg_unpack(b->map_data[m], b->map_len[m], (volatile uint8_t *)out, (uint32_t)mg_map_w(b, m) * mg_map_h(b, m) * 2);
}
int mg_map_layer(const mg_bank_t *b, int m, int n, int prio) {
  if (m < 0 || m >= b->h->n_maps || b->chr == 0xFFFFFFFFu) return -1;
  const mg_map_t *mp = &b->maps[m];
  if (mp->tile16 && b->chr16 == 0xFFFFFFFFu) return -1;   // loaded without MG_LOAD_TILES16
  int w = MG_MAP_W(mp), h = MG_MAP_H(mp), cells = w * h;
  n &= 3;
  mc_layer(n, MC_CHR(mp->tile16 ? b->chr16 : b->chr), (mp->tile16 ? MC_TILE16 : MC_TILE8) | MC_4BPP, w, h, prio);
  // the u16 entries are unpacked at the end of the tilemap, then widened in place from the start
  // (entry i is written at 4i, read at 16384 - 2 cells + 2i: always ahead, for 4096 cells at most)
  volatile uint8_t *tm = (volatile uint8_t *)TILEMAP(n);
  uint32_t at = 0x4000 - 2 * (uint32_t)cells;
  if (mg_unpack(b->map_data[m], b->map_len[m], tm + at, 2 * (uint32_t)cells)) return -1;
  volatile uint32_t *e32 = TILEMAP(n);
  for (int i = 0; i < cells; i++) {
    uint32_t e = tm[at + 2 * i] | (tm[at + 2 * i + 1] << 8);
    uint32_t hw = TILE(MG_E_TILE(e), (b->pal + MG_E_PAL(e)) & 0xFF);
    if (MG_E_HFLIP(e)) hw |= T_HFLIP;
    if (MG_E_VFLIP(e)) hw |= T_VFLIP;
    e32[i] = hw;
  }
  for (int i = cells; i < 4096; i++) e32[i] = 0;
  return 0;
}
static void layer_geom(int n, int *tsh, int *w, int *h) {
  uint32_t c = LAYER_CTRL(n & 3);
  *tsh = (c & LC_TILE16) ? 4 : 3;
  *w = 32 << ((c >> 2) & 3); *h = 32 << ((c >> 4) & 3);
}
uint32_t mg_layer_entry(int n, int tx, int ty) {
  int sh, w, h;
  layer_geom(n, &sh, &w, &h);
  return TILEMAP(n & 3)[(ty & (h - 1)) * w + (tx & (w - 1))];
}
void mg_layer_set(int n, int tx, int ty, uint32_t e) {
  int sh, w, h;
  layer_geom(n, &sh, &w, &h);
  TILEMAP(n & 3)[(ty & (h - 1)) * w + (tx & (w - 1))] = e;
}
int mg_tile_at(int n, int px, int py) {
  int sh, w, h;
  layer_geom(n, &sh, &w, &h);
  return (int)(mg_layer_entry(n, px >> sh, py >> sh) & 0xFFFF);
}
