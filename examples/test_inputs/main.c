// Input test: the 4 pads, their analog sticks, paddles and spinners, and the keyboard.
// Everything the console reads is shown as text, so a screenshot is enough to check an input.
#include <stdio.h>
#include "mc.h"

static const char *const names[12] = { "H", "B", "G", "D", "1", "2", "3", "4", "5", "6", "S", "P" };
static char line[48];
static int events[6], nev;

static void show_pad(int p, int row) {
  uint32_t v = mc_pad(p);
  int n = snprintf(line, sizeof line, "J%d ", p);
  for (int b = 0; b < 12; b++) n += snprintf(line + n, sizeof line - n, "%s", (v >> b) & 1 ? names[b] : ".");
  mc_text(0, 1, row, line, 0);
  snprintf(line, sizeof line, "   G %4d %4d  D %4d %4d", mc_stick_x(p, 0), mc_stick_y(p, 0), mc_stick_x(p, 1), mc_stick_y(p, 1));
  mc_text(0, 1, row + 1, line, 0);
  snprintf(line, sizeof line, "   PADDLE %3d  SPINNER %5d", mc_paddle(p), mc_spinner(p));
  mc_text(0, 1, row + 2, line, 0);
}

int main(void) {
  mc_init();
  mc_video_mode(320, 240);
  mc_text_layer(0, 3);
  mc_display(DISP_ON | DISP_LAYER(0));
  for (;;) {
    mc_frame();
    mc_text_clear(0);
    snprintf(line, sizeof line, "ENTREES   VERSION MATERIELLE %d", mc_hw_version());
    mc_text(0, 1, 1, line, 0);
    for (int p = 1; p <= 4; p++) show_pad(p, 3 + (p - 1) * 4);
    int e;
    while ((e = mc_key_event()) >= 0) {
      if (nev == 6) { for (int i = 0; i < 5; i++) events[i] = events[i + 1]; nev = 5; }
      events[nev++] = e;
    }
    int n = snprintf(line, sizeof line, "EVT");
    for (int i = 0; i < nev; i++) n += snprintf(line + n, sizeof line - n, " %c%03X", events[i] & 0x200 ? '+' : '-', events[i] & 0x1FF);
    mc_text(0, 1, 20, line, 0);
    n = snprintf(line, sizeof line, "BAS");
    for (int k = 0; k < 0x200 && n < 34; k++)
      if (((k & 0xFF) < 0x80 && k != 0x02) || k == 0x83) if (mc_key(k)) n += snprintf(line + n, sizeof line - n, " %03X", k);
    mc_text(0, 1, 22, line, 0);
    // mouse (hardware version 3): pointer, buttons, wheel total (+ = turned away from you)
    static int wheel_total;
    wheel_total += mc_mouse_wheel();
    snprintf(line, sizeof line, "SOURIS %s %3d,%3d B%c%c%c MOL %+d", mc_mouse_present() ? "OUI" : "NON",
             mc_mouse_x(), mc_mouse_y(), mc_mouse_buttons() & MOUSE_LEFT ? 'G' : '.',
             mc_mouse_buttons() & MOUSE_MIDDLE ? 'M' : '.', mc_mouse_buttons() & MOUSE_RIGHT ? 'D' : '.', wheel_total);
    mc_text(0, 1, 24, line, 0);
    if (mc_mouse_present()) mc_text(0, mc_mouse_x() / 8, mc_mouse_y() / 8, "+", 0);
  }
}
