#include "easy.h"

// a label and a button for each line of the display
static const char *labels[12] = {
  "HAUT", "BAS", "GAUCHE", "DROITE", "BOUTON 1", "BOUTON 2",
  "BOUTON 3", "BOUTON 4", "BOUTON 5", "BOUTON 6", "START", "PIECE"};
static const uint32_t buttons[12] = {
  UP, DOWN, LEFT, RIGHT, B1, B2, B3, B4, B5, B6, START, COIN};

int main(void) {
  init();
  background(0, 40, 30);
  print_ink(10, 1, "ESSAI DE LA MANETTE", 1);
  int presses = 0;

  for (;;) {
    wait_frame();
    for (int i = 0; i < 12; i++) {
      print(10, 4 + i * 2, labels[i]);
      if (held(1, buttons[i])) print_ink(22, 4 + i * 2, "ENFONCE", 3);
      else                     print_ink(22, 4 + i * 2, ".......", 7);
    }
    if (pressed(1, B1)) presses++;          // once per press, even if the button stays down
    print(6, 28, "APPUIS SUR LE BOUTON 1 :");
    print_num(31, 28, presses, 3, 2);
  }
}
