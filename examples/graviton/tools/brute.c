// GRAVITON: exhaustive search for a level with one pod to place (grid of 4 px, angles every 0.5 deg)
#include <stdio.h>
#include <stdlib.h>
#include "../physics.h"
#include "../levels.h"
int main(int argc, char **argv) {
  int li = atoi(argv[1]), sign = argc > 2 ? atoi(argv[2]) : 1;
  const gv_level *lv = &gv_levels[li];
  int found = 0;
  for (int y = GV_TOP + 8; y < GV_H - 8; y += 4)
    for (int x = 8; x < GV_W - 8; x += 4) {
      gv_world w; gv_world_init(&w, lv);
      if (!gv_can_place(&w, x, y, -1)) continue;
      gv_pod p = { (int16_t)x, (int16_t)y, (int8_t)sign, GV_POD_POWER }; w.pods[w.n_pods++] = p;
      int n = 0, a0 = 0;
      for (int a = lv->amin; a <= lv->amax; a += 5) if (gv_simulate(&w, a, 0) == EV_TARGET) { if (!n) a0 = a; n++; }
      if (n) { found++; if (found <= 5) printf("pod (%d,%d): %d angles, e.g. %d\n", x, y, n, a0); }
    }
  printf("%d placements solve level %d\n", found, li + 1);
}
