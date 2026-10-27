// MONOPIXEL: the graphics editor of MONOCORE-1 (the companion of MONOTRACKER), mouse and keyboard.
//
// It edits a bank of the mgfx module (sdk/mgfx.h): a sheet of 8 x 8 cells in 4 bpp (4 pages of
// 128 x 128 pixels), 16 palettes of 16 colours, 64 sprites (a rectangle of the sheet, a palette, an
// animation) and 4 maps. Banks are kept in the NVRAM (8 slots) and exported for the games with
// tools/mgfx/mgfx.py; the games show them with the same mgfx module. The interface is the ui480
// module (640 x 480 interlaced), in the manner of Deluxe Paint on the Amiga.
//
// CHR RAM: the sheet being edited (0x00000, 32 KB), the 16 x 16 tiles made from it (0x08000), the
// copies shown by the sprites (canvas, preview: 0x10000), the checkerboard (0x11000), ui480 (0x1A000).
#include <stdio.h>
#include <string.h>
#include "mc.h"
#include "mgfx.h"
#include "ui480.h"

#define SHEET_OFF   0x00000u
#define T16_OFF     0x08000u
#define CANVAS_OFF  0x10000u
#define PREVIEW_OFF 0x10800u
#define MISC_OFF    0x11000u
#define UI_OFF      0x1A000u
#define SHEET ((volatile uint8_t *)(CHRRAM_BASE + SHEET_OFF))
#define CELL(off) ((int)((off) / 32))

// hardware palettes: swatches 32-47 (colour 3), grid 48, selection 49, checkerboard 50, hover 51,
// the bank 64-79
#define P_SWATCH 32
#define P_GRID 48
#define P_SEL 49
#define P_CHECK 50
#define P_HOVER 51
#define P_BANK 64
// glyphs of the program
#define G_GRID UI_G_USER          // 27 glyphs: vertical line (none/pixel/cell) x top x middle
#define G_TRANSP (UI_G_USER + 27) // the transparent colour: a checkerboard
#define G_MASK_LOW (UI_G_USER + 28)  // the lower 6 lines in colour 3 (hide the half rows around the map view)
#define G_MASK_HIGH (UI_G_USER + 29) // the upper 6 lines
#define P_MASK 52

static const uint8_t default_pal[16][3] = {
  { 0, 0, 0 }, { 16, 14, 24 }, { 72, 52, 40 }, { 140, 96, 60 }, { 220, 170, 110 }, { 34, 70, 52 }, { 60, 140, 70 },
  { 140, 210, 90 }, { 36, 44, 100 }, { 60, 110, 200 }, { 120, 200, 240 }, { 245, 245, 240 }, { 150, 150, 165 },
  { 85, 85, 105 }, { 250, 210, 70 }, { 220, 70, 60 } };

// ================================================================ the bank being edited
static char bank_name[17];
static uint16_t pals[MG_PALS][16];
static mg_sprite_t spr[MG_SPRITES];          // empty when name[0] == 0
static uint16_t mapc[MG_MAPS][MG_MAP_CELLS];
static uint8_t map_wc[MG_MAPS], map_hc[MG_MAPS], map_t16[MG_MAPS];
static int bank_slot = -1, dirty;

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int strnlen(const char *s, int max) { int n = 0; while (n < max && s[n]) n++; return n; }
static int map_w(int m) { return 32 << map_wc[m]; }
static int map_h(int m) { return 32 << map_hc[m]; }
static uint16_t default_colour(int i) { return i ? RGB555(default_pal[i][0], default_pal[i][1], default_pal[i][2]) : 0; }
static bool pal_is_default(int p) {
  for (int i = 1; i < 16; i++) if (pals[p][i] != default_colour(i)) return false;
  return true;
}

// pixels of the sheet, in the CHR RAM (x 0-127, y 0-511)
static uint32_t px_addr(int x, int y) { return (uint32_t)(((y >> 3) * MG_SHEET_W + (x >> 3)) * 32 + (y & 7) * 4 + ((x & 7) >> 1)); }
static int get_px(int x, int y) { uint8_t b = SHEET[px_addr(x, y)]; return (x & 1) ? b >> 4 : b & 15; }
static void set_px(int x, int y, int c) {
  volatile uint8_t *p = &SHEET[px_addr(x, y)];
  *p = (uint8_t)((x & 1) ? (*p & 0x0F) | (c << 4) : (*p & 0xF0) | c);
}

// ================================================================ editor state
enum { PG_DRAW, PG_SPR, PG_MAP, PG_BANK, PG_HELP };
static int page, help_back;
// drawing
enum { T_PEN, T_LINE, T_RECT, T_BOX, T_FILL, T_PICK, T_COUNT };
static const char *const tool_name[T_COUNT] = { "CRAYON", "TRAIT", "CADRE", "PAVE", "SEAU", "PIPETTE" };
static int sheet_page, rx, ry = 0, rw = 2, rh = 2;   // the region being drawn, in cells (ry from the top of the sheet)
static int pal_sel, col_a = 1, col_b, tool, grid = 1;
static int stroke, sx, sy, lx, ly;             // stroke: 0 none, 1 left button, 2 right; start, last pixel
// sprites
static int spr_sel, spr_scroll;
// maps
enum { M_PUT, M_BOX, M_FILL, M_PICK, M_COUNT };
static const char *const mtool_name[M_COUNT] = { "POSER", "PAVE", "SEAU", "PIPETTE" };
static int map_sel, mview_x, mview_y, mtool, brush = 1, brush_w = 1, brush_h = 1, brush_hf, brush_vf;
static int mstroke, msx, msy, mlx, mly, pick_drag, pick_x0, pick_y0, mid_drag, mid_x, mid_y;
// bank page
static int slot_sel;
static int naming;                             // editing a name: 1 sprite, 2 bank
static uint32_t frame_count;

static int region_zoom(void) { int m = rw > rh ? rw : rh; return 32 / m; }
static int canvas_x(void) { return 16 + (256 - rw * 8 * region_zoom()) / 2; }
static int canvas_y(void) { return 48 + (256 - rh * 8 * region_zoom()) / 2; }

// ================================================================ undo
static uint8_t undo_px[64 * 32];               // the cells of the region before the last change
static int undo_rx, undo_ry, undo_rw, undo_rh, undo_px_ok;
static uint16_t undo_map[MG_MAP_CELLS];
static int undo_map_ok, undo_map_m, undo_map_wc, undo_map_hc, undo_kind;   // 1 pixels, 2 map

static void cells_read(uint8_t *dst, int x, int y, int w, int h) {
  for (int cy = 0; cy < h; cy++) mc_copy(dst + cy * w * 32, (const void *)(CHRRAM_BASE + SHEET_OFF + ((y + cy) * MG_SHEET_W + x) * 32), w * 32);
}
static void cells_write(const uint8_t *src, int x, int y, int w, int h) {
  for (int cy = 0; cy < h; cy++) mc_copy(SHEET + ((y + cy) * MG_SHEET_W + x) * 32, src + cy * w * 32, w * 32);
}
static void px_snapshot(void) {
  cells_read(undo_px, rx, ry, rw, rh);
  undo_rx = rx; undo_ry = ry; undo_rw = rw; undo_rh = rh; undo_px_ok = 1; undo_kind = 1;
}
static void px_preview_restore(void) { cells_write(undo_px, undo_rx, undo_ry, undo_rw, undo_rh); }
static void map_snapshot(void) {
  memcpy(undo_map, mapc[map_sel], sizeof undo_map);
  undo_map_m = map_sel; undo_map_wc = map_wc[map_sel]; undo_map_hc = map_hc[map_sel]; undo_map_ok = 1; undo_kind = 2;
}
static uint8_t tmp_cells[64 * 32];
static void undo(void) {
  if (undo_kind == 1 && undo_px_ok) {          // swaps, so a second undo redoes
    cells_read(tmp_cells, undo_rx, undo_ry, undo_rw, undo_rh);
    cells_write(undo_px, undo_rx, undo_ry, undo_rw, undo_rh);
    memcpy(undo_px, tmp_cells, sizeof undo_px);
    ui_message("DESSIN : ANNULE (CTRL+Z ENCORE : REFAIT)");
  } else if (undo_kind == 2 && undo_map_ok) {
    int m = undo_map_m, wc = map_wc[m], hc = map_hc[m];
    static uint16_t t[MG_MAP_CELLS];
    memcpy(t, mapc[m], sizeof t);
    memcpy(mapc[m], undo_map, sizeof t);
    memcpy(undo_map, t, sizeof t);
    map_wc[m] = (uint8_t)undo_map_wc; map_hc[m] = (uint8_t)undo_map_hc; undo_map_wc = wc; undo_map_hc = hc;
    ui_message("DECOR : ANNULE (CTRL+Z ENCORE : REFAIT)");
  } else { ui_message("RIEN A ANNULER"); return; }
  dirty = 1;
}

// ================================================================ NVRAM: 8 banks, packed
// "MGNV", u8 layout, u8 last slot, 10 bytes free, then 8 x {u32 offset, u32 size}, banks from 80
#define NV MC_NVRAM
#define NV_SLOTS 8
#define NV_DATA 80
static uint32_t nv32(uint32_t a) { return NV[a] | (NV[a + 1] << 8) | (NV[a + 2] << 16) | ((uint32_t)NV[a + 3] << 24); }
static void nv32w(uint32_t a, uint32_t v) { for (int i = 0; i < 4; i++) NV[a + i] = (uint8_t)(v >> (8 * i)); }
static uint32_t slot_off(int k) { return nv32(16 + 8 * k); }
static uint32_t slot_size(int k) { return nv32(20 + 8 * k); }
static uint32_t align4(uint32_t v) { return (v + 3) & ~3u; }
static const void *slot_data(int k) { return (const void *)(NVRAM_BASE + slot_off(k)); }
static void nv_check(void) {
  if (NV[0] == 'M' && NV[1] == 'G' && NV[2] == 'N' && NV[3] == 'V') {
    for (int k = 0; k < NV_SLOTS; k++)
      if (slot_size(k) && (slot_off(k) + slot_size(k) > MC_NVRAM_SIZE || mg_check(slot_data(k), slot_size(k)))) { nv32w(16 + 8 * k, 0); nv32w(20 + 8 * k, 0); }
    ui_layout = NV[4] & 1;
    return;
  }
  for (uint32_t i = 0; i < NV_DATA; i++) NV[i] = 0;
  NV[0] = 'M'; NV[1] = 'G'; NV[2] = 'N'; NV[3] = 'V'; NV[5] = 0xFF;
}
static uint32_t nv_end(void) {
  uint32_t e = NV_DATA;
  for (int k = 0; k < NV_SLOTS; k++) if (slot_size(k)) { uint32_t f = slot_off(k) + align4(slot_size(k)); if (f > e) e = f; }
  return e;
}
static void nv_delete(int k) {
  uint32_t off = slot_off(k), size = align4(slot_size(k)), end = nv_end();
  if (!slot_size(k)) return;
  for (uint32_t i = off; i + size < end; i++) NV[i] = NV[i + size];
  for (int j = 0; j < NV_SLOTS; j++) if (slot_size(j) && slot_off(j) > off) nv32w(16 + 8 * j, slot_off(j) - size);
  nv32w(16 + 8 * k, 0); nv32w(20 + 8 * k, 0);
}
static uint32_t nv_free(int replacing) {
  return MC_NVRAM_SIZE - nv_end() + (replacing >= 0 ? align4(slot_size(replacing)) : 0);
}

// ================================================================ bank: encode, load
static int pages_used(void) {
  int n = 1;
  for (int p = 0; p < MG_PAGES; p++) {
    volatile const uint32_t *w = (volatile const uint32_t *)(CHRRAM_BASE + SHEET_OFF + p * MG_PAGE_BYTES);
    for (int i = 0; i < MG_PAGE_BYTES / 4; i++) if (w[i]) { n = p + 1; break; }
  }
  return n;
}
static int sprites_used(void) { int n = 0; for (int s = 0; s < MG_SPRITES; s++) if (spr[s].name[0]) n = s + 1; return n; }
static bool map_empty(int m) { for (int i = 0; i < map_w(m) * map_h(m); i++) if (mapc[m][i]) return false; return true; }
static int maps_used(void) { int n = 0; for (int m = 0; m < MG_MAPS; m++) if (!map_empty(m)) n = m + 1; return n; }
static int pals_used(int ns, int nm) {
  int n = 1;
  for (int p = 0; p < MG_PALS; p++) if (!pal_is_default(p)) n = p + 1;
  for (int s = 0; s < ns; s++) if (spr[s].name[0] && spr[s].pal + 1 > n) n = spr[s].pal + 1;
  for (int m = 0; m < nm; m++)
    for (int i = 0; i < map_w(m) * map_h(m); i++) if (mapc[m][i] && MG_E_PAL(mapc[m][i]) + 1 > n) n = MG_E_PAL(mapc[m][i]) + 1;
  return n;
}
static void put32(uint8_t *d, uint32_t v) { for (int i = 0; i < 4; i++) d[i] = (uint8_t)(v >> (8 * i)); }
// the whole bank at dst (NULL: only its size)
static uint32_t bank_encode(uint8_t *dst) {
  int np = pages_used(), ns = sprites_used(), nm = maps_used(), npal = pals_used(ns, nm);
  mg_header_t h;
  memset(&h, 0, sizeof h);
  h.magic = MG_MAGIC; h.n_pages = (uint8_t)np; h.n_pals = (uint8_t)npal; h.n_sprites = (uint8_t)ns; h.n_maps = (uint8_t)nm;
  h.version = 1;
  memcpy(h.name, bank_name, 16);
  uint32_t o = sizeof h;
  if (dst) memcpy(dst + o, pals, (uint32_t)npal * 32);
  o += (uint32_t)npal * 32;
  for (int s = 0; s < ns; s++) {
    mg_sprite_t e = spr[s];
    if (!e.name[0]) { memset(&e, 0, sizeof e); e.w = e.h = e.frames = 1; }   // a hole: valid, but nameless
    if (dst) memcpy(dst + o, &e, sizeof e);
    o += sizeof e;
  }
  for (int m = 0; m < nm; m++) {
    mg_map_t mh;
    memset(&mh, 0, sizeof mh);
    mh.wcode = map_wc[m]; mh.hcode = map_hc[m]; mh.tile16 = map_t16[m];
    if (dst) memcpy(dst + o, &mh, sizeof mh);
    o += sizeof mh;
  }
  uint32_t n = mg_pack((const uint8_t *)(CHRRAM_BASE + SHEET_OFF), (uint32_t)np * MG_PAGE_BYTES, dst ? dst + o + 4 : 0);
  if (dst) put32(dst + o, n);
  o += 4 + n;
  for (int m = 0; m < nm; m++) {
    n = mg_pack((const uint8_t *)mapc[m], (uint32_t)map_w(m) * map_h(m) * 2, dst ? dst + o + 4 : 0);
    if (dst) put32(dst + o, n);
    o += 4 + n;
  }
  h.size = o;
  if (dst) memcpy(dst, &h, sizeof h);
  return o;
}
static void bank_clear(void) {
  mc_fill(SHEET, 0, MG_PAGES * MG_PAGE_BYTES);
  for (int p = 0; p < MG_PALS; p++) for (int i = 0; i < 16; i++) pals[p][i] = default_colour(i);
  memset(spr, 0, sizeof spr);
  memset(mapc, 0, sizeof mapc);
  for (int m = 0; m < MG_MAPS; m++) { map_wc[m] = 1; map_hc[m] = 0; map_t16[m] = 1; }   // 64 x 32 tiles of 16
  memset(bank_name, 0, sizeof bank_name);
  undo_px_ok = undo_map_ok = undo_kind = 0;
  mview_x = mview_y = 0;
}
static void bank_new(void) { bank_clear(); strcpy(bank_name, "SANS NOM"); bank_slot = -1; dirty = 0; }
static bool bank_load(const void *data, uint32_t max) {
  mg_bank_t b;
  if (mg_open(&b, data, max)) { ui_message("BANQUE ILLISIBLE"); return false; }
  bank_clear();
  if (mg_unpack(b.sheet, b.sheet_len, SHEET, (uint32_t)b.h->n_pages * MG_PAGE_BYTES)) { ui_message("PLANCHE ILLISIBLE"); return false; }
  for (int p = 0; p < b.h->n_pals; p++) for (int i = 0; i < 16; i++) {
    const uint8_t *c = (const uint8_t *)b.pals + (p * 16 + i) * 2;
    pals[p][i] = (uint16_t)(c[0] | (c[1] << 8));
  }
  for (int s = 0; s < b.h->n_sprites; s++) spr[s] = b.spr[s];
  for (int m = 0; m < b.h->n_maps; m++) {
    map_wc[m] = b.maps[m].wcode; map_hc[m] = b.maps[m].hcode; map_t16[m] = b.maps[m].tile16 ? 1 : 0;
    mg_map_read(&b, m, mapc[m]);
  }
  memcpy(bank_name, b.h->name, 16); bank_name[16] = 0;
  dirty = 0;
  return true;
}
static void bank_save(int k) {
  uint32_t n = bank_encode(0);
  char s[UI_COLS + 1];
  if (n > nv_free(k)) { snprintf(s, sizeof s, "NVRAM PLEINE (%u OCTETS A TROUVER)", (unsigned)(n - nv_free(k))); ui_message(s); return; }
  nv_delete(k);
  uint32_t off = nv_end();
  bank_encode((uint8_t *)(NVRAM_BASE + off));
  nv32w(16 + 8 * k, off); nv32w(20 + 8 * k, n);
  NV[5] = (uint8_t)k;
  bank_slot = k; dirty = 0;
  snprintf(s, sizeof s, "SAUVE EN %d (%u O) - L'OSD L'ECRIT SUR LA SD", k + 1, (unsigned)n);
  ui_message(s);
}
static void quick_save(void) {
  int k = bank_slot;
  if (k < 0) for (int i = 0; i < NV_SLOTS; i++) if (!slot_size(i)) { k = i; break; }
  if (k < 0) ui_message("NVRAM : AUCUN EMPLACEMENT LIBRE (PAGE BANQUE)"); else bank_save(k);
}

// ================================================================ drawing tools
static int reg_w(void) { return rw * 8; }
static int reg_h(void) { return rh * 8; }
static void plot(int x, int y, int c) {        // x, y in the region
  if (x < 0 || y < 0 || x >= reg_w() || y >= reg_h()) return;
  set_px(rx * 8 + x, ry * 8 + y, c);
}
static int peek(int x, int y) { return get_px(rx * 8 + x, ry * 8 + y); }
static void draw_line(int x0, int y0, int x1, int y1, int c) {
  int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
  int stx = x0 < x1 ? 1 : -1, sty = y0 < y1 ? 1 : -1, e = dx + dy;
  for (;;) {
    plot(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * e;
    if (e2 >= dy) { e += dy; x0 += stx; }
    if (e2 <= dx) { e += dx; y0 += sty; }
  }
}
static void rect(int x0, int y0, int x1, int y1, int c, bool full) {
  if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
  if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++)
      if (full || y == y0 || y == y1 || x == x0 || x == x1) plot(x, y, c);
}
static uint16_t fill_stack[64 * 64];
static void flood(int x, int y, int c) {
  int old = peek(x, y), w = reg_w(), h = reg_h(), n = 0;
  if (old == c) return;
  fill_stack[n++] = (uint16_t)(y * 64 + x);
  while (n) {
    int p = fill_stack[--n], px = p & 63, py = p >> 6;
    if (peek(px, py) != old) continue;
    plot(px, py, c);
    if (px > 0 && peek(px - 1, py) == old && n < 4096) fill_stack[n++] = (uint16_t)(p - 1);
    if (px < w - 1 && peek(px + 1, py) == old && n < 4096) fill_stack[n++] = (uint16_t)(p + 1);
    if (py > 0 && peek(px, py - 1) == old && n < 4096) fill_stack[n++] = (uint16_t)(p - 64);
    if (py < h - 1 && peek(px, py + 1) == old && n < 4096) fill_stack[n++] = (uint16_t)(p + 64);
  }
}
static uint8_t tmp[64 * 64], clip[64 * 64];
static int clip_w, clip_h;
static void region_get(uint8_t *b) { for (int y = 0; y < reg_h(); y++) for (int x = 0; x < reg_w(); x++) b[y * 64 + x] = (uint8_t)peek(x, y); }
static void region_op(int op) {                // 0 mirror H, 1 mirror V, 2 rotate, 3 clear, 4-7 shift L R U D
  if (op == 2 && rw != rh) { ui_message("PIVOTER : ZONE CARREE SEULEMENT"); return; }
  px_snapshot();
  region_get(tmp);
  int w = reg_w(), h = reg_h();
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      int c = 0;
      switch (op) {
        case 0: c = tmp[y * 64 + (w - 1 - x)]; break;
        case 1: c = tmp[(h - 1 - y) * 64 + x]; break;
        case 2: c = tmp[(w - 1 - x) * 64 + y]; break;       // a quarter turn clockwise
        case 4: c = tmp[y * 64 + (x + 1) % w]; break;
        case 5: c = tmp[y * 64 + (x + w - 1) % w]; break;
        case 6: c = tmp[((y + 1) % h) * 64 + x]; break;
        case 7: c = tmp[((y + h - 1) % h) * 64 + x]; break;
      }
      plot(x, y, c);
    }
  dirty = 1;
}
static void region_copy(void) {
  region_get(clip); clip_w = reg_w(); clip_h = reg_h();
  ui_message("ZONE COPIEE (CTRL+V POUR LA COLLER)");
}
static void region_paste(void) {
  if (!clip_w) { ui_message("RIEN A COLLER"); return; }
  px_snapshot();
  for (int y = 0; y < clip_h && y < reg_h(); y++)
    for (int x = 0; x < clip_w && x < reg_w(); x++) plot(x, y, clip[y * 64 + x]);
  dirty = 1;
}
static void set_region(int x, int y, int w, int h) {   // keeps it inside the sheet and inside one page
  rw = w; rh = h;
  rx = clampi(x, 0, MG_SHEET_W - rw);
  int pg = clampi(y / 16, 0, MG_PAGES - 1);
  ry = pg * 16 + clampi(y - pg * 16, 0, 16 - rh);
  sheet_page = pg;
}
static int next_size(int v, int d) {           // 1, 2, 4, 8 cells
  static const int s[4] = { 1, 2, 4, 8 };
  int i = 0;
  while (i < 3 && s[i] < v) i++;
  return s[clampi(i + d, 0, 3)];
}

// mouse on the canvas: a pixel of the region, or -1
static bool canvas_pixel(int mx, int my, int *x, int *y) {
  int z = region_zoom(), cx = canvas_x(), cy = canvas_y();
  if (mx < cx || my < cy || mx >= cx + reg_w() * z || my >= cy + reg_h() * z) return false;
  *x = (mx - cx) / z; *y = (my - cy) / z;
  return true;
}
static void stroke_draw(int x, int y) {
  int c = stroke == 1 ? col_a : col_b;
  switch (tool) {
    case T_PEN: draw_line(lx, ly, x, y, c); break;
    case T_LINE: px_preview_restore(); draw_line(sx, sy, x, y, c); break;
    case T_RECT: px_preview_restore(); rect(sx, sy, x, y, c, false); break;
    case T_BOX: px_preview_restore(); rect(sx, sy, x, y, c, true); break;
  }
  lx = x; ly = y;
}
static void canvas_mouse(int mx, int my) {
  int x, y;
  uint32_t pr = mc_mouse_pressed(), held = mc_mouse_buttons();
  bool in = canvas_pixel(mx, my, &x, &y);
  if (!stroke && in && (pr & (MOUSE_LEFT | MOUSE_RIGHT))) {
    int b = (pr & MOUSE_LEFT) ? 1 : 2, c = b == 1 ? col_a : col_b;
    if (tool == T_PICK) {
      if (b == 1) col_a = peek(x, y); else col_b = peek(x, y);
      return;
    }
    px_snapshot();
    dirty = 1;
    if (tool == T_FILL) { flood(x, y, c); return; }
    stroke = b; sx = lx = x; sy = ly = y;
    stroke_draw(x, y);
    return;
  }
  if (stroke) {
    if (!(held & (stroke == 1 ? MOUSE_LEFT : MOUSE_RIGHT))) { stroke = 0; return; }
    int z = region_zoom();       // outside the canvas: the nearest pixel (lines can end on the edge)
    x = clampi((mx - canvas_x()) / z, 0, reg_w() - 1); y = clampi((my - canvas_y()) / z, 0, reg_h() - 1);
    if (x != lx || y != ly || tool != T_PEN) stroke_draw(x, y);
  }
}
// the sheet shown at the right of the drawing page, 2 x: a cell is 16 x 16 pixels
#define SHEET_X 352
#define SHEET_Y 48
static bool sheet_cell(int mx, int my, int *cx, int *cy) {
  if (mx < SHEET_X || my < SHEET_Y || mx >= SHEET_X + 256 || my >= SHEET_Y + 256) return false;
  *cx = (mx - SHEET_X) / 16; *cy = (my - SHEET_Y) / 16;
  return true;
}

// ================================================================ maps
static void map_resize(int m, int wc, int hc) {
  int w0 = map_w(m), h0 = map_h(m), w = 32 << wc, h = 32 << hc;
  if (w * h > MG_MAP_CELLS) { ui_message("4096 CASES AU PLUS (LARGEUR X HAUTEUR)"); return; }
  map_snapshot();
  static uint16_t t[MG_MAP_CELLS];
  memcpy(t, mapc[m], sizeof t);
  memset(mapc[m], 0, sizeof mapc[m]);
  for (int y = 0; y < h && y < h0; y++) for (int x = 0; x < w && x < w0; x++) mapc[m][y * w + x] = t[y * w0 + x];
  map_wc[m] = (uint8_t)wc; map_hc[m] = (uint8_t)hc;
  dirty = 1;
}
static int tiles_across(void) { return map_t16[map_sel] ? 8 : 16; }    // in a page of the sheet
static int tile_px(void) { return map_t16[map_sel] ? 16 : 8; }
static int max_tile(void) { return map_t16[map_sel] ? 255 : 1023; }
static uint16_t brush_entry(int dx, int dy) {
  int ta = tiles_across(), t = brush;
  int bx = brush_hf ? brush_w - 1 - dx : dx, by = brush_vf ? brush_h - 1 - dy : dy;
  t = t + by * ta + bx;
  return t > max_tile() ? 0 : MG_ENTRY(t, pal_sel, brush_hf, brush_vf);
}
static void map_put(int tx, int ty, bool erase) {
  int w = map_w(map_sel), h = map_h(map_sel);
  for (int dy = 0; dy < brush_h; dy++)
    for (int dx = 0; dx < brush_w; dx++) {
      int x = tx + dx, y = ty + dy;
      if (x < 0 || y < 0 || x >= w || y >= h) continue;
      mapc[map_sel][y * w + x] = erase ? 0 : brush_entry(dx, dy);
    }
}
static void map_box(int x0, int y0, int x1, int y1, bool erase) {
  if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
  if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
  int w = map_w(map_sel);
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) mapc[map_sel][y * w + x] = erase ? 0 : brush_entry((x - x0) % brush_w, (y - y0) % brush_h);
}
static void map_flood(int x, int y, bool erase) {
  int w = map_w(map_sel), h = map_h(map_sel), n = 0;
  uint16_t *c = mapc[map_sel], old = c[y * w + x], nw = erase ? 0 : brush_entry(0, 0);
  if (old == nw) return;
  static uint16_t st[MG_MAP_CELLS];
  st[n++] = (uint16_t)(y * w + x);
  while (n) {
    int p = st[--n];
    if (c[p] != old) continue;
    c[p] = nw;
    int px = p % w, py = p / w;
    if (px > 0 && c[p - 1] == old && n < MG_MAP_CELLS) st[n++] = (uint16_t)(p - 1);
    if (px < w - 1 && c[p + 1] == old && n < MG_MAP_CELLS) st[n++] = (uint16_t)(p + 1);
    if (py > 0 && c[p - w] == old && n < MG_MAP_CELLS) st[n++] = (uint16_t)(p - w);
    if (py < h - 1 && c[p + w] == old && n < MG_MAP_CELLS) st[n++] = (uint16_t)(p + w);
  }
}
// the map view: 320 x 320 pixels at (16, 48); the tile picker: the sheet 2 x at (352, 48)
#define MV_X 16
#define MV_Y 48
#define MV_W 320
#define MV_H 320
#define PK_X 352
#define PK_Y 48
static bool map_tile_under(int mx, int my, int *tx, int *ty) {
  if (mx < MV_X || my < MV_Y || mx >= MV_X + MV_W || my >= MV_Y + MV_H) return false;
  int t = tile_px();
  *tx = (mview_x + mx - MV_X) / t; *ty = (mview_y + my - MV_Y) / t;
  return *tx < map_w(map_sel) && *ty < map_h(map_sel);
}
static bool picker_tile(int mx, int my, int *t) {   // tile number under the pointer, in the page shown
  if (mx < PK_X || my < PK_Y || mx >= PK_X + 256 || my >= PK_Y + 256) return false;
  int s = map_t16[map_sel] ? 32 : 16, ta = tiles_across();
  *t = sheet_page * (map_t16[map_sel] ? 64 : 256) + ((my - PK_Y) / s) * ta + (mx - PK_X) / s;
  return true;
}
static void map_view_clamp(void) {
  int t = tile_px();
  mview_x = clampi(mview_x, 0, map_w(map_sel) * t > MV_W ? map_w(map_sel) * t - MV_W : 0);
  mview_y = clampi(mview_y, 0, map_h(map_sel) * t > MV_H ? map_h(map_sel) * t - MV_H : 0);
}
static void map_mouse(int mx, int my) {
  uint32_t pr = mc_mouse_pressed(), held = mc_mouse_buttons();
  int tx, ty, t;
  if (pr & MOUSE_MIDDLE) { mid_drag = 1; mid_x = mx; mid_y = my; }
  if (mid_drag) {
    if (!(held & MOUSE_MIDDLE)) mid_drag = 0;
    else { mview_x -= mx - mid_x; mview_y -= my - mid_y; mid_x = mx; mid_y = my; map_view_clamp(); }
    return;
  }
  // tile picker: a click chooses a tile, a drag a block of tiles
  if (!mstroke && (pr & MOUSE_LEFT) && picker_tile(mx, my, &t)) { pick_drag = 1; pick_x0 = mx; pick_y0 = my; brush = t; brush_w = brush_h = 1; }
  if (pick_drag) {
    if (!(held & MOUSE_LEFT)) { pick_drag = 0; return; }
    int t0, t1;
    if (picker_tile(pick_x0, pick_y0, &t0) && picker_tile(clampi(mx, PK_X, PK_X + 255), clampi(my, PK_Y, PK_Y + 255), &t1)) {
      int ta = tiles_across(), x0 = t0 % ta, y0 = t0 / ta, x1 = t1 % ta, y1 = t1 / ta;
      if (x1 < x0) { int k = x0; x0 = x1; x1 = k; }
      if (y1 < y0) { int k = y0; y0 = y1; y1 = k; }
      brush = y0 * ta + x0; brush_w = x1 - x0 + 1; brush_h = y1 - y0 + 1;
    }
    return;
  }
  bool in = map_tile_under(mx, my, &tx, &ty);
  if (!mstroke && in && (pr & (MOUSE_LEFT | MOUSE_RIGHT))) {
    int b = (pr & MOUSE_LEFT) ? 1 : 2;
    if (mtool == M_PICK) {
      uint16_t e = mapc[map_sel][ty * map_w(map_sel) + tx];
      brush = MG_E_TILE(e); brush_w = brush_h = 1; pal_sel = MG_E_PAL(e); brush_hf = MG_E_HFLIP(e); brush_vf = MG_E_VFLIP(e);
      if (brush) sheet_page = brush / (map_t16[map_sel] ? 64 : 256);
      return;
    }
    map_snapshot(); dirty = 1;
    if (mtool == M_FILL) { map_flood(tx, ty, b == 2); return; }
    mstroke = b; msx = mlx = tx; msy = mly = ty;
    if (mtool == M_PUT) map_put(tx, ty, b == 2);
    return;
  }
  if (mstroke) {
    bool erase = mstroke == 2;
    if (!(held & (mstroke == 1 ? MOUSE_LEFT : MOUSE_RIGHT))) {
      if (mtool == M_BOX && in) map_box(msx, msy, tx, ty, erase);
      mstroke = 0; return;
    }
    if (mtool == M_PUT && in && (tx != mlx || ty != mly)) {   // every tile between the two positions
      int dx = tx - mlx, dy = ty - mly, n = dx < 0 ? -dx : dx;
      if ((dy < 0 ? -dy : dy) > n) n = dy < 0 ? -dy : dy;
      for (int i = 1; i <= n; i++) map_put(mlx + dx * i / n, mly + dy * i / n, erase);
      mlx = tx; mly = ty;
    }
  }
}

// ================================================================ layer 3: checkerboard or map
static int layer3_mode = -1;
static void layer3_checker(void) {
  if (layer3_mode != 0) {
    mc_layer(3, MC_CHR(MISC_OFF), MC_TILE16 | MC_4BPP, 64, 32, 0);
    mc_layer_scroll(3, 0, 0);
    mc_layer_linescroll(3, false);
    layer3_mode = 0;
  }
}
static void checker_area(int x0, int y0, int w, int h) {   // in 16-pixel tiles
  volatile uint32_t *tm = TILEMAP(3);
  for (int y = y0; y < y0 + h; y++) for (int x = x0; x < x0 + w; x++) tm[y * 64 + x] = TILE(1, P_CHECK);
}
static void layer3_map(void) {
  int m = map_sel, w = map_w(m), h = map_h(m);
  mc_layer(3, MC_CHR(map_t16[m] ? T16_OFF : SHEET_OFF), (map_t16[m] ? MC_TILE16 : MC_TILE8) | MC_4BPP, w, h, 0);
  layer3_mode = 1;
  mc_layer_scroll(3, mview_x - MV_X, mview_y - MV_Y);
  volatile uint32_t *tm = TILEMAP(3);
  const uint16_t *c = mapc[m];
  for (int i = 0; i < w * h; i++) {
    uint16_t e = c[i];
    uint32_t hw = TILE(MG_E_TILE(e), P_BANK + MG_E_PAL(e));
    if (MG_E_HFLIP(e)) hw |= T_HFLIP;
    if (MG_E_VFLIP(e)) hw |= T_VFLIP;
    tm[i] = hw;
  }
}

// ================================================================ drawing the pages
enum { Z_PAGE = 1, Z_TOOL, Z_SPIN, Z_BTN, Z_SWATCH, Z_SPR, Z_SLOT, Z_NAME, Z_HELP, Z_MTOOL };
enum { SP_PAL, SP_R, SP_G, SP_B, SP_RW, SP_RH, SP_SX, SP_SY, SP_SW, SP_SH, SP_SPAL, SP_SFR, SP_SSPD, SP_MAP, SP_MW, SP_MH, SP_MT, SP_MPAL };
enum { A_MIRH, A_MIRV, A_ROT, A_CLEAR, A_COPY, A_PASTE, A_GRID, A_UNDO, A_SAVE, A_PAGE0, A_PAGE1, A_PAGE2, A_PAGE3,
       A_SNEW, A_STAKE, A_SDRAW, A_SDEL, A_SPING, A_BSAVE, A_BLOAD, A_BDEL, A_BNEW, A_BDEMO, A_LAYOUT,
       A_MFLIPH, A_MFLIPV, A_MCLEAR };
static char line[UI_COLS + 1];

static void draw_top(void) {
  static const char *const names[5] = { "DESSIN", "LUTINS", "DECORS", "BANQUE", "AIDE" };
  ui_box(0, 1, 13, 1, UI_RAISED);
  ui_print(1, 1, "MONOPIXEL", UI_INK_BLUE);
  for (int p = 0; p < 5; p++) {
    snprintf(line, sizeof line, "F%d %s", p + 1, names[p]);
    ui_button(14 + p * 12, 1, 12, line, page == p, Z_PAGE, p);
  }
  ui_button(74, 1, 6, "SAUVER", false, Z_BTN, A_SAVE);
  if (dirty) ui_print(74, 1, "SAUVER", UI_INK_RED);
}
static void draw_status(const char *help) {
  ui_box(0, 29, 79, 29, UI_SUNK);
  const char *m = ui_message_text();
  snprintf(line, sizeof line, "%s", m ? m : help);
  ui_printn(1, 29, line, 64, m ? UI_YELLOW : UI_GREY);
  ui_printn(66, 29, bank_name, 13, UI_CYAN);
}
static void sync_palettes(void) {
  for (int p = 0; p < MG_PALS; p++) for (int i = 1; i < 16; i++) PALETTE[(P_BANK + p) * 16 + i] = pals[p][i];
  for (int i = 0; i < 16; i++) PALETTE[(P_SWATCH + i) * 16 + 3] = pals[pal_sel][i];
}
// the palette strip and the colour settings (rows 20-27, columns 2-33)
static void draw_palette_panel(int y) {
  ui_box(2, y, 33, y + 7, UI_RAISED);
  snprintf(line, sizeof line, "%2d", pal_sel);
  ui_spin(3, y + 1, "PALETTE", 8, line, 3, Z_SPIN, SP_PAL);
  snprintf(line, sizeof line, "A:%-2d B:%-2d", col_a, col_b);
  ui_print(20, y + 1, line, UI_INK);
  for (int i = 0; i < 16; i++) {
    int x = 2 + i * 2;
    for (int r = 0; r < 2; r++) {
      ui_put(x, y + 3 + r, i ? UI_G_SOLID : G_TRANSP, i ? P_SWATCH + i : P_GRID);
      ui_put(x + 1, y + 3 + r, i ? UI_G_SOLID : G_TRANSP, i ? P_SWATCH + i : P_GRID);
    }
    if (i == col_a) ui_put(x, y + 2, 'A' - 32, UI_INK_BLUE);
    if (i == col_b) ui_put(x + 1, y + 2, 'B' - 32, UI_INK_RED);
    ui_zone(x, y + 2, x + 1, y + 4, Z_SWATCH, i);
  }
  uint16_t c = pals[pal_sel][col_a];
  snprintf(line, sizeof line, "%2d", (c >> 10) & 31); ui_spin(3, y + 6, "R", 2, line, 3, Z_SPIN, SP_R);
  snprintf(line, sizeof line, "%2d", (c >> 5) & 31);  ui_spin(12, y + 6, "V", 2, line, 3, Z_SPIN, SP_G);
  snprintf(line, sizeof line, "%2d", c & 31);         ui_spin(21, y + 6, "B", 2, line, 3, Z_SPIN, SP_B);
  if (col_a == 0) ui_print(28, y + 6, "TRANSP", UI_INK_DIM);
}
static void draw_grid(void) {
  int z = region_zoom(), cx = canvas_x(), cy = canvas_y(), w = reg_w() * z, h = reg_h() * z;
  for (int Y = 0; Y < h; Y += 16)
    for (int X = 0; X < w; X += 8) {
      int v = 0, h0 = 0, h8 = 0;
      if (X % (8 * z) == 0) v = 2; else if (z >= 8 && X % z == 0) v = 1;
      if (Y % (8 * z) == 0) h0 = 2; else if (z >= 8 && Y % z == 0) h0 = 1;
      if (z <= 8) { int y8 = Y + 8; if (y8 % (8 * z) == 0) h8 = 2; else if (z == 8) h8 = 1; }
      if (v || h0 || h8) ui_put((cx + X) / 8, (cy + Y) / 16, G_GRID + v * 9 + h0 * 3 + h8, P_GRID);
    }
}
static void draw_draw(void) {
  ui_box(2, 3, 33, 18, UI_FRAME);
  layer3_checker();
  mc_tilemap_fill(3, 0);
  checker_area(canvas_x() / 16, canvas_y() / 16, reg_w() * region_zoom() / 16, reg_h() * region_zoom() / 16);
  checker_area(SHEET_X / 16, SHEET_Y / 16, 16, 16);
  // canvas: a copy of the region, contiguous, zoomed by the hardware
  for (int cy = 0; cy < rh; cy++) mc_copy(MC_CHRRAM + CANVAS_OFF + cy * rw * 32, (const void *)(CHRRAM_BASE + SHEET_OFF + ((ry + cy) * MG_SHEET_W + rx) * 32), rw * 32);
  int z = region_zoom();
  mc_sprite(1, canvas_x(), canvas_y(), CELL(CANVAS_OFF), rw, rh, P_BANK + pal_sel, 4); mc_sprite_zoom(1, z * 256, z * 256);
  if (grid) draw_grid();
  // tools
  for (int t = 0; t < T_COUNT; t++) ui_button(36, 3 + 2 * t, 8, tool_name[t], tool == t, Z_TOOL, t);
  ui_button(36, 15, 8, "GRILLE", grid, Z_BTN, A_GRID);
  ui_button(36, 17, 8, "ANNULER", false, Z_BTN, A_UNDO);
  // sheet
  ui_box(44, 3, 75, 18, UI_FRAME);
  mc_sprite(2, SHEET_X, SHEET_Y, CELL(SHEET_OFF) + sheet_page * MG_PAGE_CELLS, 16, 16, P_BANK + pal_sel, 4); mc_sprite_zoom(2, 0x200, 0x200);
  if (ry / 16 == sheet_page) ui_frame(44 + 2 * rx, 3 + ry % 16, 44 + 2 * (rx + rw) - 1, 3 + ry % 16 + rh - 1, P_SEL);
  int mx = mc_mouse_x(), my = mc_mouse_y(), hx, hy;
  if (mc_mouse_present() && sheet_cell(mx, my, &hx, &hy)) {
    hx = clampi(hx, 0, 16 - rw); hy = clampi(hy, 0, 16 - rh);
    ui_frame(44 + 2 * hx, 3 + hy, 44 + 2 * (hx + rw) - 1, 3 + hy + rh - 1, P_HOVER);
  }
  for (int y = 3; y <= 18; y++) ui_put(76, y, UI_G_SOLID, 15);   // the space right of the sheet
  draw_palette_panel(20);
  ui_box(36, 20, 79, 27, UI_RAISED);
  ui_print(37, 21, "PAGE", UI_INK);
  for (int p = 0; p < MG_PAGES; p++) { snprintf(line, sizeof line, "%d", p + 1); ui_button(42 + p * 4, 21, 4, line, sheet_page == p, Z_BTN, A_PAGE0 + p); }
  snprintf(line, sizeof line, "%2d", rw * 8); ui_spin(60, 21, "L", 2, line, 3, Z_SPIN, SP_RW);
  snprintf(line, sizeof line, "%2d", rh * 8); ui_spin(68, 21, "H", 2, line, 3, Z_SPIN, SP_RH);
  ui_button(36, 23, 10, "MIROIR H", false, Z_BTN, A_MIRH);
  ui_button(46, 23, 10, "MIROIR V", false, Z_BTN, A_MIRV);
  ui_button(56, 23, 10, "PIVOTER", false, Z_BTN, A_ROT);
  ui_button(66, 23, 12, "EFFACER", false, Z_BTN, A_CLEAR);
  ui_button(36, 25, 10, "COPIER", false, Z_BTN, A_COPY);
  ui_button(46, 25, 10, "COLLER", false, Z_BTN, A_PASTE);
  int cell = ry * MG_SHEET_W + rx;
  snprintf(line, sizeof line, "CELLULE %d (%d,%d)", cell, rx, ry);
  ui_print(57, 25, line, UI_INK_DIM);
  ui_print(57, 27, rw == 2 && rh == 2 ? "TUILE 16 : " : "", UI_INK_DIM);
  if (rw == 2 && rh == 2 && rx % 2 == 0 && ry % 2 == 0) { snprintf(line, sizeof line, "%d", (ry / 2) * 8 + rx / 2); ui_print(68, 27, line, UI_INK_DIM); }
  // what is under the pointer
  int px, py;
  if (mc_mouse_present() && canvas_pixel(mx, my, &px, &py)) {
    snprintf(line, sizeof line, "X %d  Y %d  COULEUR %d   (OUTIL %s)", px, py, peek(px, py), tool_name[tool]);
    draw_status(line);
  } else
    draw_status("CLIC : COULEUR A, CLIC DROIT : B  -  PLANCHE : CLIC CHOISIT LA ZONE, CLIC DROIT Y COPIE LA ZONE");
}

static void draw_sprites(void) {
  ui_box(2, 3, 41, 26, UI_SUNK);
  ui_print(3, 3, "NO NOM       TAILLE PAL IMG VIT", UI_GREY);
  const int rows = 22;
  spr_scroll = clampi(spr_scroll, 0, MG_SPRITES - rows);
  if (spr_sel < spr_scroll) spr_scroll = spr_sel;
  if (spr_sel >= spr_scroll + rows) spr_scroll = spr_sel - rows + 1;
  for (int i = 0; i < rows; i++) {
    int s = spr_scroll + i, y = 4 + i;
    const mg_sprite_t *e = &spr[s];
    int pal = s == spr_sel ? UI_YELLOW : e->name[0] ? UI_WHITE : UI_GREY;
    if (e->name[0]) snprintf(line, sizeof line, "%02d %-8.8s  %2dx%-2d  %2d  %2d  %2d", s, e->name, e->w * 8, e->h * 8, e->pal, e->frames, e->speed);
    else snprintf(line, sizeof line, "%02d -", s);
    if (s == spr_sel) ui_put(2, y, UI_G_ARROW, UI_YELLOW);
    ui_print(3, y, line, pal);
    ui_zone(3, y, 40, y, Z_SPR, s);
  }
  mg_sprite_t *e = &spr[spr_sel];
  ui_box(44, 3, 79, 18, UI_RAISED);
  snprintf(line, sizeof line, "LUTIN %02d", spr_sel);
  ui_print(45, 3, line, UI_INK_BLUE);
  if (!e->name[0]) {
    ui_print(45, 5, "EMPLACEMENT VIDE", UI_INK_DIM);
    ui_button(44, 7, 16, "NOUVEAU", false, Z_BTN, A_SNEW);
    ui_print(45, 9, "(LA ZONE DU DESSIN DEVIENT", UI_INK_DIM);
    ui_print(45, 10, " SON IMAGE 0)", UI_INK_DIM);
  } else {
    ui_print(45, 5, "NOM", UI_INK);
    ui_printn(54, 5, e->name, 8, naming == 1 ? UI_INK_RED : UI_INK_BLUE);
    if (naming == 1 && (frame_count & 16)) ui_put(54 + (int)strnlen(e->name, 8), 5, '_' - 32, UI_INK_RED);
    ui_zone(45, 5, 63, 5, Z_NAME, 1);
    static const char *const lab[7] = { "COLONNE", "LIGNE", "LARGEUR", "HAUTEUR", "PALETTE", "IMAGES", "VITESSE" };
    int val[7] = { e->x, e->y, e->w * 8, e->h * 8, e->pal, e->frames, e->speed };
    for (int k = 0; k < 7; k++) {
      snprintf(line, sizeof line, "%3d", val[k]);
      ui_spin(45 + (k & 1) * 17, 7 + (k / 2) * 2, lab[k], 9, line, 4, Z_SPIN, SP_SX + k);
    }
    ui_button(62, 13, 16, "ALLER-RETOUR", e->flags & MG_SF_PINGPONG, Z_BTN, A_SPING);
    ui_button(44, 15, 12, "PRENDRE", false, Z_BTN, A_STAKE);
    ui_button(56, 15, 12, "DESSINER", false, Z_BTN, A_SDRAW);
    ui_button(68, 15, 10, "EFFACER", false, Z_BTN, A_SDEL);
    ui_print(45, 17, "PRENDRE : LA ZONE DU DESSIN", UI_INK_DIM);
  }
  // preview: the current image, copied contiguous, zoomed to fit 176 x 112
  ui_box(44, 20, 79, 27, UI_FRAME);
  layer3_checker();
  mc_tilemap_fill(3, 0);
  if (e->name[0]) {
    int f = mg_sprite_frame(e, frame_count);
    int cx, cy, z = 1;
    mg_frame_pos(e, f, &cx, &cy);
    for (int y = 0; y < e->h; y++) {
      if (cy + y < MG_PAGES * 16) mc_copy(MC_CHRRAM + PREVIEW_OFF + y * e->w * 32, (const void *)(CHRRAM_BASE + SHEET_OFF + ((cy + y) * MG_SHEET_W + cx) * 32), e->w * 32);
      else mc_fill(MC_CHRRAM + PREVIEW_OFF + y * e->w * 32, 0, e->w * 32);
    }
    while (z < 4 && e->w * 8 * (z + 1) <= 256 && e->h * 8 * (z + 1) <= 112) z++;
    int w = e->w * 8 * z, h = e->h * 8 * z, x0 = 352 + (256 - w) / 2, y0 = 328 + (112 - h) / 2;
    x0 &= ~15; y0 &= ~15;
    checker_area(x0 / 16, y0 / 16, (w + 15) / 16, (h + 15) / 16);
    mc_sprite(3, x0, y0, CELL(PREVIEW_OFF), e->w, e->h, P_BANK + e->pal, 4); mc_sprite_zoom(3, z * 256, z * 256);
    snprintf(line, sizeof line, "IMAGE %d/%d", f + 1, e->frames);
    ui_print(45, 20, line, UI_GREY);
  }
  draw_status(naming == 1 ? "ECRIVEZ LE NOM, ENTREE POUR FINIR" : "LES JEUX : MG_SPRITE_FIND(&BANK, \"NOM\")  -  FLECHES : CHOISIR, ENTREE : NOMMER");
}

static void draw_maps(void) {
  int m = map_sel;
  map_view_clamp();
  ui_box(2, 3, 41, 22, UI_FRAME);
  layer3_map();
  for (int x = 2; x <= 41; x++) { ui_put(x, 2, G_MASK_LOW, P_MASK); ui_put(x, 23, G_MASK_HIGH, P_MASK); }
  // tile picker: the sheet, 2 x, on a dark panel (the map must not show through it)
  ui_box(44, 3, 75, 18, UI_SUNK);
  mc_sprite(2, PK_X, PK_Y, CELL(SHEET_OFF) + sheet_page * MG_PAGE_CELLS, 16, 16, P_BANK + pal_sel, 4); mc_sprite_zoom(2, 0x200, 0x200);
  for (int y = 3; y <= 18; y++) ui_put(76, y, UI_G_SOLID, 15);
  int ta = tiles_across(), per_page = ta * ta, s = map_t16[m] ? 4 : 2;   // a tile: s characters, s / 2 rows
  if (brush / per_page == sheet_page) {
    int b = brush % per_page, bx = b % ta, by = b / ta;
    ui_frame(44 + bx * s, 3 + by * s / 2, 44 + (bx + brush_w) * s - 1, 3 + (by + brush_h) * s / 2 - 1, P_SEL);
  }
  int mx = mc_mouse_x(), my = mc_mouse_y(), tx, ty, t;
  bool over_map = mc_mouse_present() && map_tile_under(mx, my, &tx, &ty);
  if (mc_mouse_present() && picker_tile(mx, my, &t)) {
    int b = t % per_page;
    ui_frame(44 + (b % ta) * s, 3 + (b / ta) * s / 2, 44 + (b % ta) * s + s - 1, 3 + (b / ta) * s / 2 + s / 2 - 1, P_HOVER);
  }
  // the brush under the pointer, as a frame over the map (in characters, so only for 16-pixel tiles)
  if (over_map && map_t16[m]) {
    int X = MV_X + tx * 16 - mview_x, Y = MV_Y + ty * 16 - mview_y;
    if (X >= 0 && Y >= 0) ui_frame(X / 8, Y / 16, X / 8 + 2 * brush_w - 1, Y / 16 + brush_h - 1, mstroke == 2 ? P_GRID : P_HOVER);
  }
  // settings
  ui_box(44, 20, 79, 27, UI_RAISED);
  snprintf(line, sizeof line, "%d", m + 1);           ui_spin(45, 21, "DECOR", 6, line, 2, Z_SPIN, SP_MAP);
  snprintf(line, sizeof line, "%3d", map_w(m));       ui_spin(57, 21, "L", 2, line, 4, Z_SPIN, SP_MW);
  snprintf(line, sizeof line, "%3d", map_h(m));       ui_spin(67, 21, "H", 2, line, 4, Z_SPIN, SP_MH);
  snprintf(line, sizeof line, "%2d", tile_px());      ui_spin(45, 23, "TUILES", 7, line, 3, Z_SPIN, SP_MT);
  snprintf(line, sizeof line, "%2d", pal_sel);        ui_spin(58, 23, "PAL", 4, line, 3, Z_SPIN, SP_MPAL);
  ui_button(68, 23, 4, "H", brush_hf, Z_BTN, A_MFLIPH);
  ui_button(72, 23, 4, "V", brush_vf, Z_BTN, A_MFLIPV);
  for (int k = 0; k < M_COUNT; k++) ui_button(44 + k * 8, 25, 8, mtool_name[k], mtool == k, Z_MTOOL, k);
  ui_box(0, 24, 41, 27, UI_RAISED);
  ui_print(1, 24, "PAGE", UI_INK);
  for (int p = 0; p < MG_PAGES; p++) { snprintf(line, sizeof line, "%d", p + 1); ui_button(6 + p * 4, 24, 4, line, sheet_page == p, Z_BTN, A_PAGE0 + p); }
  ui_button(26, 24, 14, "TOUT EFFACER", false, Z_BTN, A_MCLEAR);
  snprintf(line, sizeof line, "TUILE %d  %dx%d", brush, brush_w, brush_h);
  ui_print(1, 26, line, UI_INK);
  if (over_map) {
    uint16_t e = mapc[m][ty * map_w(m) + tx];
    snprintf(line, sizeof line, "CASE %d,%d : TUILE %d PAL %d", tx, ty, MG_E_TILE(e), MG_E_PAL(e));
    ui_print(18, 26, line, UI_INK_BLUE);
  }
  draw_status("CLIC : POSE, CLIC DROIT : EFFACE, BOUTON DU MILIEU OU FLECHES : DEFILE, MOLETTE : MONTE/DESCEND");
}

static void layer3_off(void) { layer3_checker(); mc_tilemap_fill(3, 0); }
static void draw_bank(void) {
  layer3_off();
  ui_box(2, 3, 41, 21, UI_SUNK);
  ui_print(3, 3, "EMPLACEMENTS DE LA NVRAM", UI_GREY);
  for (int k = 0; k < NV_SLOTS; k++) {
    int y = 5 + 2 * k;
    if (slot_sel == k) ui_put(2, y, UI_G_ARROW, UI_YELLOW);
    if (slot_size(k)) {
      mg_bank_t b;
      const char *nm = mg_open(&b, slot_data(k), slot_size(k)) ? "?" : mg_name(&b);
      snprintf(line, sizeof line, "%d %-16.16s %2u.%uK", k + 1, nm, (unsigned)(slot_size(k) / 1024), (unsigned)(slot_size(k) % 1024 * 10 / 1024));
    } else snprintf(line, sizeof line, "%d -", k + 1);
    ui_print(3, y, line, slot_sel == k ? UI_YELLOW : k == bank_slot ? UI_CYAN : slot_size(k) ? UI_WHITE : UI_GREY);
    ui_zone(3, y, 40, y, Z_SLOT, k);
  }
  snprintf(line, sizeof line, "LIBRE : %u OCTETS", (unsigned)nv_free(-1));
  ui_print(3, 21, line, UI_GREY);
  ui_box(44, 3, 79, 21, UI_RAISED);
  ui_print(45, 3, "BANQUE EN COURS", UI_INK_BLUE);
  ui_print(45, 5, "NOM", UI_INK);
  ui_printn(50, 5, bank_name, 16, naming == 2 ? UI_INK_RED : UI_INK_BLUE);
  if (naming == 2 && (frame_count & 16)) ui_put(50 + (int)strnlen(bank_name, 16), 5, '_' - 32, UI_INK_RED);
  ui_zone(45, 5, 66, 5, Z_NAME, 2);
  int ns = 0, nm = maps_used();
  for (int s = 0; s < MG_SPRITES; s++) if (spr[s].name[0]) ns++;
  snprintf(line, sizeof line, "PAGES %d  LUTINS %d  DECORS %d", pages_used(), ns, nm); ui_print(45, 7, line, UI_INK);
  snprintf(line, sizeof line, "TAILLE COMPACTE : %u OCTETS", (unsigned)bank_encode(0)); ui_print(45, 8, line, UI_INK);
  ui_button(44, 10, 12, "SAUVER", false, Z_BTN, A_BSAVE);
  ui_button(56, 10, 12, "CHARGER", false, Z_BTN, A_BLOAD);
  ui_button(68, 10, 10, "EFFACER", false, Z_BTN, A_BDEL);
  ui_button(44, 12, 12, "NOUVELLE", false, Z_BTN, A_BNEW);
  ui_button(56, 12, 12, "DEMO", false, Z_BTN, A_BDEMO);
  ui_print(45, 14, "CLAVIER", UI_INK);
  ui_button(54, 14, 12, ui_layout ? "QWERTY" : "AZERTY", false, Z_BTN, A_LAYOUT);
  ui_print(45, 16, "POUR UN JEU : OUVRIR L'OSD (SD),", UI_INK_DIM);
  ui_print(45, 17, "PUIS TOOLS/MGFX/MGFX.PY", UI_INK_DIM);
  ui_print(45, 18, "  MONOPIXEL.SAV N -O BANQUE.MGF", UI_INK_DIM);
  ui_print(45, 19, "ET : MODULES = MGFX", UI_INK_DIM);
  draw_status(naming == 2 ? "ECRIVEZ LE NOM, ENTREE POUR FINIR" : "CTRL+S : SAUVE  -  CTRL+L : CHARGE  -  CTRL+D : EFFACE L'EMPLACEMENT");
}

static void draw_help(void) {
  static const char *const help[] = {
    "MONOPIXEL - L'EDITEUR GRAPHIQUE DE LA MONOCORE-1 (SOURIS ET CLAVIER)",
    "",
    "F1 DESSIN  les pixels d'une zone de la planche (8 a 64 pixels) et sa palette",
    "F2 LUTINS  64 lutins : un rectangle de la planche, une palette, une animation",
    "F3 DECORS  4 decors en tuiles de 8 ou 16 pixels, poses a la souris",
    "F4 BANQUE  les 8 emplacements de la NVRAM : sauver, charger, nouvelle, demo",
    "",
    "DESSIN     clic : couleur A, clic droit : couleur B, molette : couleur A",
    "  C T R P  crayon, trait, cadre, pave      S : seau      I : pipette",
    "  X Y O    miroir horizontal, vertical, pivoter d'un quart de tour",
    "  FLECHES  deplacent la zone ; MAJ+FLECHES : decalent ses pixels",
    "  PAGE PRECEDENTE / SUIVANTE : page de la planche ; 1 2 3 4 : zone de 8 a 64",
    "  SUPPR    efface la zone ; CTRL+C, CTRL+V : copie, colle ; G : grille",
    "  PLANCHE  clic : choisit la zone ; clic droit : y copie la zone en cours",
    "",
    "DECORS     clic : pose la tuile, ou le bloc choisi en glissant sur la planche",
    "           clic droit : efface ; milieu, fleches : defile ; H, V : miroirs",
    "",
    "CTRL+Z     annule la derniere modification (une seconde fois : la refait)",
    "CTRL+S     sauve dans la NVRAM ; ouvrir l'OSD l'ecrit sur la carte SD",
    "",
    "POUR UN JEU : tools/mgfx/mgfx.py monopixel.sav 1 -o banque.mgf",
    "  puis dans project.txt : modules = mgfx, data = banque.mgf (sdk/MGFX.md)",
    "",
    "                    (F5, ECHAP OU UN CLIC POUR REVENIR)" };
  ui_box(0, 1, 79, 27, UI_SUNK);
  for (unsigned i = 0; i < sizeof help / sizeof help[0]; i++) {
    char b[UI_COLS + 1];
    int n = 0;
    for (const char *s = help[i]; *s && n < UI_COLS; s++) b[n++] = (char)(*s >= 'a' && *s <= 'z' ? *s - 32 : *s);
    b[n] = 0;
    ui_print(2, 2 + i, b, i == 0 ? UI_YELLOW : (b[0] != ' ' && b[0]) ? UI_CYAN : UI_WHITE);
  }
  ui_zone(0, 1, 79, 27, Z_HELP, 0);
  draw_status("MONOPIXEL - SOURIS USB ET CLAVIER");
}

// ================================================================ actions
static void goto_page(int p) {
  if (p == PG_HELP && page != PG_HELP) help_back = page;
  page = p; naming = 0; stroke = mstroke = 0;
  if (p == PG_MAP) mg_make_tiles16(SHEET, MC_CHRRAM + T16_OFF, MG_PAGES * 64);
  ui_pointer_shape(p == PG_DRAW || p == PG_MAP ? 1 : 0);
}
static void sprite_new(int s) {
  mg_sprite_t *e = &spr[s];
  memset(e, 0, sizeof *e);
  e->x = (uint8_t)rx; e->y = (uint8_t)ry; e->w = (uint8_t)rw; e->h = (uint8_t)rh; e->pal = (uint8_t)pal_sel; e->frames = 1;
  snprintf(e->name, 8, "LUTIN%02d", s);
  dirty = 1;
}
static void spin(int k, int d) {
  mg_sprite_t *e = &spr[spr_sel];
  int c = pals[pal_sel][col_a], r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
  switch (k) {
    case SP_PAL: pal_sel = clampi(pal_sel + d, 0, MG_PALS - 1); return;
    case SP_R: r = clampi(r + d, 0, 31); break;
    case SP_G: g = clampi(g + d, 0, 31); break;
    case SP_B: b = clampi(b + d, 0, 31); break;
    case SP_RW: set_region(rx, ry, next_size(rw, d > 0 ? 1 : -1), rh); return;
    case SP_RH: set_region(rx, ry, rw, next_size(rh, d > 0 ? 1 : -1)); return;
    case SP_SX: if (e->name[0]) { e->x = (uint8_t)clampi(e->x + d, 0, MG_SHEET_W - e->w); dirty = 1; } return;
    case SP_SY: if (e->name[0]) { e->y = (uint8_t)clampi(e->y + d, 0, MG_PAGES * 16 - e->h); dirty = 1; } return;
    case SP_SW: if (e->name[0]) { e->w = (uint8_t)clampi(e->w + (d > 0 ? 1 : -1), 1, 8); e->x = (uint8_t)clampi(e->x, 0, MG_SHEET_W - e->w); dirty = 1; } return;
    case SP_SH: if (e->name[0]) { e->h = (uint8_t)clampi(e->h + (d > 0 ? 1 : -1), 1, 8); e->y = (uint8_t)clampi(e->y, 0, MG_PAGES * 16 - e->h); dirty = 1; } return;
    case SP_SPAL: if (e->name[0]) { e->pal = (uint8_t)clampi(e->pal + (d > 0 ? 1 : -1), 0, MG_PALS - 1); dirty = 1; } return;
    case SP_SFR: if (e->name[0]) { e->frames = (uint8_t)clampi(e->frames + (d > 0 ? 1 : -1), 1, MG_FRAMES); dirty = 1; } return;
    case SP_SSPD: if (e->name[0]) { e->speed = (uint8_t)clampi(e->speed + d, 0, 99); dirty = 1; } return;
    case SP_MAP: map_sel = clampi(map_sel + (d > 0 ? 1 : -1), 0, MG_MAPS - 1); mview_x = mview_y = 0; return;
    case SP_MW: map_resize(map_sel, clampi(map_wc[map_sel] + (d > 0 ? 1 : -1), 0, 2), map_hc[map_sel]); return;
    case SP_MH: map_resize(map_sel, map_wc[map_sel], clampi(map_hc[map_sel] + (d > 0 ? 1 : -1), 0, 2)); return;
    case SP_MT:
      if (!map_empty(map_sel) && !ui_confirm(0x1000, "LES TUILES CHANGENT DE TAILLE")) return;
      map_t16[map_sel] ^= 1; brush = brush_w = brush_h = 1; dirty = 1; mview_x = mview_y = 0; return;
    case SP_MPAL: pal_sel = clampi(pal_sel + (d > 0 ? 1 : -1), 0, MG_PALS - 1); return;
    default: return;
  }
  pals[pal_sel][col_a] = (uint16_t)(r << 10 | g << 5 | b);
  dirty = 1;
}
static void bank_action(int a) {
  switch (a) {
    case A_BSAVE: {
      int k = slot_sel;
      if (!slot_size(k) || k == bank_slot || ui_confirm('S', "REMPLACER CET EMPLACEMENT")) bank_save(k);
      break;
    }
    case A_BLOAD:
      if (!slot_size(slot_sel)) { ui_message("EMPLACEMENT VIDE"); break; }
      if (dirty && !ui_confirm('L', "MODIFICATIONS PERDUES")) break;
      if (bank_load(slot_data(slot_sel), slot_size(slot_sel))) { bank_slot = slot_sel; NV[5] = (uint8_t)slot_sel; ui_message("BANQUE CHARGEE"); }
      break;
    case A_BDEL:
      if (slot_size(slot_sel) && ui_confirm('D', "EFFACER L'EMPLACEMENT")) {
        nv_delete(slot_sel);
        if (bank_slot == slot_sel) bank_slot = -1;
        ui_message("EMPLACEMENT EFFACE");
      }
      break;
    case A_BNEW: if (!dirty || ui_confirm('N', "MODIFICATIONS PERDUES")) { bank_new(); ui_message("NOUVELLE BANQUE"); } break;
    case A_BDEMO:
      if (!dirty || ui_confirm('R', "MODIFICATIONS PERDUES")) { bank_load(demo_file, demo_size); bank_slot = -1; ui_message("DEMO CHARGEE"); }
      break;
    case A_LAYOUT: ui_layout = !ui_layout; NV[4] = (uint8_t)ui_layout; break;
  }
}
static void action(int a) {
  switch (a) {
    case A_MIRH: region_op(0); break;
    case A_MIRV: region_op(1); break;
    case A_ROT: region_op(2); break;
    case A_CLEAR: region_op(3); break;
    case A_COPY: region_copy(); break;
    case A_PASTE: region_paste(); break;
    case A_GRID: grid = !grid; break;
    case A_UNDO: undo(); break;
    case A_SAVE: quick_save(); break;
    case A_PAGE0: case A_PAGE1: case A_PAGE2: case A_PAGE3:
      sheet_page = a - A_PAGE0;
      if (page == PG_DRAW) set_region(rx, sheet_page * 16 + ry % 16, rw, rh);
      break;
    case A_SNEW: sprite_new(spr_sel); break;
    case A_STAKE: {
      mg_sprite_t *e = &spr[spr_sel];
      e->x = (uint8_t)rx; e->y = (uint8_t)ry; e->w = (uint8_t)rw; e->h = (uint8_t)rh; e->pal = (uint8_t)pal_sel; dirty = 1;
      ui_message("LE LUTIN PREND LA ZONE DU DESSIN");
      break;
    }
    case A_SDRAW: {
      mg_sprite_t *e = &spr[spr_sel];
      int cx, cy, f = 0;
      mg_frame_pos(e, f, &cx, &cy);
      set_region(cx, cy, next_size(e->w, 0), next_size(e->h, 0));
      pal_sel = e->pal;
      goto_page(PG_DRAW);
      break;
    }
    case A_SDEL: if (ui_confirm('E', "EFFACER LE LUTIN (PAS SES PIXELS)")) { memset(&spr[spr_sel], 0, sizeof spr[0]); dirty = 1; } break;
    case A_SPING: spr[spr_sel].flags ^= MG_SF_PINGPONG; dirty = 1; break;
    case A_MFLIPH: brush_hf = !brush_hf; break;
    case A_MFLIPV: brush_vf = !brush_vf; break;
    case A_MCLEAR: if (ui_confirm('M', "EFFACER TOUT LE DECOR")) { map_snapshot(); memset(mapc[map_sel], 0, sizeof mapc[0]); dirty = 1; } break;
    default: bank_action(a); break;
  }
}

// ================================================================ keyboard
static void name_key(int k, char *name, int max) {
  int n = (int)strnlen(name, max);
  if (k == KEY_ENTER || k == KEY_KPENTER || k == KEY_ESC) { naming = 0; return; }
  if (k == KEY_BACKSPACE) { if (n) name[n - 1] = 0; dirty = 1; return; }
  int c = ui_char(k);
  if (c && n < max) { name[n] = (char)c; if (n + 1 < max) name[n + 1] = 0; dirty = 1; }
}
static void draw_key(int k) {
  int c = ui_char(k);
  bool sh = ui_shift();
  switch (k) {
    case KEY_LEFT:  if (sh) region_op(5); else set_region(rx - 1, ry, rw, rh); return;   // shift: the pixels move right
    case KEY_RIGHT: if (sh) region_op(4); else set_region(rx + 1, ry, rw, rh); return;
    case KEY_UP:    if (sh) region_op(7); else set_region(rx, ry - 1, rw, rh); return;
    case KEY_DOWN:  if (sh) region_op(6); else set_region(rx, ry + 1, rw, rh); return;
    case KEY_PGUP:  action(A_PAGE0 + clampi(sheet_page - 1, 0, 3)); return;
    case KEY_PGDN:  action(A_PAGE0 + clampi(sheet_page + 1, 0, 3)); return;
    case KEY_DELETE: region_op(3); return;
    case KEY_TAB: col_a = (col_a + (sh ? 15 : 1)) & 15; return;
  }
  if (ui_ctrl()) {
    if (c == 'C') region_copy();
    if (c == 'V') region_paste();
    return;
  }
  switch (c) {
    case 'C': tool = T_PEN; break;
    case 'T': tool = T_LINE; break;
    case 'R': tool = T_RECT; break;
    case 'P': tool = T_BOX; break;
    case 'S': tool = T_FILL; break;
    case 'I': tool = T_PICK; break;
    case 'X': region_op(0); break;
    case 'Y': region_op(1); break;
    case 'O': region_op(2); break;
    case 'G': grid = !grid; break;
    case '1': case '2': case '3': case '4': { int s = 1 << (c - '1'); set_region(rx, ry, s, s); break; }
  }
}
static void sprite_key(int k) {
  if (naming == 1) { name_key(k, spr[spr_sel].name, 8); return; }
  switch (k) {
    case KEY_UP: spr_sel = clampi(spr_sel - 1, 0, MG_SPRITES - 1); return;
    case KEY_DOWN: spr_sel = clampi(spr_sel + 1, 0, MG_SPRITES - 1); return;
    case KEY_PGUP: spr_sel = clampi(spr_sel - 16, 0, MG_SPRITES - 1); return;
    case KEY_PGDN: spr_sel = clampi(spr_sel + 16, 0, MG_SPRITES - 1); return;
    case KEY_ENTER: if (spr[spr_sel].name[0]) naming = 1; else sprite_new(spr_sel); return;
  }
}
static void map_key(int k) {
  int t = tile_px(), c = ui_char(k);
  switch (k) {
    case KEY_LEFT: mview_x -= t * (ui_shift() ? 8 : 1); break;
    case KEY_RIGHT: mview_x += t * (ui_shift() ? 8 : 1); break;
    case KEY_UP: mview_y -= t * (ui_shift() ? 8 : 1); break;
    case KEY_DOWN: mview_y += t * (ui_shift() ? 8 : 1); break;
    case KEY_PGUP: action(A_PAGE0 + clampi(sheet_page - 1, 0, 3)); break;
    case KEY_PGDN: action(A_PAGE0 + clampi(sheet_page + 1, 0, 3)); break;
  }
  if (!ui_ctrl()) {
    if (c == 'H') brush_hf = !brush_hf;
    if (c == 'V') brush_vf = !brush_vf;
    if (c == 'I') mtool = M_PICK;
    if (c == 'S') mtool = M_FILL;
    if (c == 'P') mtool = M_BOX;
    if (c == 'C') mtool = M_PUT;
  }
  map_view_clamp();
}
static void bank_key(int k) {
  if (naming == 2) { name_key(k, bank_name, 16); return; }
  int c = ui_char(k);
  if (k == KEY_UP) slot_sel = (slot_sel + NV_SLOTS - 1) % NV_SLOTS;
  else if (k == KEY_DOWN) slot_sel = (slot_sel + 1) % NV_SLOTS;
  else if (k == KEY_ENTER) naming = 2;
  else if (ui_ctrl() && c == 'L') bank_action(A_BLOAD);
  else if (ui_ctrl() && c == 'D') bank_action(A_BDEL);
}
static void key(int k) {
  if (naming) { if (page == PG_SPR) sprite_key(k); else bank_key(k); return; }
  switch (k) {
    case KEY_F1: goto_page(PG_DRAW); return;
    case KEY_F2: goto_page(PG_SPR); return;
    case KEY_F3: goto_page(PG_MAP); return;
    case KEY_F4: goto_page(PG_BANK); return;
    case KEY_F5: goto_page(page == PG_HELP ? help_back : PG_HELP); return;
    case KEY_ESC: if (page == PG_HELP) goto_page(help_back); return;
  }
  int c = ui_char(k);
  if (ui_ctrl() && c == 'S') { if (page == PG_BANK) bank_action(A_BSAVE); else quick_save(); return; }
  if (ui_ctrl() && c == 'Z') { undo(); return; }
  switch (page) {
    case PG_DRAW: draw_key(k); break;
    case PG_SPR: sprite_key(k); break;
    case PG_MAP: map_key(k); break;
    case PG_BANK: bank_key(k); break;
  }
}

// ================================================================ mouse
static int hold_btn, hold_timer, hold_x, hold_y;
static bool zone_repeats(const ui_zone_t *z) { return z->id == Z_SPIN && z->arg % 4 < 2; }
static void click(const ui_zone_t *z, int btn) {
  int d = (btn == 2 ? -1 : 1) * (ui_shift() ? 4 : 1);
  switch (z->id) {
    case Z_PAGE: if (btn == 1) goto_page(z->arg); break;
    case Z_TOOL: tool = z->arg; break;
    case Z_MTOOL: mtool = z->arg; break;
    case Z_SPIN:
      if (z->arg % 4 == 0) spin(z->arg / 4, d);
      else if (z->arg % 4 == 1) spin(z->arg / 4, -d);
      break;
    case Z_BTN: if (btn == 1) action(z->arg); break;
    case Z_SWATCH: if (btn == 1) col_a = z->arg; else col_b = z->arg; break;
    case Z_SPR: spr_sel = z->arg; naming = 0; break;
    case Z_SLOT: slot_sel = z->arg; break;
    case Z_NAME:
      if (z->arg == 1 && spr[spr_sel].name[0]) naming = 1;
      if (z->arg == 2) naming = 2;
      break;
    case Z_HELP: goto_page(help_back); break;
  }
}
static void wheel(const ui_zone_t *z, int w) {
  if (page == PG_MAP && !z) {
    if (ui_shift()) mview_x -= w * tile_px() * 2; else mview_y -= w * tile_px() * 2;
    map_view_clamp();
    return;
  }
  if (!z) { if (page == PG_DRAW) col_a = (col_a - w + 16) & 15; return; }
  if (z->id == Z_SPIN) spin(z->arg / 4, w);
  else if (z->id == Z_SPR) spr_sel = clampi(spr_sel - w, 0, MG_SPRITES - 1);
  else if (z->id == Z_SWATCH) col_a = (col_a - w + 16) & 15;
}
static void mouse(void) {
  if (!mc_mouse_present()) return;
  int mx = mc_mouse_x(), my = mc_mouse_y();
  uint32_t pr = mc_mouse_pressed();
  // drawing areas first (a stroke started there keeps going over the rest of the screen)
  if (page == PG_DRAW) {
    int hx, hy;
    if (!stroke && (pr & (MOUSE_LEFT | MOUSE_RIGHT)) && sheet_cell(mx, my, &hx, &hy)) {
      int nx = clampi(hx, 0, 16 - rw), ny = sheet_page * 16 + clampi(hy, 0, 16 - rh);
      if (pr & MOUSE_RIGHT) {                  // right click: a copy of the region there
        uint8_t *b = tmp;
        cells_read(b, rx, ry, rw, rh);
        set_region(nx, ny, rw, rh);
        px_snapshot();
        cells_write(b, rx, ry, rw, rh);
        dirty = 1;
        ui_message("ZONE COPIEE ICI (CTRL+Z ANNULE)");
      } else set_region(nx, ny, rw, rh);
      return;
    }
    canvas_mouse(mx, my);
    if (stroke) return;
  }
  if (page == PG_MAP) {
    map_mouse(mx, my);
    if (mstroke || pick_drag || mid_drag) return;
    int tx, ty, t;
    if (map_tile_under(mx, my, &tx, &ty) || picker_tile(mx, my, &t)) { if (mc_mouse_wheel()) wheel(0, mc_mouse_wheel()); return; }
  }
  const ui_zone_t *z = ui_zone_at(mx, my);
  int btn = (pr & MOUSE_LEFT) ? 1 : (pr & MOUSE_RIGHT) ? 2 : 0;
  if (naming && btn && !(z && z->id == Z_NAME)) naming = 0;
  if (btn && z) {
    click(z, btn);
    hold_btn = zone_repeats(z) ? btn : 0; hold_timer = 18; hold_x = mx; hold_y = my;
  } else if (hold_btn) {
    if (!(mc_mouse_buttons() & (hold_btn == 1 ? MOUSE_LEFT : MOUSE_RIGHT))) hold_btn = 0;
    else if (--hold_timer == 0) { hold_timer = 4; const ui_zone_t *h = ui_zone_at(hold_x, hold_y); if (h && zone_repeats(h)) click(h, hold_btn); }
  }
  if (mc_mouse_wheel()) wheel(z, mc_mouse_wheel());
}

// ================================================================ start
static void misc_graphics(void) {
  // grid glyphs: v (vertical line at x 0), h0 (top), h8 (middle); 1 pixel grid, 2 cell edges
  static uint8_t a[32], b[32];
  for (int v = 0; v < 3; v++)
    for (int h0 = 0; h0 < 3; h0++)
      for (int h8 = 0; h8 < 3; h8++) {
        memset(a, 0, 32); memset(b, 0, 32);
        for (int y = 0; y < 16; y++)
          for (int x = 0; x < 8; x++) {
            int c = 0;
            if (x == 0 && v) c = v;
            if (y == 0 && h0) c = h0 > c ? h0 : c;
            if (y == 1 && h0 == 2) c = 2;
            if (y == 8 && h8) c = h8 > c ? h8 : c;
            if (y == 9 && h8 == 2) c = 2;
            if (c) { uint8_t *p = &(y < 8 ? a : b)[(y & 7) * 4 + (x >> 1)]; *p = (uint8_t)((x & 1) ? (*p & 0x0F) | (c << 4) : (*p & 0xF0) | c); }
          }
        ui_glyph_tiles(G_GRID + v * 9 + h0 * 3 + h8, a, b);
      }
  ui_glyph_define(G_TRANSP, "11112222" "11112222" "11112222" "11112222" "22221111" "22221111" "22221111" "22221111"
                            "11112222" "11112222" "11112222" "11112222" "22221111" "22221111" "22221111" "22221111");
  ui_glyph_define(G_MASK_LOW, "........" "........" "........" "........" "........" "........" "........" "........"
                              "........" "........" "33333333" "33333333" "33333333" "33333333" "33333333" "33333333");
  ui_glyph_define(G_MASK_HIGH, "33333333" "33333333" "33333333" "33333333" "33333333" "33333333" "........" "........"
                               "........" "........" "........" "........" "........" "........" "........" "........");
  PALETTE[P_MASK * 16 + 3] = RGB555(12, 14, 26);
  // checkerboard tile (16 x 16) for layer 3, tile 1 (tile 0 empty)
  static uint8_t t[256];
  memset(t, 0, sizeof t);
  for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
    int c = ((x >> 3) ^ (y >> 3)) & 1 ? 1 : 2;
    uint8_t *p = &t[128 + y * 8 + (x >> 1)];
    *p = (uint8_t)((x & 1) ? (*p & 0x0F) | (c << 4) : (*p & 0xF0) | c);
  }
  mc_chr_upload(MISC_OFF, t, 256);
  PALETTE[P_GRID * 16 + 1] = RGB555(96, 100, 128);  PALETTE[P_GRID * 16 + 2] = RGB555(170, 176, 210);
  PALETTE[P_SEL * 16 + 1] = RGB555(255, 230, 60);
  PALETTE[P_HOVER * 16 + 1] = RGB555(90, 220, 255);
  PALETTE[P_CHECK * 16 + 1] = RGB555(52, 54, 66);  PALETTE[P_CHECK * 16 + 2] = RGB555(70, 72, 86);
}

int main(void) {
  mc_init();
  ui_init(UI_OFF);
  if (!ui_ok()) {                              // an older core has no 640-pixel mode: say so and stop
    mc_video_mode(384, 240);
    mc_text_layer(0, 3);
    mc_display(DISP_ON | DISP_LAYER(0));
    mc_text(0, 2, 12, "MONOPIXEL DEMANDE LE CORE", 0);
    mc_text(0, 2, 14, "MONOCORE VERSION 5 (HAUTE RESOLUTION)", 0);
    for (;;) mc_frame();
  }
  misc_graphics();
  nv_check();
  int last = NV[5];
  if (last < NV_SLOTS && slot_size(last) && bank_load(slot_data(last), slot_size(last))) bank_slot = last;
  else { bank_load(demo_file, demo_size); bank_slot = -1; }
  set_region(0, 0, 2, 2);
  goto_page(PG_DRAW);
  if (mc_hw_version() < 3) ui_message("CE CORE N'A PAS DE SOURIS (VERSION 3 ET PLUS) - F5 : AIDE");
  else ui_message("MONOPIXEL - F5 POUR L'AIDE");
  for (;;) {
    mc_frame();
    frame_count++;
    ui_frame_begin();
    int k;
    while ((k = ui_key()) >= 0) key(k);
    mouse();
    for (int i = 1; i <= 8; i++) mc_sprite_hide(i);    // drawn again by their page
    sync_palettes();
    if (page == PG_MAP) ui_box(0, 0, 79, 29, UI_BACK);   // the map layer shows only through the view
    if (page != PG_HELP) draw_top();
    switch (page) {
      case PG_DRAW: draw_draw(); break;
      case PG_SPR: draw_sprites(); break;
      case PG_MAP: draw_maps(); break;
      case PG_BANK: draw_bank(); break;
      default: layer3_off(); draw_help(); break;
    }
    ui_frame_end();
  }
}
