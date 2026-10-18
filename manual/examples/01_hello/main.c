#include "easy.h"

int main(void) {
  init();
  background(0, 0, 120);
  print(14, 12, "BONJOUR !");
  print(8, 16, "JE SUIS LA MONOCORE-1.");
  for (;;) wait_frame();
}
