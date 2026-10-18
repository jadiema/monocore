#include "easy.h"

extern const uint8_t sheet_chr[], sheet_pal[];
extern const uint8_t landscape_chr[], landscape_pal[], landscape_map[];
extern const uint32_t landscape_width, landscape_height;     // in 16 x 16 tiles

int main(void) {
  init();
  load_sprites(sheet_chr, sheet_pal, 8);
  load_map(landscape_chr, landscape_pal, landscape_map, landscape_width, landscape_height, 9);
  print_ink(2, 1, "GAUCHE / DROITE : VITESSE", 1);

  int camera = 0;            // screen position in the map, in pixels
  int speed = 1;
  int y = 100;

  for (;;) {
    wait_frame();
    if (pressed(1, RIGHT) && speed < 6)  speed++;
    if (pressed(1, LEFT)  && speed > -6) speed--;
    if (held(1, UP)   && y > 20)  y -= 2;
    if (held(1, DOWN) && y < 160) y += 2;

    camera = camera + speed;
    scroll_map(camera, 0);                // the map is 1024 pixels wide and wraps around by itself

    sprite(0, 152, y, 0);
    print(2, 28, "VITESSE :");
    print_num(12, 28, speed, 2, 2);
  }
}
