// L'Aigle d'Or on the MONOCORE-1: platform layer (video, pad, sound, timing) for the port.
// The MO5 bitmap (320 x 200, 2 colours per group of 8 pixels) is kept as 1000 tiles of 8 x 8 in
// 4 bpp in CHR RAM, shown by layer 0; each MO5 byte that changes rewrites one row of one tile.
#include <string.h>
#include "mc.h"
#include "mo5rt.h"
#include "game.h"

extern const uint8_t aigle_image[0x7F00];
extern const uint8_t intro_bin[0xC01], intro_des[0xDFC];

#define TICK_FRAMES 5            // one pass of the game loop every 5 frames (12 per second)

// MO5 palette (colours 0-7 and their pastel versions 8-15)
static const uint16_t mo5_pal[16] = {
  MC_RGB(0, 0, 0),       MC_RGB(255, 0, 0),     MC_RGB(0, 255, 0),     MC_RGB(255, 255, 0),
  MC_RGB(0, 0, 255),     MC_RGB(255, 0, 255),   MC_RGB(0, 255, 255),   MC_RGB(255, 255, 255),
  MC_RGB(128, 128, 128), MC_RGB(255, 128, 128), MC_RGB(128, 255, 128), MC_RGB(255, 255, 128),
  MC_RGB(128, 128, 255), MC_RGB(255, 128, 255), MC_RGB(128, 255, 255), MC_RGB(255, 128, 0),
};

// mask[f]: nibble 0xF for each pixel set in forme byte f (leftmost pixel = low nibble)
static uint32_t mask[256];

void plat_byte_changed(unsigned off) {
  unsigned y = off / 40, xb = off - y * 40;
  unsigned tile = (y >> 3) * 40 + xb;
  uint8_t c = vram_color[off];
  uint32_t m = mask[vram_forme[off]];
  uint32_t w = ((uint32_t)(c >> 4) * 0x11111111u & m) | ((uint32_t)(c & 15) * 0x11111111u & ~m);
  *(volatile uint32_t *)(CHRRAM_BASE + tile * 32 + (y & 7) * 4) = w;
}

// ---------------------------------------------------------------- pad -> MO5 keyboard
// MO5 key numbers: arrows, space, letters and digits used by the game
enum { K_UP = 49, K_DOWN = 33, K_LEFT = 41, K_RIGHT = 25, K_SPACE = 32, K_A = 45, K_B = 34, K_S = 35,
       K_D = 27, K_O = 20, K_P = 28, K_L = 18, K_G = 11, K_I = 12, K_Q = 43 };
static const int digit_key[7] = { 30, 47, 39, 31, 23, 15, 7 };   // 0-6
static int object, pending_key = -1, pending_count;

int plat_key(void) {
  if (pending_count > 0) { pending_count--; return pending_key; }
  uint32_t p = mc_pad(1);
  if (p & IN_COIN) {                                   // COIN = shift for the rarer commands
    uint32_t e = mc_pad_pressed(1);
    if (e & (IN_LEFT | IN_RIGHT)) {
      object = (object + ((e & IN_RIGHT) ? 1 : 6)) % 7;
      pending_key = digit_key[object]; pending_count = 1;
      return pending_key;
    }
    if (p & IN_B(1)) return K_B;
    if (p & IN_B(2)) return K_L;
    if (p & IN_B(3)) return K_I;
    if (p & IN_B(4)) return K_Q;
    return -1;
  }
  if (p & IN_LEFT) return K_LEFT;
  if (p & IN_RIGHT) return K_RIGHT;
  if (p & IN_UP) return K_UP;
  if (p & IN_DOWN) return K_DOWN;
  if (p & IN_START) return K_SPACE;
  if (p & IN_B(1)) return K_P;
  if (p & IN_B(2)) return K_O;
  if (p & IN_B(3)) return K_S;
  if (p & IN_B(4)) return K_D;
  if (p & IN_B(5)) return K_A;
  if (p & IN_B(6)) return K_G;
  return -1;
}

int plat_menu_key(const char *keys) {               // B1, B2... choose keys[0], keys[1]...
  for (;;) {                                         // keys = "": any button
    mc_frame();
    uint32_t e = mc_pad_pressed(1);
    if (!keys[0] && (e & (IN_START | IN_B(1) | IN_B(2) | IN_B(3) | IN_B(4) | IN_B(5) | IN_B(6)))) return 0;
    for (int i = 0; keys[i] && i < 6; i++) if (e & IN_B(i + 1)) return keys[i];
    if ((e & IN_START) && keys[0]) return keys[0];
  }
}

// ---------------------------------------------------------------- timing and sound
void plat_tick(void) { for (int i = 0; i < TICK_FRAMES; i++) mc_frame(); }
void plat_wait_ms(int ms) { int n = ms * 60 / 1000; while (n-- > 0) mc_frame(); }
void plat_beep(int hz, int ms) {
  if (hz > 0) mc_psg(0, PSG_SQUARE, (uint32_t)hz, 128, 160, 160);
  plat_wait_ms(ms);
  mc_psg_volume(0, 0, 0);
}
void plat_border(int c) { (void)c; }      // the picture fills the screen; colour 0 stays black
uint32_t plat_rand(void) { return mc_rand(); }
void cpu_bad_jump(uint16_t pc) { mc_debug("bad jump %x\n", pc); }

int main(void) {
  mc_init();
  for (int f = 0; f < 256; f++) {
    uint32_t m = 0;
    for (int i = 0; i < 8; i++) if (f & (0x80 >> i)) m |= 0xFu << (4 * i);
    mask[f] = m;
  }
  mc_video_mode(320, 200);
  mc_palette(0, mo5_pal, 16);
  mc_background(0);
  mc_layer(0, MC_CHR(0), MC_TILE8 | MC_4BPP, 64, 32, 0);
  for (int y = 0; y < 25; y++)
    for (int x = 0; x < 40; x++) mc_tile(0, x, y, MC_TILE(y * 40 + x, 0, 0));
  mc_display(DISP_ON | DISP_LAYER(0));

  S = 0xAFF0;
  mo5_init();
  memcpy(&mem[0x5000], intro_bin, sizeof intro_bin);     // drawing engine and its variables
  memcpy(&mem[0x9204], intro_des, sizeof intro_des);     // pictures of the title sequence
  intro_run();
  memcpy(&mem[0x2100], aigle_image, sizeof aigle_image);  // what LOADM "AIGLE.JEU" loads
  game_run();
  for (;;) mc_frame();
}
