// GRAVITON: aim-only levels, every angle; writes the best trajectory for draw.py
#include <stdio.h>
#include <stdlib.h>
#include "../physics.h"
#include "../levels.h"
int main(int argc, char **argv) {
  for (int li = 0; li < GV_LEVELS; li++) {
    const gv_level *lv = &gv_levels[li];
    if (argc > 1 && atoi(argv[1]) != li) continue;
    gv_world w; gv_world_init(&w, lv);
    int ok = 0, best_a = 0; long best = 1L << 40; int first = 9999, last = -9999;
    for (int a = lv->amin; a <= lv->amax; a++) {
      gv_body b; gv_launch(&b, lv, a);
      long md = 1L << 40; int ev;
      for (;;) { ev = gv_frame(&w, &b);
        long dx = (b.x >> 16) - lv->tx, dy = (b.y >> 16) - lv->ty, d = dx * dx + dy * dy; if (d < md) md = d;
        if (ev != EV_NONE && ev != EV_BOUNCE) break; }
      if (ev == EV_TARGET) { ok++; md = 0; if (a < first) first = a; last = a; }
      if (md < best) { best = md; best_a = a; }
    }
    printf("%2d %-18s %4d angles ok (%d..%d), best %d, closest %ld px\n", li + 1, lv->name, ok, first, last, best_a, (long)(best > 0 ? (long)__builtin_sqrt((double)best) : 0));
    char fn[64]; sprintf(fn, "/tmp/lv%02d.txt", li + 1); FILE *f = fopen(fn, "w");
    fprintf(f, "A %d\n", best_a);
    for (int i = 0; i < w.n_pods; i++) fprintf(f, "P %d %d %d 0\n", w.pods[i].x, w.pods[i].y, w.pods[i].sign);
    gv_body b; gv_launch(&b, lv, best_a);
    for (int fr = 0; fr < GV_LIFE; fr++) { int ev = gv_frame(&w, &b); fprintf(f, "T %d %d\n", b.x >> 16, b.y >> 16); if (ev != EV_NONE && ev != EV_BOUNCE) break; }
    fclose(f);
  }
}
