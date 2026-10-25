// The mtrack module as a game uses it: the demo song of MONOTRACKER plays, button 1 plays a
// sound effect on PSG channel 8 (reserved: the song leaves it alone), button 2 a PCM effect on
// voice 8 (shared: the song gets it back when the effect is over), START pauses.
// Instrument numbers are those of the song (here: 09 BIP, 1E ORCH HIT).
#include <stdio.h>
#include "mc.h"
#include "mtrack.h"

int main(void) {
  mc_init();
  mc_video_mode(320, 240);
  mc_text_layer(0, 3);
  mt_init(mtbank_file);
  mt_reserve(MT_PSG(7), true);
  mt_play(demo_file, 0);
  bool paused = false;
  char line[48];
  for (;;) {
    mc_frame();
    uint32_t p = mc_pad_pressed(1);
    if (p & IN_B(1)) mt_sfx(MT_PSG(7), 73 + (mc_rand() % 12), 9, 64);
    if (p & IN_B(2)) mt_sfx(MT_PCM(7), 37, 30, 64);
    if (p & IN_START) { paused = !paused; mt_pause(paused); }
    mc_text_clear(0);
    mc_text(0, 2, 2, "TEST DU MODULE MTRACK", 0);
    snprintf(line, sizeof line, "%s", mt_song_name(demo_file));
    mc_text(0, 2, 4, line, 0);
    snprintf(line, sizeof line, "POSITION %02d  LIGNE %02d %s", mt_order(), mt_row(), paused ? "PAUSE" : "");
    mc_text(0, 2, 6, line, 0);
    for (int t = 0; t < MT_TRACKS; t++) {
      int n = mt_level(t) / 16;
      int k = snprintf(line, sizeof line, "%s%d ", t < 8 ? "PSG" : "PCM", (t & 7) + 1);
      for (int i = 0; i < 16; i++) line[k++] = i < n ? '#' : '.';
      line[k] = 0;
      mc_text(0, 2, 8 + t, line, 0);
    }
    mc_text(0, 2, 26, "B1 BRUITAGE PSG  B2 PCM  START PAUSE", 0);
  }
}
