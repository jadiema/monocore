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

  static const int notes[6] = {NOTE_C4, NOTE_D4, NOTE_E4, NOTE_F4, NOTE_G4, NOTE_A4};
  static const uint32_t keys[6] = {PAD_B1, PAD_B2, PAD_B3, PAD_B4, PAD_B5, PAD_B6};
  int playing = 0;

  for (;;) {
    wait_frame();
    int frequency = 0;
    for (int i = 0; i < 6; i++)
      if (held(1, keys[i])) frequency = notes[i];
    if (held(1, PAD_UP)) frequency = NOTE_B4;
    if (frequency) tone(0, frequency, 150); else quiet(0);
    if (held(1, PAD_DOWN)) noise(1, 120); else quiet(1);

    if (pressed(1, PAD_START)) {
      playing = !playing;
      if (playing) play(2, moonlight, 15, 100);
      else         stop(2);
    }
    print_ink(12, 20, playing ? "MUSIQUE : OUI" : "MUSIQUE : NON", playing ? 3 : 7);
  }
}
