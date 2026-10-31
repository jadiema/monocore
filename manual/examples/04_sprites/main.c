#include "easy.h"

// pictures of sheet.png: sheet is the number of its first picture
#define SHIP   (sheet + 0)
#define STAR   (sheet + 1)
#define STAR2  (sheet + 2)

int main(void) {
  init();                        // also loads sheet.png
  background(0, 0, 40);
  print_ink(3, 1, "MANETTE:BOUGER  B1:MIROIR  B2:TAILLE", 2);

  int x = 152, y = 120;          // ship position
  int flipped = 0, big = 0;

  for (;;) {
    wait_frame();
    if (held(1, PAD_LEFT))  x = x - 2;
    if (held(1, PAD_RIGHT)) x = x + 2;
    if (held(1, PAD_UP))    y = y - 2;
    if (held(1, PAD_DOWN))  y = y + 2;
    if (pressed(1, PAD_B1)) { flipped = !flipped; flip_sprite(0, 0, flipped); }
    if (pressed(1, PAD_B2)) { big = !big;         scale_sprite(0, big ? 200 : 100); }
    sprite(0, x, y, SHIP);

    // five twinkling stars: the picture changes every 10 frames
    for (int i = 0; i < 5; i++) {
      int picture = ((frame_count / 10 + i) % 2) ? STAR : STAR2;
      sprite(1 + i, 30 + i * 60, 50, picture);
    }
  }
}
