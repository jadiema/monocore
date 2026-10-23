// ARENA: a mode of our own, written in C (the game's graphics, our rules). The four dragons play on
// one board: each shoots bubbles of its own colour (player 1 green, 2 blue, 3 red, 4 yellow) and
// bursts only groups of 3 of its colour; a bubble that touches another colour sticks there. The
// bubbles in flight bounce on the walls and on each other. Bubbles cut from the ceiling fall and
// count for the player who cut them. The ceiling comes down every 20 s. 2 minutes; the best score
// wins. The game is started as a game of two (it loads its decor, palettes and bubble graphics),
// then frozen (no vertical blank for it) while the arena has the screen; at the end the console
// starts again from its reset.

#define AR_COLS   20                               // even rows 20 bubbles, odd rows 19 (half a bubble in)
#define AR_ROWS   15
#define AR_X      32                               // the board: 320 pixels wide
#define AR_Y      34
#define AR_STEP   14                               // between two rows
#define AR_LY     262                              // the launchers
#define AR_TIME   (120 * 60)
#define AR_DROP   (20 * 60)                        // the ceiling comes down
#define FX(v)     ((int32_t)(v) << 8)              // 24.8 fixed point

static const int16_t ar_sin[91] = {0, 4, 9, 13, 18, 22, 27, 31, 36, 40, 44, 49, 53, 58, 62, 66, 71, 75, 79, 83, 88, 92, 96, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 139, 143, 147, 150, 154, 158, 161, 165, 168, 171, 175, 178, 181, 184, 187, 190, 193, 196, 199, 202, 204, 207, 210, 212, 215, 217, 219, 222, 224, 226, 228, 230, 232, 234, 236, 237, 239, 241, 242, 243, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 254, 255, 255, 255, 256, 256, 256, 256};
static int ar_sinv(int d) { int s = d < 0 ? -1 : 1; d = d < 0 ? -d : d; return s * ar_sin[d > 90 ? 90 : d]; }
static int ar_cosv(int d) { d = d < 0 ? -d : d; return ar_sin[90 - (d > 90 ? 90 : d)]; }

static int ar_cleared;
static int8_t ar_grid[AR_ROWS][AR_COLS];           // -1 empty, else the colour (the player)
static uint8_t ar_shift;                           // parity of the rows (the ceiling came down)
static uint32_t ar_num[4];                         // the bubble tile of each colour (from the game)
static uint8_t ar_pal[4];                          // and its palette
static int ar_ang[4];                              // aim, degrees from the vertical
static struct { int on; int32_t x, y, vx, vy; } ar_fly[4];
static struct { int on, col; int32_t x, y, vy; } ar_fall[64];
static uint32_t ar_score[4], ar_clock, ar_end, ar_rng = 12345;
static uint32_t ar_prev[4];

// the game's sounds (tools/sound/sounds.txt): shot, stick, burst, fall, ready go, danger, alert, fanfare
static void ar_sound(uint32_t code) { neo_platform_sound(0x1A, code); }
#define AR_SND_SHOT   0x21
#define AR_SND_STICK  0x24
#define AR_SND_POP    0x22
#define AR_SND_FALL   0x25
#define AR_SND_READY  0x2F
#define AR_SND_DANGER 0x4E
#define AR_SND_ALERT  0x54
#define AR_SND_WIN    0x26

static uint32_t ar_rand(void) { ar_rng = ar_rng * 1103515245u + 12345u; return ar_rng >> 16; }
static int ar_cols(int r) { return ((r + ar_shift) & 1) ? AR_COLS - 1 : AR_COLS; }
static int32_t ar_cx(int r, int c) { return FX(AR_X + 8 + c * 16 + (((r + ar_shift) & 1) ? 8 : 0)); }
static int32_t ar_cy(int r) { return FX(AR_Y + 8 + r * AR_STEP); }
static int ar_ok(int r, int c) { return r >= 0 && r < AR_ROWS && c >= 0 && c < ar_cols(r); }

// the six neighbours of a cell (hexagonal grid, odd rows half a bubble right)
static int ar_nb(int r, int c, int *nr, int *nc) {
  int odd = (r + ar_shift) & 1, n = 0;
  static const int de[6][2] = {{0,-1},{0,1},{-1,-1},{-1,0},{1,-1},{1,0}}, dd[6][2] = {{0,-1},{0,1},{-1,0},{-1,1},{1,0},{1,1}};
  for (int k = 0; k < 6; k++) {
    int rr = r + (odd ? dd : de)[k][0], cc = c + (odd ? dd : de)[k][1];
    if (ar_ok(rr, cc)) { nr[n] = rr; nc[n] = cc; n++; }
  }
  return n;
}

// the bubbles cut from the ceiling fall (points to the player p)
static void ar_drop_loose(int p) {
  static uint8_t seen[AR_ROWS][AR_COLS]; static uint8_t qr[AR_ROWS * AR_COLS], qc[AR_ROWS * AR_COLS];
  int h = 0, t = 0, dropped = 0;
  for (int r = 0; r < AR_ROWS; r++) for (int c = 0; c < AR_COLS; c++) seen[r][c] = 0;
  for (int c = 0; c < ar_cols(0); c++) if (ar_grid[0][c] >= 0) { seen[0][c] = 1; qr[t] = 0; qc[t] = (uint8_t)c; t++; }
  while (h < t) {
    int nr[6], nc[6], n = ar_nb(qr[h], qc[h], nr, nc); h++;
    for (int k = 0; k < n; k++)
      if (!seen[nr[k]][nc[k]] && ar_grid[nr[k]][nc[k]] >= 0) { seen[nr[k]][nc[k]] = 1; qr[t] = (uint8_t)nr[k]; qc[t] = (uint8_t)nc[k]; t++; }
  }
  for (int r = 0; r < AR_ROWS; r++)
    for (int c = 0; c < ar_cols(r); c++)
      if (ar_grid[r][c] >= 0 && !seen[r][c]) {
        for (int i = 0; i < 64; i++) if (!ar_fall[i].on) {
          ar_fall[i].on = 1; ar_fall[i].col = ar_grid[r][c]; ar_fall[i].x = ar_cx(r, c); ar_fall[i].y = ar_cy(r); ar_fall[i].vy = 0; break;
        }
        ar_grid[r][c] = -1;
        if (p >= 0) ar_score[p] += 20;
        dropped++;
      }
  if (dropped) ar_sound(AR_SND_FALL);
}

// a bubble of player p sticks at cell (r, c): a group of 3 or more of its colour bursts
static void ar_stick(int p, int r, int c) {
  static uint8_t qr[AR_ROWS * AR_COLS], qc[AR_ROWS * AR_COLS], seen[AR_ROWS][AR_COLS];
  ar_grid[r][c] = (int8_t)p;
  for (int i = 0; i < AR_ROWS; i++) for (int j = 0; j < AR_COLS; j++) seen[i][j] = 0;
  int h = 0, t = 0; qr[t] = (uint8_t)r; qc[t] = (uint8_t)c; t++; seen[r][c] = 1;
  while (h < t) {
    int nr[6], nc[6], n = ar_nb(qr[h], qc[h], nr, nc); h++;
    for (int k = 0; k < n; k++)
      if (!seen[nr[k]][nc[k]] && ar_grid[nr[k]][nc[k]] == p) { seen[nr[k]][nc[k]] = 1; qr[t] = (uint8_t)nr[k]; qc[t] = (uint8_t)nc[k]; t++; }
  }
  if (t >= 3) {
    for (int i = 0; i < t; i++) ar_grid[qr[i]][qc[i]] = -1;
    ar_score[p] += 10 * (uint32_t)t;
    ar_sound(AR_SND_POP);
    ar_drop_loose(p);
  } else ar_sound(AR_SND_STICK);
  if (r >= AR_ROWS - 1)                            // at the bottom line: that row goes, no points
    for (int j = 0; j < AR_COLS; j++) ar_grid[AR_ROWS - 1][j] = -1;
}

// the nearest empty cell to (x, y) touching the ceiling or a bubble
static int ar_place(int32_t x, int32_t y, int *pr, int *pc) {
  int r0 = (int)(((y >> 8) - AR_Y - 8 + AR_STEP / 2) / AR_STEP), best = -1; int64_t bd = 0;
  for (int r = r0 - 1; r <= r0 + 1; r++)
    for (int c = 0; c < AR_COLS; c++) {
      if (!ar_ok(r, c) || ar_grid[r][c] >= 0) continue;
      int nr[6], nc[6], n = ar_nb(r, c, nr, nc), touch = r == 0;
      for (int k = 0; k < n && !touch; k++) touch = ar_grid[nr[k]][nc[k]] >= 0;
      if (!touch) continue;
      int64_t dx = ar_cx(r, c) - x, dy = ar_cy(r) - y, d = dx * dx + dy * dy;
      if (best < 0 || d < bd) { best = r * 32 + c; bd = d; }
    }
  if (best < 0) return 0;
  *pr = best / 32; *pc = best % 32; return 1;
}

static void ar_new_row(void) {                      // the ceiling comes down: a new row on top
  ar_sound(AR_SND_DANGER);
  for (int r = AR_ROWS - 1; r > 0; r--) for (int c = 0; c < AR_COLS; c++) ar_grid[r][c] = ar_grid[r - 1][c];
  ar_shift ^= 1;
  for (int c = 0; c < AR_COLS; c++) ar_grid[0][c] = c < ar_cols(0) ? (int8_t)(ar_rand() % arena_players) : -1;
  for (int c = 0; c < AR_COLS; c++) if (ar_grid[AR_ROWS - 1][c] >= 0) { ar_grid[AR_ROWS - 1][c] = -1; }
}

// the game's bubble graphics: each colour of bubble has its own tile, so the bubbles seen in the
// sprite list during the start of the game's round are noted (tile and palette, one tile, not
// empty), and the four colours are chosen among them, nearest in hue to green, blue, red, yellow
static uint32_t ar_seen_tile[64];
static uint8_t ar_src4;                            // the palette the yellow of player 4 comes from
static uint16_t ar_hue(uint16_t c, int target) {    // any saturated colour to the hue target (0-1535)
  int r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
  int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b), d = mx - mn;
  if (d < 6) return c;                             // greys, whites, blacks stay
  int sector = target >> 8, f = target & 255, p = mn, q = mx - (d * f) / 256, t = mn + (d * f) / 256, v = mx;
  switch (sector) {
  case 0: r = v; g = t; b = p; break;  case 1: r = q; g = v; b = p; break;
  case 2: r = p; g = v; b = t; break;  case 3: r = p; g = q; b = v; break;
  case 4: r = t; g = p; b = v; break;  default: r = v; g = p; b = q; break;
  }
  return (uint16_t)((r << 10) | (g << 5) | b);
}
static void ar_scan(void) {
  for (int n = LIST_FIRST; n < 381; n++) {
    if (sp_rows[n] != 1) continue;
    uint32_t t0 = neo_vram[n * 64], t1 = neo_vram[n * 64 + 1], pl = t1 >> 8;
    if (pl < 20 || pl > 40 || !t0) continue;
    ar_seen_tile[pl] = t0 | ((t1 & 0xF0) << 12);
  }
}
static void ar_take_graphics(void) {
  static const int want[4][3] = {{0, 31, 0}, {0, 8, 31}, {31, 0, 0}, {31, 31, 4}};
  const uint16_t *pal = neo_pal[neo.pal_bank];
  uint64_t taken = 0;
  for (int k = 0; k < 4; k++) {
    int best = -1, bd = 0;
    for (int pl = 20; pl <= 40; pl++) {
      if (!ar_seen_tile[pl] || (taken >> pl & 1)) continue;
      int r = 0, g = 0, b = 0;
      for (int i = 4; i <= 10; i++) { uint32_t c = rgb555(pal[pl * 16 + i]); r += (c >> 10) & 31; g += (c >> 5) & 31; b += c & 31; }
      r /= 7; g /= 7; b /= 7;
      int d = (r - want[k][0]) * (r - want[k][0]) + (g - want[k][1]) * (g - want[k][1]) + (b - want[k][2]) * (b - want[k][2]);
      if (best < 0 || d < bd) { best = pl; bd = d; }
    }
    if (best < 0) best = 29;                       // (never seen: whatever is there)
    taken |= 1ull << best;
    ar_pal[k] = (uint8_t)best; ar_num[k] = ar_seen_tile[best];
  }
  // player 4 is yellow: its bubble (often orange, a yellow one is rarely there) in palette 215
  // (never used by the game), turned yellow as the dragon of player 4
  ar_src4 = ar_pal[3]; ar_pal[3] = 215;
}

// The dragon and the launcher of the game, under each launcher of the arena: the sprites of the
// bottom of the fields of player 1 (Bub) and player 2 (Bob) on the frozen Neo Geo screen (not the
// bubbles), around the launcher's bubble, drawn at 3/4 of their size; Bob for players 2-4, in the
// colours of the 4-player screen (blue, red, yellow).
typedef struct { int16_t dx, dy; uint32_t num; uint8_t pal, flip; } ar_tile_t;
static ar_tile_t ar_stamp[2][96];
static int ar_stamps[2];
static void ar_take_stamp(int f) {
  int x0 = f ? 168 : 24, n_out = 0, ax = x0 + 64, ay = 200;
  for (int n = LIST_FIRST; n < 381; n++) {          // the launcher's bubble: the anchor
    if (sp_rows[n] != 1) continue;
    uint32_t pl = neo_vram[n * 64 + 1] >> 8; int x = sp_x[n] & 0x1FF, y = sp_top[n] & 0x1FF;
    if (pl >= 28 && pl <= 36 && x >= x0 + 40 && x < x0 + 88 && y >= 170 && y < 224) { ax = x + 8; ay = y + 8; break; }
  }
  for (int n = LIST_FIRST; n < 381 && n_out < 96; n++) {
    int rows = sp_rows[n] > 32 ? 32 : sp_rows[n], zy = sp_zy[n], x = sp_x[n] & 0x1FF;
    if (x >= 0x1E0) x -= 0x200;
    for (int k = 0; k < rows && n_out < 96; k++) {
      int y = (sp_top[n] + ((k * 16 * (zy + 1)) >> 8)) & 0x1FF;
      uint32_t t0 = neo_vram[n * 64 + k * 2], t1 = neo_vram[n * 64 + k * 2 + 1], pl = t1 >> 8;
      if (!(t0 | t1) || (pl >= 28 && pl <= 36) || pl >= 116) continue;   // no bubble, no counter
      if (x < x0 - 16 || x >= x0 + 144 || y < 160 || y >= 240) continue;
      int dx = x + 8 - ax, dy = y + 8 - ay;
      if (dx < -72 || dx > 72) continue;
      ar_tile_t *t = &ar_stamp[f][n_out++];
      t->dx = (int16_t)dx; t->dy = (int16_t)dy; t->num = t0 | ((t1 & 0xF0) << 12); t->pal = (uint8_t)pl; t->flip = (uint8_t)(t1 & 3);
    }
  }
  ar_stamps[f] = n_out;
  int d16 = 0, d21 = 0; for (int i = 0; i < n_out; i++) { d16 += ar_stamp[f][i].pal == 16; d21 += ar_stamp[f][i].pal == 21; }
  mc_debug("STAMP %d: %d tiles, anchor %d %d, dragon tiles %d %d\n", f, n_out, ax, ay, d16, d21);
}
static void ar_draw_stamp(int *out, int k) {
  const uint16_t *smap = (const uint16_t *)spr_map_file;
  int f = k ? 1 : 0;
  int32_t lx = AR_X + 40 + 80 * k, ly = AR_LY - 10;
  for (int i = 0; i < ar_stamps[f] && *out < MC_SPRITES; i++) {
    ar_tile_t *t = &ar_stamp[f][i];
    int slot = cache_get(smap[t->num]);
    if (slot < 0) continue;
    uint32_t pal = t->pal;
    if (pal == DRAGON_PAL && k >= 2) pal = k == 2 ? DRAGON3_PAL : DRAGON4_PAL;
    sprite_t *s = &mc_spr[(*out)++];
    s->x = (int16_t)(lx + ((t->dx * 192) >> 8) - 6); s->y = (int16_t)(ly + ((t->dy * 192) >> 8) - 6);
    s->tile = (uint16_t)(slot * 4); s->pal = (uint8_t)pal; s->w = 2; s->h = 2;
    s->attr = (uint8_t)(SA_ON | SA_PRIO(SPR_PRIO) | ((t->flip & 1) ? SA_HFLIP : 0) | ((t->flip & 2) ? SA_VFLIP : 0));
    s->zoomx = 192; s->zoomy = 192;
  }
}

// The decor of the stage behind the board: the inside of the field of player 1 (120 x 208 source
// pixels, inside its frame), at its own size, side by side across the board; its palettes follow the
// game's (decor_frame's slots 240-254)
static const layout_t ar_layout = {{0}, {0}, 256, 256, 15, 26, 16, 0, 16, 16, {0}, {0}, 0, 0, 24};
static void ar_decor(void) {
  const layout_t *keep = L; L = &ar_layout;
  decor_rows(0, 26, 1);
  L = keep;
  for (int i = 0; i < 64 * 64; i++) TILEMAP(1)[i] = MC_TILE(DECOR_W * DECOR_H, DECOR_PAL, 0);
  for (int x = AR_X; x < AR_X + 320; x += 120)     // (the inside of the frame only: no seam)
    for (int cy = 0; cy < 26; cy++)
      for (int cx = 0; cx < 15 && x + cx * 8 < AR_X + 320; cx++)
        TILEMAP(1)[(AR_Y / 8 + cy) * 64 + x / 8 + cx] = MC_TILE(cy * 15 + cx, DECOR_PAL, 0);
}

static void arena_start(void) {
  ar_take_graphics();
  ar_take_stamp(0); ar_take_stamp(1);
  for (int r = 0; r < AR_ROWS; r++) for (int c = 0; c < AR_COLS; c++)
    ar_grid[r][c] = (r < 7 && c < ar_cols(r)) ? (int8_t)(ar_rand() % arena_players) : -1;
  for (int k = 0; k < 4; k++) { ar_ang[k] = 0; ar_fly[k].on = 0; ar_score[k] = 0; ar_prev[k] = 0xFFFFFFFF; }
  for (int i = 0; i < 64; i++) ar_fall[i].on = 0;
  ar_clock = AR_TIME; ar_end = 0;
  for (int i = 0; i < 64 * 64; i++) { TILEMAP(0)[i] = MC_TILE(FIX_EMPTY, 0, 0); TILEMAP(1)[i] = MC_TILE(DECOR_W * DECOR_H, DECOR_PAL, 0); }
  arena_on = 1; ar_cleared = 0;
  mc_debug("ARENA frame %u: %d players, bubbles %X/%u %X/%u %X/%u %X/%u\n", neo.frame, arena_players, ar_num[0], ar_pal[0], ar_num[1], ar_pal[1], ar_num[2], ar_pal[2], ar_num[3], ar_pal[3]);
}

static void ar_text_score(void) {
  static const uint8_t col[4] = {1, 9, 31, 39};
  for (int k = 0; k < arena_players; k++) {
    char t[8] = "P1 0000"; t[1] = (char)('1' + k);
    uint32_t v = ar_score[k] > 9999 ? 9999 : ar_score[k];
    for (int i = 6; i >= 3; i--) { t[i] = (char)('0' + v % 10); v /= 10; }
    hud_text(col[k], 1, t, HUD_PAL + k);
  }
  uint32_t s = (ar_clock + 59) / 60; char t[6] = "0:00";
  t[0] = (char)('0' + s / 60); t[2] = (char)('0' + (s % 60) / 10); t[3] = (char)('0' + s % 10);
  hud_text(22, 1, t, HUD_PAL + 3);
}

static void ar_sprite(int *out, int32_t x, int32_t y, int k, uint16_t zoom) {   // one bubble, centred on x, y
  if (*out >= MC_SPRITES) return;
  const uint16_t *smap = (const uint16_t *)spr_map_file;
  int slot = cache_get(smap[ar_num[k]]);
  if (slot < 0) return;
  int size = (16 * zoom) >> 8;
  sprite_t *s = &mc_spr[(*out)++];
  s->x = (int16_t)((x >> 8) - size / 2); s->y = (int16_t)((y >> 8) - size / 2);
  s->tile = (uint16_t)(slot * 4); s->pal = ar_pal[k]; s->w = 2; s->h = 2;
  s->attr = (uint8_t)(SA_ON | SA_PRIO(SPR_PRIO)); s->zoomx = zoom; s->zoomy = zoom;
}

static void arena_frame(void) {
  uint32_t in[4] = {IN_P1, IN_P2, IN_P3, IN_P4};
  if (!ar_cleared) {                               // the panels of the 4-player screen go; the decor
    for (int i = 0; i < 64 * 64; i++) TILEMAP(0)[i] = MC_TILE(FIX_EMPTY, 0, 0);
    ar_decor();
    ar_sound(AR_SND_READY);
    ar_cleared = 1;
  }
  { const uint16_t *p = neo_pal[neo.pal_bank];     // the palettes of the decor, the dragons
    for (int k = 0; k < decor_slots; k++)
      for (int c = 0; c < 16; c++) PALETTE[(DECOR_PAL + k) * 16 + c] = rgb555(p[decor_pal_of[k] * 16 + c]);
    dragons_frame();
    for (int i = 0; i < 16; i++) PALETTE[215 * 16 + i] = i ? ar_hue(rgb555(p[ar_src4 * 16 + i]), 250) : 0; }
  if (ar_end) {                                    // the results, then the console starts again
    if (--ar_end == 1) arena_restart = 1;
  } else {
    if (ar_clock) ar_clock--;
    if (ar_clock && ar_clock <= 10 * 60 && ar_clock % 60 == 0) ar_sound(AR_SND_ALERT);   // the last 10 s
    if (!ar_clock) {
      ar_end = 8 * 60;
      int best = 0; for (int k = 1; k < arena_players; k++) if (ar_score[k] > ar_score[best]) best = k;
      char t[] = "PLAYER 1 WINS!"; t[7] = (char)('1' + best);
      hud_text(13, 17, t, HUD_PAL + best);
      ar_sound(AR_SND_WIN);
    }
    if (ar_clock && ar_clock % AR_DROP == 0) ar_new_row();
    for (int k = 0; k < arena_players; k++) {      // aim and shoot
      if (in[k] & IN_LEFT) ar_ang[k] -= 2;
      if (in[k] & IN_RIGHT) ar_ang[k] += 2;
      if (ar_ang[k] < -75) ar_ang[k] = -75;
      if (ar_ang[k] > 75) ar_ang[k] = 75;
      uint32_t e = in[k] & ~ar_prev[k];
      if ((e & (IN_B(1) | IN_UP)) && !ar_fly[k].on && ar_clock) {
        ar_fly[k].on = 1; ar_fly[k].x = FX(AR_X + 40 + 80 * k); ar_fly[k].y = FX(AR_LY - 10);
        ar_fly[k].vx = ar_sinv(ar_ang[k]) * 6; ar_fly[k].vy = -ar_cosv(ar_ang[k]) * 6;
        ar_sound(AR_SND_SHOT);
      }
      ar_prev[k] = in[k];
    }
    for (int step = 0; step < 3; step++) {          // the bubbles in flight, in three steps
      for (int k = 0; k < 4; k++) {
        if (!ar_fly[k].on) continue;
        ar_fly[k].x += ar_fly[k].vx / 3; ar_fly[k].y += ar_fly[k].vy / 3;
        if (ar_fly[k].x < FX(AR_X + 8)) { ar_fly[k].x = 2 * FX(AR_X + 8) - ar_fly[k].x; ar_fly[k].vx = -ar_fly[k].vx; }
        if (ar_fly[k].x > FX(AR_X + 320 - 8)) { ar_fly[k].x = 2 * FX(AR_X + 320 - 8) - ar_fly[k].x; ar_fly[k].vx = -ar_fly[k].vx; }
        if (ar_fly[k].y > FX(AR_LY)) ar_fly[k].on = 0;   // went down (after a bounce): lost
        int hit = ar_fly[k].y <= FX(AR_Y + 8);
        int r0 = (int)(((ar_fly[k].y >> 8) - AR_Y - 8) / AR_STEP);
        for (int r = r0 - 1; r <= r0 + 1 && !hit; r++)
          for (int c = 0; c < AR_COLS && !hit; c++) {
            if (!ar_ok(r, c) || ar_grid[r][c] < 0) continue;
            int64_t dx = ar_cx(r, c) - ar_fly[k].x, dy = ar_cy(r) - ar_fly[k].y;
            if (dx * dx + dy * dy < (int64_t)FX(14) * FX(14)) hit = 1;
          }
        if (hit) {
          int r, c;
          if (ar_place(ar_fly[k].x, ar_fly[k].y, &r, &c)) ar_stick(k, r, c);
          ar_fly[k].on = 0;
        }
      }
      for (int i = 0; i < 4; i++)                  // two bubbles in flight bounce on each other
        for (int j = i + 1; j < 4; j++) {
          if (!ar_fly[i].on || !ar_fly[j].on) continue;
          int64_t dx = ar_fly[j].x - ar_fly[i].x, dy = ar_fly[j].y - ar_fly[i].y, d2 = dx * dx + dy * dy;
          if (d2 >= (int64_t)FX(16) * FX(16) || d2 == 0) continue;
          int64_t rvx = ar_fly[j].vx - ar_fly[i].vx, rvy = ar_fly[j].vy - ar_fly[i].vy, dot = rvx * dx + rvy * dy;
          if (dot >= 0) continue;                  // moving apart already
          int64_t ix = dot * dx / d2, iy = dot * dy / d2;   // equal masses: swap the parts along the line
          ar_fly[i].vx += (int32_t)ix; ar_fly[i].vy += (int32_t)iy;
          ar_fly[j].vx -= (int32_t)ix; ar_fly[j].vy -= (int32_t)iy;
        }
    }
    for (int i = 0; i < 64; i++)                   // the bubbles falling
      if (ar_fall[i].on) { ar_fall[i].vy += FX(1) / 4; ar_fall[i].y += ar_fall[i].vy; if (ar_fall[i].y > FX(300)) ar_fall[i].on = 0; }
  }
  // drawing
  int out = 0;
  for (int k = 0; k < 4; k++) if (ar_fly[k].on) ar_sprite(&out, ar_fly[k].x, ar_fly[k].y, k, 256);
  for (int i = 0; i < 64; i++) if (ar_fall[i].on) ar_sprite(&out, ar_fall[i].x, ar_fall[i].y, ar_fall[i].col, 256);
  for (int k = 0; k < arena_players; k++) {        // the launchers: the next bubble and the aim
    int32_t x = FX(AR_X + 40 + 80 * k), y = FX(AR_LY - 10);
    if (!ar_fly[k].on) ar_sprite(&out, x, y, k, 256);
    for (int d = 1; d <= 5; d++)
      ar_sprite(&out, x + ar_sinv(ar_ang[k]) * 7 * d, y - ar_cosv(ar_ang[k]) * 7 * d, k, 80);
  }
  for (int r = 0; r < AR_ROWS; r++)
    for (int c = 0; c < ar_cols(r); c++)
      if (ar_grid[r][c] >= 0) ar_sprite(&out, ar_cx(r, c), ar_cy(r), ar_grid[r][c], 256);
  for (int k = 0; k < arena_players; k++) ar_draw_stamp(&out, k);   // behind the bubbles
  mc_sprites_used(out);
  mc_copy(SPRITES, mc_spr, (uint32_t)out * sizeof(sprite_t));
  ar_text_score();
  DISP_CTRL = DISP_ON | DISP_SPRITES | DISP_LAYER(0) | DISP_LAYER(1);
}
