// MONOCORE-1 "mjeu": the common parts of a small game (see mjeu.h and MJEU.md).
#include <string.h>
#include "mc.h"
#include "mgfx.h"
#include "mjeu.h"

static int fdiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }   // rounds down

int mj_world(mj_world_t *w, mg_bank_t *b, int map, int layer) {
  memset(w, 0, sizeof *w);
  w->bank = b; w->map = map; w->layer = layer;
  w->tile = mg_map_tile(b, map);
  w->w = mg_map_w(b, map) * w->tile; w->h = mg_map_h(b, map) * w->tile;
  w->screen_w = mc_screen_w(); w->screen_h = mc_screen_h();
  w->gravity = 4; w->max_fall = 96; w->walls = true;
  if (mg_map_layer(b, map, layer, 1)) return -1;
  mj_camera_at(w, 0, 0);
  return 0;
}
int mj_attr(const mj_world_t *w, int px, int py) {
  if (px < 0 || px >= w->w) return w->walls ? MG_SOLID : 0;
  if (py < 0 || py >= w->h) return 0;                      // above and below: open (one falls out)
  return mg_attr_at(w->bank, w->layer, px, py);
}

// ---------------------------------------------------------------- objects
void mj_obj(mj_obj_t *o, const mj_world_t *w, int sprite, int px, int py) {
  memset(o, 0, sizeof *o);
  o->sprite = (uint8_t)sprite; o->flags = MJ_ACTIVE | MJ_ANIM;
  o->x = px * MJ_FIX; o->y = py * MJ_FIX;
  o->bw = (uint8_t)mg_sprite_w(w->bank, sprite); o->bh = (uint8_t)mg_sprite_h(w->bank, sprite);
}
int mj_from_map(const mj_world_t *w, mj_obj_t *objs, int max) {
  int n = 0;
  for (int i = 0; i < mg_object_count(w->bank, w->map) && n < max; i++) {
    const mg_object_t *mo = mg_object(w->bank, w->map, i);
    mj_obj(&objs[n], w, mo->sprite, mo->x, mo->y);
    objs[n].param = mo->param;
    if (mo->flags & MG_OF_HFLIP) objs[n].flags |= MJ_HFLIP;
    n++;
  }
  return n;
}
int mj_px(const mj_obj_t *o) { return fdiv(o->x, MJ_FIX); }
int mj_py(const mj_obj_t *o) { return fdiv(o->y, MJ_FIX); }
void mj_set_pos(mj_obj_t *o, int px, int py) { o->x = px * MJ_FIX; o->y = py * MJ_FIX; }
void mj_box(mj_obj_t *o, int bx, int by, int bw, int bh) { o->bx = (int8_t)bx; o->by = (int8_t)by; o->bw = (uint8_t)bw; o->bh = (uint8_t)bh; }

// is there a tile with one of the attributes along a segment (a side of the box)?
static bool along_y(const mj_world_t *w, int x, int y0, int y1, int attr) {
  int step = w->tile < 8 ? w->tile : 8;
  for (int y = y0;; y += step) {
    if (y > y1) y = y1;
    if (mj_attr(w, x, y) & attr) return true;
    if (y == y1) return false;
  }
}
static int along_x(const mj_world_t *w, int x0, int x1, int y) {   // all the attributes met
  int step = w->tile < 8 ? w->tile : 8, a = 0;
  for (int x = x0;; x += step) {
    if (x > x1) x = x1;
    a |= mj_attr(w, x, y);
    if (x == x1) return a;
  }
}
void mj_move(const mj_world_t *w, mj_obj_t *o) {
  if (!(o->flags & MJ_ACTIVE)) return;
  o->touch = 0;
  if (o->flags & MJ_GRAVITY) { o->vy += w->gravity; if (o->vy > w->max_fall) o->vy = (int16_t)w->max_fall; }
  if (o->flags & MJ_GHOST) { o->x += o->vx; o->y += o->vy; return; }
  int t = w->tile;
  // across
  if (o->vx) {
    int32_t nx = o->x + o->vx;
    int left = fdiv(nx, MJ_FIX) + o->bx, right = left + o->bw - 1, top = mj_py(o) + o->by, bottom = top + o->bh - 1;
    if (o->vx > 0 && along_y(w, right, top, bottom, MG_SOLID)) {
      nx = (int32_t)(fdiv(right, t) * t - o->bw - o->bx) * MJ_FIX; o->touch |= MJ_RIGHT; o->vx = 0;
    } else if (o->vx < 0 && along_y(w, left, top, bottom, MG_SOLID)) {
      nx = (int32_t)((fdiv(left, t) + 1) * t - o->bx) * MJ_FIX; o->touch |= MJ_LEFT; o->vx = 0;
    }
    o->x = nx;
  }
  // up and down
  int left = mj_px(o) + o->bx, right = left + o->bw - 1, t2 = t;
  int old_bottom = mj_py(o) + o->by + o->bh - 1;
  int32_t ny = o->y + o->vy;
  int top = fdiv(ny, MJ_FIX) + o->by, bottom = top + o->bh - 1;
  if (o->vy > 0) {                          // (with gravity, vy > 0 every frame)
    // the row just under the feet: standing, or landing (a platform only if it was not passed yet)
    int probe = bottom + 1, a = along_x(w, left, right, probe), ttop = fdiv(probe, t2) * t2;
    bool land = (a & MG_SOLID) || ((a & MG_PLATFORM) && !(o->flags & MJ_DOWN) && old_bottom < ttop);
    if (land) { ny = (int32_t)(ttop - o->bh - o->by) * MJ_FIX; o->touch |= MJ_GROUND; o->vy = 0; }
  } else if (along_x(w, left, right, top) & MG_SOLID) {
    ny = (int32_t)((fdiv(top, t2) + 1) * t2 - o->by) * MJ_FIX; o->touch |= MJ_CEILING; o->vy = 0;
  }
  o->y = ny;
  o->flags &= (uint8_t)~MJ_DOWN;
}
bool mj_touches(const mj_world_t *w, const mj_obj_t *o, int attr) {
  int left = mj_px(o) + o->bx, top = mj_py(o) + o->by;
  int step = w->tile < 8 ? w->tile : 8;
  for (int y = top;; y += step) {
    if (y > top + o->bh - 1) y = top + o->bh - 1;
    if (along_x(w, left, left + o->bw - 1, y) & attr) return true;
    if (y == top + o->bh - 1) return false;
  }
}
bool mj_overlap(const mj_obj_t *a, const mj_obj_t *b) {
  if (!(a->flags & MJ_ACTIVE) || !(b->flags & MJ_ACTIVE)) return false;
  int ax = mj_px(a) + a->bx, ay = mj_py(a) + a->by, bx = mj_px(b) + b->bx, by = mj_py(b) + b->by;
  return ax < bx + b->bw && bx < ax + a->bw && ay < by + b->bh && by < ay + a->bh;
}
void mj_patrol(const mj_world_t *w, mj_obj_t *o, int v, bool ledges) {
  bool left = (o->flags & MJ_HFLIP) != 0;
  o->vx = (int16_t)(left ? -v : v);
  mj_move(w, o);
  bool turn = (o->touch & (MJ_LEFT | MJ_RIGHT)) != 0;
  if (!turn && ledges && (o->touch & MJ_GROUND)) {          // the ground ends just ahead?
    int fx = mj_px(o) + o->bx + (left ? -1 : o->bw), fy = mj_py(o) + o->by + o->bh;
    if (!(mj_attr(w, fx, fy) & (MG_SOLID | MG_PLATFORM))) turn = true;
  }
  if (turn) o->flags ^= MJ_HFLIP;
}

// ---------------------------------------------------------------- camera, drawing
void mj_camera_at(mj_world_t *w, int x, int y) {
  if (x > w->w - w->screen_w) x = w->w - w->screen_w;
  if (y > w->h - w->screen_h) y = w->h - w->screen_h;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  w->cam_x = x; w->cam_y = y;
  mc_layer_scroll(w->layer, x, y);
}
void mj_camera(mj_world_t *w, const mj_obj_t *o) {
  int cx = mj_px(o) + o->bx + o->bw / 2 - w->cam_x, cy = mj_py(o) + o->by + o->bh / 2 - w->cam_y;
  int x = w->cam_x, y = w->cam_y, dzx = w->screen_w / 10, dzy = w->screen_h / 8;
  if (cx < w->screen_w / 2 - dzx) x -= w->screen_w / 2 - dzx - cx;
  if (cx > w->screen_w / 2 + dzx) x += cx - (w->screen_w / 2 + dzx);
  if (cy < w->screen_h / 2 - dzy) y -= w->screen_h / 2 - dzy - cy;
  if (cy > w->screen_h / 2 + dzy) y += cy - (w->screen_h / 2 + dzy);
  mj_camera_at(w, x, y);
}
void mj_draw(const mj_world_t *w, const mj_obj_t *o, int hw, int prio) {
  int x = mj_px(o) - w->cam_x, y = mj_py(o) - w->cam_y;
  if (!(o->flags & MJ_ACTIVE) || (o->flags & MJ_HIDDEN) || x < -72 || y < -72 || x > w->screen_w + 8 || y > w->screen_h + 8) {
    mc_sprite_hide(hw); return;
  }
  int f = (o->flags & MJ_ANIM) ? mg_frame_at(w->bank, o->sprite, mc_frames) : o->frame;
  mg_sprite(w->bank, hw, o->sprite, f, x, y, prio);
  mc_sprite_flip(hw, (o->flags & MJ_HFLIP) != 0, false);
}
void mj_draw_all(const mj_world_t *w, const mj_obj_t *objs, int n, int first_hw, int prio) {
  for (int i = 0; i < n; i++) mj_draw(w, &objs[i], first_hw + i, prio);
}

// ---------------------------------------------------------------- sound effects
static struct { int f0, f1, n, t, vol, wave; } fx[8];
void mj_sfx(int ch, int wave, int f0, int f1, int frames, int vol) {
  ch &= 7;
  fx[ch].f0 = f0; fx[ch].f1 = f1; fx[ch].n = frames > 0 ? frames : 1; fx[ch].t = 0; fx[ch].vol = vol; fx[ch].wave = wave;
  mc_psg(ch, wave, (uint32_t)f0, 128, vol, vol);
}
void mj_frame(void) {
  mc_frame();
  for (int ch = 0; ch < 8; ch++) {
    if (!fx[ch].n) continue;
    if (++fx[ch].t >= fx[ch].n) { mc_psg_volume(ch, 0, 0); fx[ch].n = 0; continue; }
    int f = fx[ch].f0 + (fx[ch].f1 - fx[ch].f0) * fx[ch].t / fx[ch].n, v = fx[ch].vol * (fx[ch].n - fx[ch].t) / fx[ch].n;
    mc_psg_freq16(ch, (uint32_t)f * 16);
    mc_psg_volume(ch, v, v);
  }
}

// ---------------------------------------------------------------- a save in the NVRAM
// "MJSV", u32 size, u32 checksum, then the data (from the start of the NVRAM)
static uint32_t sum(const uint8_t *p, uint32_t n) { uint32_t s = 0x4D4A; for (uint32_t i = 0; i < n; i++) s = (s << 5) + s + p[i]; return s; }
static uint32_t nvw(int a) { volatile uint8_t *n = MC_NVRAM; return n[a] | (n[a + 1] << 8) | (n[a + 2] << 16) | ((uint32_t)n[a + 3] << 24); }
bool mj_load(void *data, uint32_t n) {
  volatile uint8_t *nv = MC_NVRAM;
  if (n > 4000 || nv[0] != 'M' || nv[1] != 'J' || nv[2] != 'S' || nv[3] != 'V' || nvw(4) != n) return false;
  static uint8_t b[4000];
  for (uint32_t i = 0; i < n; i++) b[i] = nv[12 + i];
  if (sum(b, n) != nvw(8)) return false;
  memcpy(data, b, n);
  return true;
}
void mj_save(const void *data, uint32_t n) {
  volatile uint8_t *nv = MC_NVRAM;
  if (n > 4000) return;
  uint32_t s = sum(data, n);
  for (uint32_t i = 0; i < n; i++) nv[12 + i] = ((const uint8_t *)data)[i];
  nv[0] = 'M'; nv[1] = 'J'; nv[2] = 'S'; nv[3] = 'V';
  for (int i = 0; i < 4; i++) { nv[4 + i] = (uint8_t)(n >> (8 * i)); nv[8 + i] = (uint8_t)(s >> (8 * i)); }
}
