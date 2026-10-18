#include "easy.h"

int main(void) {
  init();
  background(20, 0, 60);                     // a very dark purple

  print_ink(12, 3, "LES HUIT ENCRES", 1);    // ink 1: yellow
  for (int n = 0; n < 8; n++) {              // n takes the values 0, 1, ... up to 7
    print_ink(12, 7 + n * 2, "ENCRE NUMERO", n);
    print_num(25, 7 + n * 2, n, 1, n);
  }

  int seconds = 0;
  for (;;) {                                 // forever
    wait_frame();
    if (frame_count % 60 == 0) seconds = seconds + 1;   // 60 frames = 1 second
    print(9, 25, "SECONDES :");
    print_num(20, 25, seconds, 4, 2);

    // blinking text: on for half a second, off for half a second
    if ((frame_count / 30) % 2 == 0) print_ink(12, 27, "APPUIE SUR START", 6);
    else                             print    (12, 27, "                ");

    if (pressed(1, START)) background(random(256), random(256), random(256));
  }
}
