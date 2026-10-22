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
  print_ink(10, 1, "ESSAI DES MANETTES", 1);
  int presses[5] = {0, 0, 0, 0, 0};       // presses[1] to presses[4]: one for each player

  for (int player = 1; player <= 4; player++) {
    print_ink(9 + player * 6, 3, "J", 2);
    print_num(10 + player * 6, 3, player, 1, 2);
  }
  for (;;) {
    wait_frame();
    for (int i = 0; i < 12; i++) {
      print(2, 5 + i * 2, labels[i]);
      for (int player = 1; player <= 4; player++) {
        if (held(player, buttons[i])) print_ink(9 + player * 6, 5 + i * 2, "##", 3);
        else                          print_ink(9 + player * 6, 5 + i * 2, "..", 7);
      }
    }
    print(2, 29, "APPUIS B1");
    for (int player = 1; player <= 4; player++) {
      if (pressed(player, B1)) presses[player]++;   // once per press, even if the button stays down
      print_num(8 + player * 6, 29, presses[player], 3, 2);
    }
  }
}
