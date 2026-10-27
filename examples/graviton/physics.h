// GRAVITON: deterministic physics, integer only.
// Shared by the game (RV32IM) and the PC level checker (tools/solve.c): the same source gives
// the same trajectories, bit for bit, on both.
#pragma once
#include <stdint.h>
#include "sintab.h"

#define GV_SUB        4            // sub-steps per frame
#define GV_LIFE       1500         // frames before the shot is lost in space
#define GV_POD_POWER  64           // pull of a player pod (px^3 / frame^2)
#define GV_RMIN2      (6 * 6 * 256) // softening: r >= 6 px (units 1/256 px^2)
#define GV_HIT_TARGET 8            // px
#define GV_HIT_POD    4            // px
#define GV_WALL_R     4            // half wall thickness + shot radius, px
#define GV_W          320
#define GV_H          240
#define GV_TOP        12           // first line of the playfield (HUD above)

enum { W_SOLID = 0, W_BOUNCE = 1 };
enum { EV_NONE = 0, EV_TARGET, EV_WALL, EV_POD, EV_OUT, EV_BOUNCE, EV_LOST };

typedef struct { int16_t x1, y1, x2, y2; uint8_t kind; } gv_wall;
typedef struct { int16_t x, y; int8_t sign; uint8_t power; } gv_pod;   // sign +1 pulls, -1 pushes
typedef struct { int16_t x0, y0, x1, y1; } gv_rect;

typedef struct {
  const char *name;
  int16_t lx, ly;                  // launcher
  int16_t angle, amin, amax;       // 1024 steps a turn, 0 = right, 256 = down; amin == amax: fixed
  uint8_t speed;                   // launch speed, 1/16 px per frame
  int16_t tx, ty;                  // target
  uint8_t n_pull, n_push;          // pods the player may place
  const gv_pod *fixed; uint8_t n_fixed;
  const gv_wall *walls; uint8_t n_walls;
  const gv_rect *nopod; uint8_t n_nopod;
} gv_level;

#define GV_MAX_PODS 16
typedef struct {
  const gv_level *lv;
  gv_pod pods[GV_MAX_PODS];        // fixed pods first, then the player's
  int n_pods;
} gv_world;

typedef struct { int32_t x, y, vx, vy; int frame; } gv_body;   // 16.16, px and px / frame

static inline int32_t gv_sin(int a) {
  a &= 1023;
  if (a < 256) return gv_sin_q[a];
  if (a < 512) return gv_sin_q[512 - a];
  if (a < 768) return -gv_sin_q[a - 512];
  return -gv_sin_q[1024 - a];
}
static inline int32_t gv_cos(int a) { return gv_sin(a + 256); }

static inline uint32_t gv_isqrt(uint32_t v) {
  uint32_t r = 0, b = 1u << 30;
  while (b > v) b >>= 2;
  while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
  return r;
}

static inline void gv_launch(gv_body *b, const gv_level *lv, int angle) {
  b->x = lv->lx << 16; b->y = lv->ly << 16;
  int32_t s = lv->speed << 12;                         // 1/16 px -> 16.16
  b->vx = (int32_t)(((int64_t)s * gv_cos(angle)) >> 16);
  b->vy = (int32_t)(((int64_t)s * gv_sin(angle)) >> 16);
  // start just outside the launcher
  b->x += b->vx * 4; b->y += b->vy * 4;
  b->frame = 0;
}

// Wall test at 1/16 px. Returns 1 and the push-out normal (nx, ny, 1/16 px, length n) if touching.
static inline int gv_wall_hit(const gv_wall *w, int32_t px, int32_t py, int32_t *nx, int32_t *ny) {
  const int32_t R = GV_WALL_R * 16;
  int32_t ax = w->x1 * 16, ay = w->y1 * 16, bx = w->x2 * 16, by = w->y2 * 16;
  int32_t lo, hi;
  lo = ax < bx ? ax : bx; hi = ax < bx ? bx : ax;
  if (px < lo - R || px > hi + R) return 0;
  lo = ay < by ? ay : by; hi = ay < by ? by : ay;
  if (py < lo - R || py > hi + R) return 0;
  int32_t dx = bx - ax, dy = by - ay;
  int32_t den = dx * dx + dy * dy;                     // < 2^27 on a 320 x 240 screen
  int32_t num = (px - ax) * dx + (py - ay) * dy;
  int32_t cx, cy;
  if (den == 0 || num <= 0) { cx = ax; cy = ay; }
  else if (num >= den) { cx = bx; cy = by; }
  else {
    while (den >= 65536) { den >>= 1; num >>= 1; }     // keeps the 32-bit division exact enough
    int32_t tt = (num << 15) / (den ? den : 1);        // 0..32768
    cx = ax + ((dx * (tt >> 3)) >> 12);
    cy = ay + ((dy * (tt >> 3)) >> 12);
  }
  int32_t ex = px - cx, ey = py - cy;
  if (ex * ex + ey * ey >= R * R) return 0;
  if (ex == 0 && ey == 0) { ex = -dy; ey = dx; if (ex == 0 && ey == 0) ex = 1; }
  *nx = ex; *ny = ey;
  return 1;
}

// One sub-step. Returns an EV_ event.
static inline int gv_step(const gv_world *w, gv_body *b) {
  int32_t ax = 0, ay = 0;
  for (int i = 0; i < w->n_pods; i++) {
    const gv_pod *p = &w->pods[i];
    int32_t dx4 = ((p->x << 16) - b->x) >> 12, dy4 = ((p->y << 16) - b->y) >> 12;   // 1/16 px
    uint32_t r2 = (uint32_t)(dx4 * dx4 + dy4 * dy4);                               // 1/256 px^2
    if (r2 < (uint32_t)(GV_HIT_POD * GV_HIT_POD * 256)) return EV_POD;
    if (r2 < GV_RMIN2) r2 = GV_RMIN2;
    int32_t r = (int32_t)gv_isqrt(r2);                                             // 1/16 px
    int32_t f = (int32_t)(((uint32_t)p->power << 23) / r2);                       // power * 32768 / r^2
    int32_t ux = (dx4 << 14) / r, uy = (dy4 << 14) / r;                           // unit vector, 1/16384
    int32_t gx = (int32_t)(((int64_t)f * ux) >> 13), gy = (int32_t)(((int64_t)f * uy) >> 13);
    if (p->sign < 0) { gx = -gx; gy = -gy; }
    ax += gx; ay += gy;
  }
  b->vx += ax / GV_SUB; b->vy += ay / GV_SUB;
  b->x += b->vx / GV_SUB; b->y += b->vy / GV_SUB;

  int32_t px = b->x >> 12, py = b->y >> 12;
  const gv_level *lv = w->lv;
  int32_t tx = px - lv->tx * 16, ty = py - lv->ty * 16;
  if (tx * tx + ty * ty < GV_HIT_TARGET * GV_HIT_TARGET * 256) return EV_TARGET;

  int ev = EV_NONE;
  for (int i = 0; i < lv->n_walls; i++) {
    int32_t nx, ny;
    const gv_wall *wl = &lv->walls[i];
    if (!gv_wall_hit(wl, px, py, &nx, &ny)) continue;
    if (wl->kind == W_SOLID) return EV_WALL;
    // bounce: reflect the speed about the normal, damped a little, and push out
    int32_t n2 = nx * nx + ny * ny;
    int32_t n = (int32_t)gv_isqrt((uint32_t)n2); if (n == 0) n = 1;
    int32_t ux = (nx << 14) / n, uy = (ny << 14) / n;
    int32_t vn = (int32_t)(((int64_t)b->vx * ux + (int64_t)b->vy * uy) >> 14);
    if (vn < 0) {
      b->vx -= (int32_t)(((int64_t)2 * vn * ux) >> 14);
      b->vy -= (int32_t)(((int64_t)2 * vn * uy) >> 14);
      b->vx = b->vx - (b->vx >> 4); b->vy = b->vy - (b->vy >> 4);
      ev = EV_BOUNCE;
    }
    int32_t cx = px - nx, cy = py - ny;                          // closest point, 1/16 px
    int32_t R = GV_WALL_R * 16 + 1;
    b->x = (cx + ((R * ux) >> 14)) << 12;
    b->y = (cy + ((R * uy) >> 14)) << 12;
    px = b->x >> 12; py = b->y >> 12;
  }
  if (px < 0 || px >= GV_W * 16 || py < GV_TOP * 16 || py >= GV_H * 16) return EV_OUT;
  return ev;
}

// One frame (GV_SUB sub-steps). Stops at the first final event.
static inline int gv_frame(const gv_world *w, gv_body *b) {
  int last = EV_NONE;
  for (int s = 0; s < GV_SUB; s++) {
    int ev = gv_step(w, b);
    if (ev == EV_BOUNCE) last = EV_BOUNCE;
    else if (ev != EV_NONE) return ev;
  }
  if (++b->frame >= GV_LIFE) return EV_LOST;
  return last;
}

static inline void gv_world_init(gv_world *w, const gv_level *lv) {
  w->lv = lv; w->n_pods = 0;
  for (int i = 0; i < lv->n_fixed && w->n_pods < GV_MAX_PODS; i++) w->pods[w->n_pods++] = lv->fixed[i];
}

// Where the player may put a pod: inside the playfield, outside the forbidden zones, away from the
// launcher, the target, the walls and the other pods. skip = index of a pod to ignore (-1: none).
static inline int gv_can_place(const gv_world *w, int x, int y, int skip) {
  const gv_level *lv = w->lv;
  if (x < 8 || x > GV_W - 9 || y < GV_TOP + 8 || y > GV_H - 9) return 0;
  for (int i = 0; i < lv->n_nopod; i++) {
    const gv_rect *r = &lv->nopod[i];
    if (x >= r->x0 && x <= r->x1 && y >= r->y0 && y <= r->y1) return 0;
  }
  int dx = x - lv->lx, dy = y - lv->ly;
  if (dx * dx + dy * dy < 16 * 16) return 0;
  dx = x - lv->tx; dy = y - lv->ty;
  if (dx * dx + dy * dy < 16 * 16) return 0;
  for (int i = 0; i < w->n_pods; i++) {
    if (i == skip) continue;
    dx = x - w->pods[i].x; dy = y - w->pods[i].y;
    if (dx * dx + dy * dy < 14 * 14) return 0;
  }
  for (int i = 0; i < lv->n_walls; i++) {
    int32_t nx, ny;
    gv_wall wl = lv->walls[i];
    // a pod needs 8 px of room: test with the shot test, twice as wide
    if (gv_wall_hit(&wl, x * 16, y * 16, &nx, &ny)) return 0;
    if (gv_wall_hit(&wl, x * 16 + 64, y * 16, &nx, &ny) || gv_wall_hit(&wl, x * 16 - 64, y * 16, &nx, &ny) ||
        gv_wall_hit(&wl, x * 16, y * 16 + 64, &nx, &ny) || gv_wall_hit(&wl, x * 16, y * 16 - 64, &nx, &ny)) return 0;
  }
  return 1;
}

// Plays a whole shot; returns the final event (EV_TARGET, EV_WALL...).
static inline int gv_simulate(const gv_world *w, int angle, int *frames) {
  gv_body b; gv_launch(&b, w->lv, angle);
  for (;;) {
    int ev = gv_frame(w, &b);
    if (ev != EV_NONE && ev != EV_BOUNCE) { if (frames) *frames = b.frame; return ev; }
  }
}
