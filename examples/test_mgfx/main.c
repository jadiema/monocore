// test_mgfx: shows how a game uses a bank drawn with MONOPIXEL (module mgfx, sdk/MGFX.md).
// The demo bank of MONOPIXEL: its decor on layer 1 (scrolling), the hero (animated sprite) that
// runs and jumps on the tiles (mg_tile_at), coins that turn, stars that twinkle.
#include <stdio.h>
#include "mc.h"
#include "mgfx.h"

static mg_bank_t bank;
static int hero, coin, star;
#define SOLID(t) ((t) >= 1 && (t) <= 7 && (t) != 5)   // grass, dirt, bricks, stone, crates, leaves; not water

static bool solid_at(int x, int y) { return y >= 0 && SOLID(mg_tile_at(1, x, y)); }

int main(void) {
  mc_init();
  mc_video_mode(320, 240);
  if (mg_open(&bank, demo_file, demo_size)) { mc_debug("banque illisible\n"); for (;;) mc_frame(); }
  // the font of mc_text uses palette 0 and CHR in ROM; the bank goes into the CHR RAM, palettes 16 on
  if (!mg_load(&bank, 0, 16, MG_LOAD_ALL)) { mc_debug("CHR RAM trop petite\n"); for (;;) mc_frame(); }
  mg_map_layer(&bank, 0, 1, 1);
  mc_text_layer(0, 3);
  mc_color(15 * 16 + 1, MC_RGB(120, 200, 240));    // sky
  mc_background(15 * 16 + 1);
  mc_display(DISP_ON | DISP_LAYER(0) | DISP_LAYER(1) | DISP_SPRITES);
  hero = mg_sprite_find(&bank, "HEROS"); coin = mg_sprite_find(&bank, "PIECE"); star = mg_sprite_find(&bank, "ETOILE");
  mc_debug("banque %s : %d lutins, decor %dx%d, CHR RAM %u octets\n", mg_name(&bank), mg_sprite_count(&bank),
           mg_map_w(&bank, 0), mg_map_h(&bank, 0), (unsigned)mg_load_size(&bank, MG_LOAD_ALL));
  int coins[8][3];                                 // x, y, taken
  for (int i = 0; i < 8; i++) { coins[i][0] = 120 + i * 110; coins[i][1] = 140 - (i % 3) * 40; coins[i][2] = 0; }
  int x = 40 * 16, y = 100 * 16, vy = 0, face = 0, score = 0, cam = 0;   // 1/16 pixel
  char s[40];
  for (;;) {
    mc_frame();
    uint32_t p = mc_pad(1);
    int vx = (p & IN_LEFT) ? -24 : (p & IN_RIGHT) ? 24 : 0;
    if (vx) face = vx < 0;
    int nx = x + vx, px = nx / 16, py = y / 16;
    if (!solid_at(px + (vx > 0 ? 13 : 2), py + 4) && !solid_at(px + (vx > 0 ? 13 : 2), py + 14)) x = nx;
    bool ground = solid_at(x / 16 + 3, y / 16 + 16) || solid_at(x / 16 + 12, y / 16 + 16);
    if (ground && (mc_pad_pressed(1) & IN_B(1))) vy = -78;
    vy += 4; if (vy > 96) vy = 96;
    int ny = y + vy;
    if (vy > 0 && (solid_at(x / 16 + 3, ny / 16 + 16) || solid_at(x / 16 + 12, ny / 16 + 16))) { ny = ((ny / 16 + 16) & ~15) * 16 - 16 * 16; vy = 0; }
    if (vy < 0 && (solid_at(x / 16 + 3, ny / 16 + 1) || solid_at(x / 16 + 12, ny / 16 + 1))) vy = 0, ny = y;
    y = ny;
    if (x < 0) x = 0;
    if (x > (64 * 16 - 16) * 16) x = (64 * 16 - 16) * 16;
    cam = x / 16 - 150; if (cam < 0) cam = 0; if (cam > 64 * 16 - 320) cam = 64 * 16 - 320;
    mc_layer_scroll(1, cam, 0);
    int fr = vx ? mg_frame_at(&bank, hero, mc_frames) : 0;
    mg_sprite(&bank, 1, hero, fr, x / 16 - cam, y / 16, 4);
    mc_sprite_flip(1, face, false);
    for (int i = 0; i < 8; i++) {
      int cx = coins[i][0], cy = coins[i][1];
      if (!coins[i][2] && cx < x / 16 + 14 && cx + 14 > x / 16 && cy < y / 16 + 14 && cy + 14 > y / 16) { coins[i][2] = 1; score++; }
      if (coins[i][2]) mc_sprite_hide(2 + i);
      else mg_sprite(&bank, 2 + i, coin, mg_frame_at(&bank, coin, mc_frames + i * 5), cx - cam, cy, 3);
    }
    for (int i = 0; i < 3; i++) mg_sprite(&bank, 12 + i, star, mg_frame_at(&bank, star, mc_frames + i * 7), 60 + i * 100, 20 + (i & 1) * 12, 2);
    snprintf(s, sizeof s, "PIECES %d/8   GAUCHE/DROITE, B1 SAUTE", score);
    mc_text(0, 1, 1, s, 0);
  }
}
