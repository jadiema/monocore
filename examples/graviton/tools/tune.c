// GRAVITON: moves the fixed pods of an aim-only level until some angles hit (walls kept)
#include <stdio.h>
#include <stdlib.h>
#include "../physics.h"
#include "../levels.h"
static uint32_t rs = 777;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static long eval(gv_level *lv, int step, int *ok) {
  gv_world w; gv_world_init(&w, lv);
  long best = 1L << 40; *ok = 0;
  for (int a = lv->amin; a <= lv->amax; a += step) {
    gv_body b; gv_launch(&b, lv, a); long md = 1L << 40; int ev;
    for (;;) { ev = gv_frame(&w, &b);
      long dx = (b.x >> 12) - lv->tx * 16, dy = (b.y >> 12) - lv->ty * 16, d = dx * dx + dy * dy; if (d < md) md = d;
      if (ev != EV_NONE && ev != EV_BOUNCE) break; }
    if (ev == EV_TARGET) { (*ok)++; md = 0; }
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
  for (int it = 0; it < 600 && !(ok >= 3 && ok <= 10); it++) {
    gv_pod save[GV_MAX_PODS]; for (int i = 0; i < lv.n_fixed; i++) save[i] = pods[i];
    int i = rnd() % lv.n_fixed, st = 4 + rnd() % 30;
    pods[i].x = (int16_t)(pods[i].x + (int)(rnd() % (2 * st + 1)) - st);
    pods[i].y = (int16_t)(pods[i].y + (int)(rnd() % (2 * st + 1)) - st);
    if (rnd() % 3 == 0) pods[i].power = (uint8_t)(60 + rnd() % 180);
    if (pods[i].x < 20 || pods[i].x > 300 || pods[i].y < GV_TOP + 10 || pods[i].y > 230) { for (int k = 0; k < lv.n_fixed; k++) pods[k] = save[k]; continue; }
    int ok2; long s2 = eval(&lv, 4, &ok2);
    double sc2 = ok2 ? -(ok2 > 10 ? 20 - ok2 : ok2) : s2;
    if (sc2 <= score) { score = sc2; ok = ok2; } else for (int k = 0; k < lv.n_fixed; k++) pods[k] = save[k];
  }
  int exact; eval(&lv, 1, &exact);
  printf("%2d %-16s %d angles (exact):", li + 1, lv.name, exact);
  for (int i = 0; i < lv.n_fixed; i++) printf(" { %d, %d, %d, %d },", pods[i].x, pods[i].y, pods[i].sign, pods[i].power);
  printf("\n");
}
