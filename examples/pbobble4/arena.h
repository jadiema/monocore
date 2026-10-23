// ARENA: a mode of our own, written in C (the game's graphics, our rules). The four dragons play on
// one board: each shoots bubbles of its own colour (player 1 green, 2 blue, 3 red, 4 yellow) and
// bursts only groups of 3 of its colour; a bubble that touches another colour sticks there. The
// bubbles in flight bounce on the walls and on each other. Bubbles cut from the ceiling fall and
// count for the player who cut them. The ceiling comes down every 20 s. 2 minutes; the best score
// wins. The game is started as a game of two (it loads its decor, palettes and bubble graphics);
// then its fields (tasks 5, 6, 7, 9) are suspended and it gets no input, while the rest of it runs
// on: its decor (clouds moving) and music, drawn behind the board; at the end the console starts
// again from its reset.

#define AR_COLS   20                               // even rows 20 bubbles, odd rows 19 (half a bubble in)
#define AR_ROWS   15
#define AR_X      32                               // the board: 320 pixels wide
#define AR_Y      34
#define AR_STEP   14                               // between two rows
#define AR_LY     262                              // the launchers
#define AR_TIME   (120 * 60)
#define AR_DROP   (20 * 60)                        // the ceiling comes down
#define FX(v)     ((int32_t)(v) << 8)              // 24.8 fixed point
#define AR_DX     AR_X                             // the game's 320-pixel screen under the board
#define AR_DY     20

enum { AR_REST, AR_SHOOT, AR_HAPPY, AR_WORRY };     // the moods of a dragon (its scripts, below)
static void ar_dragon_play(int k, int mood, int loop);
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
static struct { int t, col; int32_t x, y; } ar_pop[64];   // bubbles bursting (as the game: grey, blinking)
#define AR_POP_T    18                            // 6 frames of the game's burst, 3 each
#define AR_GREY_PAL 217                            // 217-220: the grey of each colour
static uint32_t ar_score[4], ar_clock, ar_end, ar_rng = 12345;
static uint32_t ar_prev[4];
static uint32_t ar_start;                          // frames of READY / GO before the first shot
static uint8_t ar_jump[4];                         // a dragon jumps when its player shoots
#define AR_READY  90

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

// the points won, written where they were won, in the colour of the player, rising for 0.4 s
static struct { int8_t col, row, k; uint8_t t, len, shown_row; uint16_t val; } ar_pts[8];
static void ar_points(int k, int32_t x, int32_t y, uint32_t val) {
  for (int i = 0; i < 8; i++) if (!ar_pts[i].t) {
    ar_pts[i].col = (int8_t)(((x >> 8) / 8) - 1); ar_pts[i].row = (int8_t)((y >> 8) / 8);
    ar_pts[i].k = (int8_t)k; ar_pts[i].t = 24; ar_pts[i].val = (uint16_t)(val > 9999 ? 9999 : val);
    ar_pts[i].len = 0; return;
  }
}
static void ar_points_frame(void) {
  for (int i = 0; i < 8; i++) {
    if (!ar_pts[i].len && !ar_pts[i].t) continue;
    for (int c = 0; c < ar_pts[i].len; c++)        // where it was: cleared
      TILEMAP(0)[ar_pts[i].shown_row * 64 + ar_pts[i].col + c] = MC_TILE(FIX_EMPTY, 0, 0);
    ar_pts[i].len = 0;
    if (!ar_pts[i].t) continue;
    ar_pts[i].t--;
    char t[6]; int n = 0; uint32_t v = ar_pts[i].val, d = 1000; t[n++] = '+';
    while (d > v && d > 1) d /= 10;
    for (; d; d /= 10) t[n++] = (char)('0' + (v / d) % 10);
    t[n] = 0;
    int row = ar_pts[i].row - (24 - ar_pts[i].t) / 8;
    if (row < 3 || ar_pts[i].col < 4 || ar_pts[i].col + n > 44) continue;
    hud_text(ar_pts[i].col, row, t, HUD_PAL + ar_pts[i].k);
    ar_pts[i].len = (uint8_t)n; ar_pts[i].shown_row = (uint8_t)row;
  }
}

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
static int32_t ar_fall_x, ar_fall_y;                // where the last bubble cut was
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
          ar_fall[i].on = 1; ar_fall[i].col = ar_grid[r][c]; ar_fall[i].x = ar_cx(r, c); ar_fall[i].y = ar_cy(r); ar_fall[i].vy = 0;
          ar_fall_x = ar_fall[i].x; ar_fall_y = ar_fall[i].y; break;
        }
        ar_grid[r][c] = -1;
        if (p >= 0) ar_score[p] += 20;
        dropped++;
      }
  if (dropped) ar_sound(AR_SND_FALL);
  if (dropped && p >= 0) ar_points(p, ar_fall_x, ar_fall_y + FX(12), 20 * (uint32_t)dropped);
  if (dropped >= 5 && p >= 0) ar_dragon_play(p, AR_HAPPY, 0);
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
    for (int i = 0; i < t; i++) {
      for (int j = 0; j < 64; j++) if (!ar_pop[j].t) {
        ar_pop[j].t = AR_POP_T; ar_pop[j].col = p; ar_pop[j].x = ar_cx(qr[i], qc[i]); ar_pop[j].y = ar_cy(qr[i]); break;
      }
      ar_grid[qr[i]][qc[i]] = -1;
    }
    ar_score[p] += 10 * (uint32_t)t;
    ar_points(p, ar_cx(r, c), ar_cy(r), 10 * (uint32_t)t);
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
// The game's own animations of the bubbles, read from its scripts in the ROM at the start of the
// arena (the format of its animation engine, $6FE14-$6FEF2: words; 0-4 a frame: 5-1 parameters, a
// duration, a sprite definition; -1 a loop; -2 the end; -3 a call). Versus: a bubble at rest
// ($44C8A, by colour: 8 tiles of 8 frames, all the bubbles of a colour in step) and a bubble
// bursting ($43464: frames of 3 x 3 tiles, 3 frames each).
#define AR_IDLE_T   0x44C8A
#define AR_BURST_T  0x43464
static uint32_t ar_idle[4][8], ar_idles[4];        // the tiles at rest, per colour
static uint32_t ar_burst[4][8][9], ar_bursts[4];   // the frames of a burst: 3 x 3 tiles each
static uint8_t ar_burst_flip[4][8][9];
static uint16_t ar_r16(uint32_t a);
// The launcher of the game, from its ROM: the needle (definitions by angle at $626A2: 4 x 4 tiles,
// angle 0 upright to 60 leaning; leaning left with a mirror), at (-32, -32) from the launcher's
// bubble; the wheel (9 frames turning as the needle moves, at $3D990: 6 x 4 tiles), at (-48, -24)
// (the list gives y 32 lines above the screen's).
// Palettes 76 and 80.
#define AR_NEEDLE_T 0x626A2
#define AR_WHEEL_T  0x3D990
static int ar_wheel[4];                            // how far each wheel has turned
static uint16_t ar_r16(uint32_t a) { return *(const uint16_t *)(neo_rom + (a & ~1u)); }
static uint32_t ar_r32(uint32_t a) { return (uint32_t)ar_r16(a) << 16 | ar_r16(a + 2); }
static uint32_t ar_tile_of(uint32_t d, int i) { uint32_t t0 = ar_r16(d + 2 + 4 * i), t1 = ar_r16(d + 4 + 4 * i); return t0 | ((t1 & 0xF0) << 12); }
static void ar_read_scripts(int k, int colour) {   // colour: the game's 0-7 (palette 29-36)
  static const int8_t nparam[5] = {5, 4, 3, 2, 1};
  for (int pass = 0; pass < 2; pass++) {
    uint32_t a = ar_r32((pass ? AR_BURST_T : AR_IDLE_T) + 4 * colour), n = 0;
    for (int step = 0; step < 24 && n < 8; step++) {
      int op = (int16_t)ar_r16(a); a += 2;
      if (op < 0 || op > 4) break;                 // a loop, the end, a call: the frames are read
      a += 2 * nparam[op];
      uint32_t dur = ar_r16(a), d = ar_r32(a + 2); a += 6;
      if (d >= neo_rom_size) break;
      uint32_t size = ar_r16(d);
      if (!pass) { if (size) break; ar_idle[k][n++] = ar_tile_of(d, 0); continue; }
      if (((size & 0xFF) != 2) || (((size >> 8) & 0xFF) != 2)) break;   // the burst: its 3 x 3 frames
      if (!dur) continue;                          // (a frame that waits: shown by the next one)
      for (int i = 0; i < 9; i++) { ar_burst[k][n][i] = ar_tile_of(d, i); ar_burst_flip[k][n][i] = (uint8_t)(ar_r16(d + 4 + 4 * i) & 3); }
      n++;
    }
    if (pass) ar_bursts[k] = n; else ar_idles[k] = n;
  }
}
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
  for (int k = 0; k < 4; k++)                       // the game's colour of each: its palette
    if (ar_pal[k] >= 29 && ar_pal[k] <= 36) ar_read_scripts(k, ar_pal[k] - 29);
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
      if ((pl == 76 && dy < 0 && dx > -32 && dx < 32) || pl == 80 || pl == 16 || pl == 21) continue;   // needle, wheel, dragon: drawn moving
      ar_tile_t *t = &ar_stamp[f][n_out++];
      t->dx = (int16_t)dx; t->dy = (int16_t)dy; t->num = t0 | ((t1 & 0xF0) << 12); t->pal = (uint8_t)pl; t->flip = (uint8_t)(t1 & 3);
    }
  }
  ar_stamps[f] = n_out;
  int d16 = 0, d21 = 0; for (int i = 0; i < n_out; i++) { d16 += ar_stamp[f][i].pal == 16; d21 += ar_stamp[f][i].pal == 21; }
  mc_debug("STAMP %d: %d tiles, anchor %d %d, dragon tiles %d %d\n", f, n_out, ax, ay, d16, d21);
}
static void ar_block(int *out, uint32_t d, int32_t x0, int32_t y0, int flip, uint8_t pal) {   // a definition, 3/4 size
  const uint16_t *smap = (const uint16_t *)spr_map_file;
  uint32_t size = ar_r16(d), cols = (size & 0xFF) + 1, rows = ((size >> 8) & 0xFF) + 1;
  for (uint32_t c = 0; c < cols; c++)
    for (uint32_t r = 0; r < rows && *out < MC_SPRITES; r++) {
      uint32_t i = c * rows + r, num = ar_tile_of(d, (int)i), t1 = ar_r16(d + 4 + 4 * i);
      if (!num || !smap[num]) continue;
      int slot = cache_get(smap[num]);
      if (slot < 0) continue;
      int cc = flip ? (int)(cols - 1 - c) : (int)c, fl = (int)(t1 & 3) ^ (flip ? 1 : 0);
      sprite_t *s = &mc_spr[(*out)++];
      s->x = (int16_t)(x0 + ((cc * 16 * 192) >> 8)); s->y = (int16_t)(y0 + (((int)r * 16 * 192) >> 8));
      s->tile = (uint16_t)(slot * 4); s->pal = pal; s->w = 2; s->h = 2;
      s->attr = (uint8_t)(SA_ON | SA_PRIO(SPR_PRIO) | ((fl & 1) ? SA_HFLIP : 0) | ((fl & 2) ? SA_VFLIP : 0));
      s->zoomx = 192; s->zoomy = 192;
    }
}
static void ar_draw_launcher(int *out, int k) {    // the needle at the aim, the wheel turning
  int32_t lx = AR_X + 40 + 80 * k, ly = AR_LY - 10;
  int a = ar_ang[k] < 0 ? -ar_ang[k] : ar_ang[k], idx = a * 60 / 75;
  ar_block(out, ar_r32(AR_NEEDLE_T + 4 * idx), lx + ((-32 * 192) >> 8), ly + ((-32 * 192) >> 8), ar_ang[k] < 0, 76);
  int w = ((ar_wheel[k] / 3) % 9 + 9) % 9;
  ar_block(out, ar_r32(AR_WHEEL_T + 4 * w), lx + ((-48 * 192) >> 8), ly + ((-24 * 192) >> 8), 0, 80);
}
// The dragons of the game, by their scripts in the ROM (the routine of a dragon, $42C3C, plays one
// script per mood of its field): at rest $42E4A (it blinks), shooting $42EA6, happy $4307E (it
// jumps), worried $42F76. Blocks of 2 x 2 tiles and more, at the height of the launcher's bubble:
// Bub (player 1, palette 16, mirrored) 56 pixels left of it, Bob (21) 24 pixels right.
static const uint32_t ar_drag_script[4] = {0x42E4A, 0x42EA6, 0x4307E, 0x42F76};
static uint32_t ar_drag_def[4][16]; static uint8_t ar_drag_dur[4][16], ar_drag_n[4];
static struct { uint8_t mood, frame, loop; uint16_t timer; } ar_drag[4];
static void ar_read_dragon(void) {
  static const int8_t nparam[5] = {5, 4, 3, 2, 1};
  for (int m = 0; m < 4; m++) {
    uint32_t a = ar_drag_script[m]; int n = 0;
    for (int step = 0; step < 32 && n < 16; step++) {
      int op = (int16_t)ar_r16(a); a += 2;
      if (op < 0 || op > 4) break;
      a += 2 * nparam[op];
      uint32_t dur = ar_r16(a), d = ar_r32(a + 2); a += 6;
      if (d >= neo_rom_size) break;
      ar_drag_def[m][n] = d; ar_drag_dur[m][n] = (uint8_t)(dur ? dur : 1); n++;
    }
    ar_drag_n[m] = (uint8_t)n;
  }
}
static void ar_dragon_play(int k, int mood, int loop) {
  if (!ar_drag_n[mood]) return;
  ar_drag[k].mood = (uint8_t)mood; ar_drag[k].frame = 0; ar_drag[k].loop = (uint8_t)loop;
  ar_drag[k].timer = ar_drag_dur[mood][0];
}
static void ar_dragon_step(int k) {
  if (!ar_drag_n[ar_drag[k].mood] || --ar_drag[k].timer) return;
  if (++ar_drag[k].frame >= ar_drag_n[ar_drag[k].mood]) {
    if (ar_drag[k].loop) ar_drag[k].frame = 0; else { ar_dragon_play(k, AR_REST, 1); return; }
  }
  ar_drag[k].timer = ar_drag_dur[ar_drag[k].mood][ar_drag[k].frame];
}
static void ar_draw_dragon(int *out, int k) {
  if (!ar_drag_n[ar_drag[k].mood]) return;
  int32_t lx = AR_X + 40 + 80 * k, ly = AR_LY - 10;
  uint8_t pal = k == 0 ? 16 : k == 2 ? DRAGON3_PAL : k == 3 ? DRAGON4_PAL : DRAGON_PAL;
  int dx = k ? 24 : -56;
  ar_block(out, ar_drag_def[ar_drag[k].mood][ar_drag[k].frame], lx + ((dx * 192) >> 8), ly, k == 0, pal);
}

// The still parts of the four launchers (the machines, the NEXT signs: the stamps) are drawn once
// into an image on layer 1 (cells of 8 x 8, 8 bpp, in the decor area, unused in the arena): drawn as
// sprites they made more than 64 sprites on the lines of the launchers. With them the black bands
// on both sides of the board (one black cell): the landscape ends on its edges (the sky scrolls a
// little wider, and a layer cannot cut its 16-pixel tiles). Its palettes follow the game's, in the
// slots 240-254. Layer 1 is the arena's: the game's decor uses layers 2 and 3 only.
#define AR_IMG_Y    224                            // the image: 320 x 64 pixels under the board
#define AR_IMG_W    40
#define AR_IMG_H    8
#define AR_CELL_BLACK (AR_IMG_W * AR_IMG_H)        // then a black cell, then an empty one
#define AR_CELL_EMPTY (AR_CELL_BLACK + 1)
static uint8_t ar_img[8][AR_IMG_W * 8];            // one row of cells at a time
static int ar_img_row;
static uint8_t ar_slot_of[256], ar_slot_pal[15], ar_slots;
static void ar_img_tile(uint32_t num, uint8_t pal, int flip, int x0, int y0) {   // a tile at 3/4 size
  const uint16_t *smap = (const uint16_t *)spr_map_file;
  if (!smap[num]) return;
  for (int y = 0; y < 12; y++)
    for (int x = 0; x < 12; x++) {
      int sx = x * 16 / 12, sy = y * 16 / 12;
      if (flip & 1) sx = 15 - sx;
      if (flip & 2) sy = 15 - sy;
      const uint8_t *row = spr_file + smap[num] * 128 + sy * 8;
      int pix = (row[sx >> 1] >> ((sx & 1) * 4)) & 15, ix = x0 + x - AR_X, iy = y0 + y - AR_IMG_Y - ar_img_row * 8;
      if (!pix || ix < 0 || ix >= AR_IMG_W * 8 || iy < 0 || iy >= 8) continue;
      if (!ar_slot_of[pal]) { if (ar_slots >= 15) continue; ar_slot_pal[ar_slots] = pal; ar_slot_of[pal] = ++ar_slots; }
      ar_img[iy][ix] = (uint8_t)(((ar_slot_of[pal] - 1) << 4) | pix);
    }
}
static void ar_compose(void) {
  static uint8_t cell[64];
  for (int i = 0; i < 256; i++) ar_slot_of[i] = 0;
  ar_slots = 0;
  for (int cy = 0; cy < AR_IMG_H; cy++) {           // row of cells by row of cells
    ar_img_row = cy;
    for (int y = 0; y < 8; y++) for (int x = 0; x < AR_IMG_W * 8; x++) ar_img[y][x] = 0;
    for (int k = 0; k < arena_players; k++) {
      int f = k ? 1 : 0; int32_t lx = AR_X + 40 + 80 * k, ly = AR_LY - 10;
      for (int i = 0; i < ar_stamps[f]; i++) {
        ar_tile_t *t = &ar_stamp[f][i];
        ar_img_tile(t->num, t->pal, t->flip, lx + ((t->dx * 192) >> 8) - 6, ly + ((t->dy * 192) >> 8) - 6);
      }
    }
    for (int cx = 0; cx < AR_IMG_W; cx++) {
      for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) cell[y * 8 + x] = ar_img[y][cx * 8 + x];
      mc_copy((volatile void *)(DECOR_BASE + (cy * AR_IMG_W + cx) * 64), cell, 64);
    }
  }
  for (int i = 0; i < 64; i++) cell[i] = 0xFE;    // black: palette slot 15, entry 14 (set to black)
  mc_copy((volatile void *)(DECOR_BASE + AR_CELL_BLACK * 64), cell, 64);
  for (int i = 0; i < 64; i++) cell[i] = 0;
  mc_copy((volatile void *)(DECOR_BASE + AR_CELL_EMPTY * 64), cell, 64);
  mc_layer(1, MC_CHR(DECOR_BASE - CHRRAM_BASE), MC_TILE8 | MC_8BPP, 64, 64, 0);
  for (int y = 0; y < 64; y++)
    for (int x = 0; x < 64; x++) {
      int sx = x * 8, sy = y * 8; uint32_t c = AR_CELL_EMPTY;
      if (sx < AR_X || (sx >= AR_X + 320 && sx < 384)) c = AR_CELL_BLACK;
      else if (sx < AR_X + 320 && sy >= AR_IMG_Y && sy < AR_IMG_Y + AR_IMG_H * 8) c = (uint32_t)((sy - AR_IMG_Y) / 8 * AR_IMG_W + (sx - AR_X) / 8);
      TILEMAP(1)[y * 64 + x] = MC_TILE(c, DECOR_PAL, 0);
    }
}
static void ar_img_palettes(void) {                // the image's palettes, with the game's fades
  const uint16_t *p = neo_pal[neo.pal_bank];
  for (int k = 0; k < ar_slots; k++)
    for (int c = 1; c < 16; c++) PALETTE[(DECOR_PAL + k) * 16 + c] = rgb555(p[ar_slot_pal[k] * 16 + c]);
  PALETTE[(DECOR_PAL + 15) * 16 + 14] = 0;          // the black cell
}

static void arena_start(void) {
  ar_take_graphics();
  ar_take_stamp(0); ar_take_stamp(1);
  ar_read_dragon();
  for (int k = 0; k < 4; k++) ar_dragon_play(k, AR_REST, 1);

  uint32_t mask = ((uint32_t)ram16(0x1034C6) << 16) | ram16(0x1034C8);   // the fields stop (scheduler mask)
  mask &= ~((1u << 5) | (1u << 6) | (1u << 7) | (1u << 9));
  *(uint16_t *)(neo_ram + 0x34C6) = (uint16_t)(mask >> 16); *(uint16_t *)(neo_ram + 0x34C8) = (uint16_t)mask;
  for (int r = 0; r < AR_ROWS; r++) for (int c = 0; c < AR_COLS; c++)
    ar_grid[r][c] = (r < 7 && c < ar_cols(r)) ? (int8_t)(ar_rand() % arena_players) : -1;
  for (int k = 0; k < 4; k++) { ar_ang[k] = 0; ar_fly[k].on = 0; ar_score[k] = 0; ar_prev[k] = 0xFFFFFFFF; }
  for (int i = 0; i < 64; i++) { ar_fall[i].on = 0; ar_pop[i].t = 0; }
  for (int i = 0; i < 8; i++) { ar_pts[i].t = 0; ar_pts[i].len = 0; }
  ar_clock = AR_TIME; ar_end = 0; ar_start = AR_READY;
  for (int k = 0; k < 4; k++) ar_jump[k] = 0;
  for (int i = 0; i < 64 * 64; i++) { TILEMAP(0)[i] = MC_TILE(FIX_EMPTY, 0, 0); TILEMAP(1)[i] = MC_TILE(DECOR_W * DECOR_H, DECOR_PAL, 0); }
  arena_on = 1; ar_cleared = 0;
  mc_debug("ARENA frame %u: %d players, bubbles %X/%u %X/%u %X/%u %X/%u\n", neo.frame, arena_players, ar_num[0], ar_pal[0], ar_num[1], ar_pal[1], ar_num[2], ar_pal[2], ar_num[3], ar_pal[3]);
}

static void ar_text_score(void) {
  static const uint8_t col[4] = {4, 12, 29, 37};   // inside the board (columns 4-43)
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

static void ar_sprite_pal(int *out, int32_t x, int32_t y, int k, uint16_t zoom, uint8_t pal);
static void ar_sprite(int *out, int32_t x, int32_t y, int k, uint16_t zoom) { ar_sprite_pal(out, x, y, k, zoom, ar_pal[k]); }
// a bubble of the board: 14 lines high (its rows are 14 lines apart: a line crosses one row, not
// two, or 2 x 20 bubbles plus the bursts passed the 64 sprites of a line)
static void ar_sprite_h(int *out, int32_t x, int32_t y, int k) {
  int before = *out;
  ar_sprite(out, x, y, k, 256);
  if (*out > before) { mc_spr[before].zoomy = 224; mc_spr[before].y += 1; }
}
static void ar_sprite_pal(int *out, int32_t x, int32_t y, int k, uint16_t zoom, uint8_t pal) {   // one bubble, centred
  if (*out >= MC_SPRITES) return;
  const uint16_t *smap = (const uint16_t *)spr_map_file;
  uint32_t num = ar_idles[k] ? ar_idle[k][(vframe >> 3) % ar_idles[k]] : ar_num[k];   // the game's shine
  int slot = cache_get(smap[num]);
  if (slot < 0) return;
  int size = (16 * zoom) >> 8;
  sprite_t *s = &mc_spr[(*out)++];
  s->x = (int16_t)((x >> 8) - size / 2); s->y = (int16_t)((y >> 8) - size / 2);
  s->tile = (uint16_t)(slot * 4); s->pal = pal; s->w = 2; s->h = 2;
  s->attr = (uint8_t)(SA_ON | SA_PRIO(SPR_PRIO)); s->zoomx = zoom; s->zoomy = zoom;
}

static void arena_frame(int *outp) {
  uint32_t in[4] = {IN_P1, IN_P2, IN_P3, IN_P4};
  if (!ar_cleared) {                               // the panels of the 4-player screen go; the decor
    for (int i = 0; i < 64 * 64; i++) TILEMAP(0)[i] = MC_TILE(FIX_EMPTY, 0, 0);
    for (int l = 2; l <= 3; l++) mc_layer(l, spr_file, MC_TILE16 | MC_4BPP, 32, 32, 0);   // the game's decor
    ar_compose();                                  // layer 1: the still launchers, the black sides
    ar_sound(AR_SND_READY);
    ar_cleared = 1;
  }
  { const uint16_t *p = neo_pal[neo.pal_bank];     // the colours of the dragons, the yellow
    dragons_frame();
    ar_img_palettes();
    for (int k = 0; k < 4; k++)                    // the grey of each colour: its lightness, no hue
      for (int i = 0; i < 16; i++) {
        uint16_t c = PALETTE[ar_pal[k] * 16 + i];
        int r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31, l = (r * 5 + g * 9 + b * 2) >> 4;
        PALETTE[(AR_GREY_PAL + k) * 16 + i] = i ? (uint16_t)((l << 10) | (l << 5) | l) : 0;
      }
    for (int i = 0; i < 16; i++) PALETTE[215 * 16 + i] = i ? ar_hue(rgb555(p[ar_src4 * 16 + i]), 250) : 0; }
  if (ar_end) {                                    // the results, then the console starts again
    if (--ar_end == 1) arena_restart = 1;
  } else if (ar_start) {                          // READY ... GO!
    ar_start--;
    hud_text(19, 16, ar_start > 30 ? "READY" : " GO! ", HUD_PAL + 3);
    if (ar_start == 30) ar_sound(AR_SND_SHOT);
    if (!ar_start) for (int i = 0; i < 5; i++) TILEMAP(0)[16 * 64 + 19 + i] = MC_TILE(FIX_EMPTY, 0, 0);   // (spaces are opaque)
    for (int k = 0; k < arena_players; k++) {      // they may aim already
      if (in[k] & IN_LEFT) { ar_ang[k] -= 2; if (ar_ang[k] >= -75) ar_wheel[k]--; }
      if (in[k] & IN_RIGHT) { ar_ang[k] += 2; if (ar_ang[k] <= 75) ar_wheel[k]++; }
      if (ar_ang[k] < -75) ar_ang[k] = -75;
      if (ar_ang[k] > 75) ar_ang[k] = 75;
      ar_prev[k] = in[k];
    }
  } else {
    if (ar_clock) ar_clock--;
    if (ar_clock && ar_clock <= 10 * 60 && ar_clock % 60 == 0) ar_sound(AR_SND_ALERT);   // the last 10 s
    if (!ar_clock) {
      ar_end = 10 * 60;
      int order[4] = {0, 1, 2, 3};                   // the ranking, best first
      for (int i = 0; i < arena_players; i++)
        for (int j = i + 1; j < arena_players; j++)
          if (ar_score[order[j]] > ar_score[order[i]]) { int t = order[i]; order[i] = order[j]; order[j] = t; }
      for (int r = 0; r < 64 * 64; r++) TILEMAP(0)[r] = MC_TILE(FIX_EMPTY, 0, 0);
      char t[] = "PLAYER 1 WINS!"; t[7] = (char)('1' + order[0]);
      hud_text(13, 9, t, HUD_PAL + order[0]);
      for (int i = 0; i < arena_players; i++) {
        int k = order[i]; char line[] = "1ST  PLAYER 1  00000";
        static const char *const place[4] = {"1ST", "2ND", "3RD", "4TH"};
        for (int c = 0; c < 3; c++) line[c] = place[i][c];
        line[12] = (char)('1' + k);
        uint32_t v = ar_score[k] > 99999 ? 99999 : ar_score[k];
        for (int c = 19; c >= 15; c--) { line[c] = (char)('0' + v % 10); v /= 10; }
        hud_text(10, 13 + 2 * i, line, HUD_PAL + k);
      }
      ar_sound(AR_SND_WIN);
      for (int i = 0; i < arena_players; i++) ar_dragon_play(order[i], i ? AR_WORRY : AR_HAPPY, 1);
    }
    if (ar_clock && ar_clock % AR_DROP == 0) ar_new_row();
    for (int k = 0; k < arena_players; k++) {      // aim and shoot
      if (in[k] & IN_LEFT) { ar_ang[k] -= 2; if (ar_ang[k] >= -75) ar_wheel[k]--; }
      if (in[k] & IN_RIGHT) { ar_ang[k] += 2; if (ar_ang[k] <= 75) ar_wheel[k]++; }
      if (ar_ang[k] < -75) ar_ang[k] = -75;
      if (ar_ang[k] > 75) ar_ang[k] = 75;
      uint32_t e = in[k] & ~ar_prev[k];
      if (ar_jump[k]) ar_jump[k]--;
      if ((e & (IN_B(1) | IN_UP)) && !ar_fly[k].on && ar_clock && !ar_start) {
        ar_jump[k] = 16; ar_dragon_play(k, AR_SHOOT, 0);
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
    for (int i = 0; i < 64; i++) if (ar_pop[i].t) ar_pop[i].t--;   // the bursts go on
    for (int i = 0; i < 64; i++)                   // the bubbles falling
      if (ar_fall[i].on) { ar_fall[i].vy += FX(1) / 4; ar_fall[i].y += ar_fall[i].vy; if (ar_fall[i].y > FX(300)) ar_fall[i].on = 0; }
  }
  // drawing (the game's decor is added behind, by video_frame)
  int out = *outp;
  for (int k = 0; k < 4; k++) if (ar_fly[k].on) ar_sprite(&out, ar_fly[k].x, ar_fly[k].y, k, 256);
  for (int i = 0; i < 64; i++) if (ar_fall[i].on) ar_sprite(&out, ar_fall[i].x, ar_fall[i].y, ar_fall[i].col, 256);
  for (int i = 0; i < 64; i++) {                   // a burst: the game's frames (3 x 3 tiles, 3 frames each)
    int t = ar_pop[i].t, k = ar_pop[i].col;
    if (!t) continue;
    if (ar_bursts[k]) {
      int f = (AR_POP_T - t) / 3;
      if (f >= (int)ar_bursts[k]) continue;
      const uint16_t *smap = (const uint16_t *)spr_map_file;
      for (int j = 0; j < 9 && out < MC_SPRITES; j++) {   // column by column, as the Neo Geo blocks
        uint32_t num = ar_burst[k][f][j];
        if (!num || !smap[num]) continue;
        int slot = cache_get(smap[num]);
        if (slot < 0) continue;
        sprite_t *sp = &mc_spr[out++];
        sp->x = (int16_t)((ar_pop[i].x >> 8) - 24 + (j / 3) * 16); sp->y = (int16_t)((ar_pop[i].y >> 8) - 24 + (j % 3) * 16);
        sp->tile = (uint16_t)(slot * 4); sp->pal = ar_pal[k]; sp->w = 2; sp->h = 2;
        sp->attr = (uint8_t)(SA_ON | SA_PRIO(SPR_PRIO) | ((ar_burst_flip[k][f][j] & 1) ? SA_HFLIP : 0) | ((ar_burst_flip[k][f][j] & 2) ? SA_VFLIP : 0));
        sp->zoomx = 256; sp->zoomy = 256;
      }
      continue;
    }
    if (t > 8) ar_sprite_pal(&out, ar_pop[i].x, ar_pop[i].y, k, 256, ((t >> 2) & 1) ? (uint8_t)(AR_GREY_PAL + k) : ar_pal[k]);
    else ar_sprite_pal(&out, ar_pop[i].x, ar_pop[i].y, k, (uint16_t)(256 + (8 - t) * 12), (uint8_t)(AR_GREY_PAL + k));
  }
  for (int k = 0; k < arena_players; k++) {        // the launchers: the next bubble and the aim
    int32_t x = FX(AR_X + 40 + 80 * k), y = FX(AR_LY - 10);
    if (!ar_fly[k].on) ar_sprite(&out, x, y, k, 256);
    for (int d = 1; d <= 5; d++)
      ar_sprite(&out, x + ar_sinv(ar_ang[k]) * 7 * d, y - ar_cosv(ar_ang[k]) * 7 * d, k, 80);
  }
  for (int r = 0; r < AR_ROWS && !ar_end; r++)    // (the results: the landscape alone behind them)
    for (int c = 0; c < ar_cols(r); c++)
      if (ar_grid[r][c] >= 0) ar_sprite_h(&out, ar_cx(r, c), ar_cy(r), ar_grid[r][c]);
  for (int k = 0; k < arena_players; k++) { ar_dragon_step(k); ar_draw_dragon(&out, k); }
  for (int k = 0; k < arena_players; k++) ar_draw_launcher(&out, k);  // in front of the machine
                                                   // (the still launchers: layer 1)
  *outp = out;
  if (!ar_end) ar_points_frame();
  ar_text_score();
}
