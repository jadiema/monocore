#include "easy.h"

// "Au clair de la lune", one note per beat
static const char *moonlight =
  "C C C D E - D - C E D D C - - . "
  "C C C D E - D - C E D D C - - . ";

int main(void) {
  init();
  background(40, 0, 0);
  print_ink(13, 3, "LE PETIT PIANO", 1);
  print(4, 8,  "BOUTONS 1 A 6 : DO RE MI FA SOL LA");
  print(4, 10, "HAUT          : SI");
  print(4, 12, "BAS           : BRUIT");
  print(4, 14, "START         : MUSIQUE OUI / NON");

  static const int notes[6] = {262, 294, 330, 349, 392, 440};   // C D E F G A, in hertz
  static const uint32_t keys[6] = {B1, B2, B3, B4, B5, B6};
  int playing = 0;

  for (;;) {
    wait_frame();
    int frequency = 0;
    for (int i = 0; i < 6; i++)
      if (held(1, keys[i])) frequency = notes[i];
    if (held(1, UP)) frequency = 494;                   // B
    if (frequency) tone(0, frequency, 150); else quiet(0);
    if (held(1, DOWN)) noise(1, 120); else quiet(1);

    if (pressed(1, START)) {
      playing = !playing;
      if (playing) play(2, moonlight, 15, 100);
      else         stop(2);
    }
    print_ink(12, 20, playing ? "MUSIQUE : OUI" : "MUSIQUE : NON", playing ? 3 : 7);
  }
}
