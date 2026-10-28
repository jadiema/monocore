// GRAVITON: moves the fixed pods of an aim-only level until some angles hit (walls kept)
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../physics.h"
#include "../levels.h"
static double seg_dist(double px, double py, const gv_wall *w) {
  double dx = w->x2 - w->x1, dy = w->y2 - w->y1, l2 = dx * dx + dy * dy, t = l2 ? ((px - w->x1) * dx + (py - w->y1) * dy) / l2 : 0;
  if (t < 0) t = 0; if (t > 1) t = 1;
  return hypot(px - w->x1 - t * dx, py - w->y1 - t * dy);
}
static uint32_t rs = 777;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static long eval(gv_level *lv, int step, int *ok) {
  gv_world w; gv_world_init(&w, lv);
  long best = 1L << 40; *ok = 0;
  for (int a = lv->amin; a <= lv->amax; a += step) {
    gv_body b; gv_launch(&b, lv, a); long md = 1L << 40; int ev; double pc = 1e9;
    for (;;) { ev = gv_frame(&w, &b);
      for (int i = 0; i < w.n_pods; i++) { double d = hypot(b.x / 65536.0 - w.pods[i].x, b.y / 65536.0 - w.pods[i].y); if (d < pc) pc = d; }
      long dx = (b.x >> 12) - lv->tx * 16, dy = (b.y >> 12) - lv->ty * 16, d = dx * dx + dy * dy; if (d < md) md = d;
      if (ev != EV_NONE && ev != EV_BOUNCE) break; }
    if (ev == EV_TARGET && pc >= 12) { (*ok)++; md = 0; }
    else if (ev == EV_TARGET) md = 1;
    if (md < best) best = md;
  }
  return best;
}
int main(int argc, char **argv) {
  int li = atoi(argv[1]); if (argc > 2) rs = (uint32_t)atoi(argv[2]);
  gv_level lv = gv_levels[li];
  gv_pod pods[GV_MAX_PODS]; for (int i = 0; i < lv.n_fixed; i++) pods[i] = lv.fixed[i];
  lv.fixed = pods;
  int ok; long sc = eval(&lv, 4, &ok);
  double score = ok ? -ok : sc;
  for (int it = 0; it < 600 && !(ok >= 12 && ok <= 30); it++) {
    gv_pod save[GV_MAX_PODS]; for (int i = 0; i < lv.n_fixed; i++) save[i] = pods[i];
    int i = rnd() % lv.n_fixed, st = 4 + rnd() % 30;
    pods[i].x = (int16_t)(pods[i].x + (int)(rnd() % (2 * st + 1)) - st);
    pods[i].y = (int16_t)(pods[i].y + (int)(rnd() % (2 * st + 1)) - st);
    if (rnd() % 3 == 0) pods[i].power = (uint8_t)(60 + rnd() % 180);
    int near = 0;
    for (int k = 0; k < lv.n_walls; k++) if (seg_dist(pods[i].x, pods[i].y, &lv.walls[k]) < 30) near = 1;
    if (near || pods[i].x < 20 || pods[i].x > 300 || pods[i].y < GV_TOP + 10 || pods[i].y > 230) { for (int k = 0; k < lv.n_fixed; k++) pods[k] = save[k]; continue; }
    int ok2; long s2 = eval(&lv, 4, &ok2);
    double sc2 = ok2 ? -(ok2 > 30 ? 60 - ok2 : ok2) : s2;
    if (sc2 <= score) { score = sc2; ok = ok2; } else for (int k = 0; k < lv.n_fixed; k++) pods[k] = save[k];
  }
  int exact; eval(&lv, 1, &exact);
  printf("%2d %-16s %d angles (exact):", li + 1, lv.name, exact);
  for (int i = 0; i < lv.n_fixed; i++) printf(" { %d, %d, %d, %d },", pods[i].x, pods[i].y, pods[i].sign, pods[i].power);
  printf("\n");
}
