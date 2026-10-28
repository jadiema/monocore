// winning angles of an aim-only level, with the closest pass to a pod along each
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../physics.h"
#include "../levels.h"
int main(int argc, char **argv) {
  const gv_level *lv = &gv_levels[atoi(argv[1])];
  gv_world w; gv_world_init(&w, lv);
  for (int a = lv->amin; a <= lv->amax; a++) {
    gv_body b; gv_launch(&b, lv, a); double md = 1e9; int ev;
    for (;;) { ev = gv_frame(&w, &b);
      for (int i = 0; i < w.n_pods; i++) { double d = hypot(b.x / 65536.0 - w.pods[i].x, b.y / 65536.0 - w.pods[i].y); if (d < md) md = d; }
      if (ev != EV_NONE && ev != EV_BOUNCE) break; }
    if (ev == EV_TARGET) printf("%d: %.1f px  ", a, md);
  }
  printf("\n");
}
