// GRAVITON level checker (PC): random search of pod placements with the game's own physics.
// cc -O2 -I.. solve.c -o solve && ./solve [level] [trials]
#include <stdio.h>
#include <stdlib.h>
#include "../physics.h"
#include "../levels.h"

static uint32_t rs = 12345;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }

// closest approach to the target (1/256 px^2), 0 when hit
static int64_t shot_score(const gv_world *w, int angle, int *ev_out) {
  gv_body b; gv_launch(&b, w->lv, angle);
  int64_t best = (int64_t)1 << 40;
  for (;;) {
    int ev = EV_NONE;
    for (int s = 0; s < GV_SUB && (ev == EV_NONE || ev == EV_BOUNCE); s++) {
      ev = gv_step(w, &b);
      int64_t dx = (b.x >> 12) - w->lv->tx * 16, dy = (b.y >> 12) - w->lv->ty * 16;
      int64_t d = dx * dx + dy * dy;
      if (d < best) best = d;
    }
    if (ev == EV_NONE || ev == EV_BOUNCE) { if (++b.frame >= GV_LIFE) ev = EV_LOST; else continue; }
    *ev_out = ev;
    return ev == EV_TARGET ? 0 : best;
  }
}

// hill climbing from random starts; returns 1 and the solution in *out
static int climb(const gv_level *lv, int np, int nq, int restarts, gv_world *out, int *angle) {
  for (int r = 0; r < restarts; r++) {
    gv_world w; gv_world_init(&w, lv);
    int k = np + nq, fail = 0;
    for (int i = 0; i < k; i++) {
      int x, y, tries = 0;
      do { x = rnd() % GV_W; y = rnd() % GV_H; } while (!gv_can_place(&w, x, y, -1) && ++tries < 500);
      if (tries >= 500) { fail = 1; break; }
      gv_pod p = { (int16_t)x, (int16_t)y, (int8_t)(i < np ? 1 : -1), GV_POD_POWER };
      w.pods[w.n_pods++] = p;
    }
    if (fail) continue;
    int a = lv->amin, ev; int64_t sc = (int64_t)1 << 62;
    for (int aa = lv->amin; aa <= lv->amax; aa += 15) {        // coarse sweep of the angle first
      int64_t s1 = shot_score(&w, aa, &ev);
      if (s1 < sc) { sc = s1; a = aa; }
    }
    for (int it = 0; it < 400 && sc > 0; it++) {
      gv_world w2 = w; int a2 = a;
      int step = 1 + rnd() % 24;
      int which = rnd() % (k + 1);
      if (which == k || k == 0) { if (lv->amax > lv->amin) { a2 += (rnd() & 1) ? (int)(rnd() % 81) - 40 : (int)(rnd() % 5) - 2; if (a2 < lv->amin) a2 = lv->amin; if (a2 > lv->amax) a2 = lv->amax; } else continue; }
      else {
        int i = lv->n_fixed + which;
        int x = w2.pods[i].x + (int)(rnd() % (2 * step + 1)) - step, y = w2.pods[i].y + (int)(rnd() % (2 * step + 1)) - step;
        if (!gv_can_place(&w2, x, y, i)) continue;
        w2.pods[i].x = (int16_t)x; w2.pods[i].y = (int16_t)y;
      }
      int64_t s2 = shot_score(&w2, a2, &ev);
      if (s2 <= sc) { sc = s2; w = w2; a = a2; }
    }
    if (sc == 0) { *out = w; *angle = a; return 1; }
  }
  return 0;
}

int main(int argc, char **argv) {
  int only = argc > 1 ? atoi(argv[1]) : -1;
  long trials = argc > 2 ? atol(argv[2]) : 200000;
  int dump = argc > 3;
  int restarts = argc > 4 ? atoi(argv[4]) : 300;
  if (argc > 5) rs = (uint32_t)atoi(argv[5]);
  int kmin = argc > 6 ? atoi(argv[6]) : 0;
  for (int li = 0; li < GV_LEVELS; li++) {
    if (only >= 0 && li != only) continue;
    const gv_level *lv = &gv_levels[li];
    gv_world w; gv_world_init(&w, lv);
    int direct = 0;
    for (int a = lv->amin; a <= lv->amax; a++) if (gv_simulate(&w, a, 0) == EV_TARGET) direct++;
    long ok[4] = {0}, tried[4] = {0};
    int best_k = 99; gv_world best; int best_a = 0, best_f = 0;
    int nmax = lv->n_pull + lv->n_push;
    for (long t = 0; t < trials; t++) {
      gv_world_init(&w, lv);
      int np = rnd() % (lv->n_pull + 1), nq = rnd() % (lv->n_push + 1);
      int k = np + nq;
      int fail = 0;
      for (int i = 0; i < k; i++) {
        int x, y, tries = 0;
        do { x = rnd() % GV_W; y = rnd() % GV_H; } while (!gv_can_place(&w, x, y, -1) && ++tries < 200);
        if (tries >= 200) { fail = 1; break; }
        gv_pod p = { (int16_t)x, (int16_t)y, (int8_t)(i < np ? 1 : -1), GV_POD_POWER };
        w.pods[w.n_pods++] = p;
      }
      if (fail) continue;
      int a = lv->amin + (lv->amax > lv->amin ? (int)(rnd() % (lv->amax - lv->amin + 1)) : 0);
      int f, ev = gv_simulate(&w, a, &f);
      int kk = k > 3 ? 3 : k;
      tried[kk]++;
      if (ev == EV_TARGET) {
        ok[kk]++;
        if (k < best_k) { best_k = k; best = w; best_a = a; best_f = f; }
      }
    }
    printf("%2d %-22s direct %d/%d  ", li + 1, lv->name, direct, lv->amax - lv->amin + 1);
    for (int k = 0; k <= 3 && k <= nmax; k++)
      printf(" %d pods: %5.2f%%", k, tried[k] ? 100.0 * ok[k] / tried[k] : 0.0);
    if (best_k < 99) {
      printf("\n     e.g. angle %d, %d frames:", best_a, best_f);
      for (int i = lv->n_fixed; i < best.n_pods; i++)
        printf(" %c(%d,%d)", best.pods[i].sign > 0 ? '+' : '-', best.pods[i].x, best.pods[i].y);
    } else printf("\n     random: none");
    {
      gv_world sw; int sa;
      int found = 0;
      for (int k = kmin; k <= nmax && !found; k++)
        for (int np = 0; np <= lv->n_pull && np <= k && !found; np++) {
          int nq = k - np; if (nq > lv->n_push) continue;
          if (climb(lv, np, nq, restarts, &sw, &sa)) {
            found = 1;
            printf("\n     climb: %d pods, angle %d:", k, sa);
            if (dump) {
              char fn[64]; sprintf(fn, "/tmp/lv%02d.txt", li + 1);
              FILE *f = fopen(fn, "w");
              fprintf(f, "A %d\n", sa);
              for (int i = 0; i < sw.n_pods; i++) fprintf(f, "P %d %d %d %d\n", sw.pods[i].x, sw.pods[i].y, sw.pods[i].sign, i >= lv->n_fixed);
              gv_body b; gv_launch(&b, lv, sa);
              for (int fr = 0; fr < GV_LIFE; fr++) {
                int ev = gv_frame(&sw, &b);
                fprintf(f, "T %d %d\n", b.x >> 16, b.y >> 16);
                if (ev != EV_NONE && ev != EV_BOUNCE) break;
              }
              fclose(f);
            }
            for (int i = lv->n_fixed; i < sw.n_pods; i++)
              printf(" %c(%d,%d)", sw.pods[i].sign > 0 ? '+' : '-', sw.pods[i].x, sw.pods[i].y);
          }
        }
      if (!found) printf("\n     climb: NO SOLUTION");
    }
    printf("\n");
  }
  return 0;
}
