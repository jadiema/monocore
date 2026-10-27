// MONOCORE-1 - AUBE, the start-up screen (boot.rom), in 640 x 480 interlaced (hardware version 5): a dawn over a lake where a black monolith
// stands, an orchestral piece, the name of the console, then "INSÉREZ UNE CARTOUCHE".
// Everything moves through the palette: scene.png is the painting monolithe.png in 230 colours (plus
// 12 for the rim of the monolith), and every colour goes from its night shade to the painting's,
// the bright ones near the sun first (scene_colors.h, made by make_assets.py). The music is a 30 s stereo recording made by make_music.py, played
// by two PCM voices. START plays the sequence again.
#include "mc.h"
#include "scene_colors.h"                      // made by make_assets.py from monolithe.png

#define PAL_MOTE   22                  // 4 bpp sprites, after the 256 colours of the scene: 22-25, four
                                       // brightnesses of the specks of light
#define PAL_TEXT   32                  // then one palette per tile of text (TEXT_TILES of them)

// ---------------------------------------------------------------- colours (0-255 per channel)
typedef struct { int r, g, b; } rgb;
static rgb mix(rgb a, rgb b, int t) {  // t = 0..256
  return (rgb){ a.r + (b.r - a.r) * t / 256, a.g + (b.g - a.g) * t / 256, a.b + (b.b - a.b) * t / 256 };
}
static int clamp(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
static rgb cur[256];                   // colour of each scene index in this frame (for the text behind)
static uint16_t out(rgb c, int m) { return MC_RGB(clamp(c.r * m / 256), clamp(c.g * m / 256), clamp(c.b * m / 256)); }

// m: overall brightness, k: dawn, h: flash of the sun, rim: light on the monolith (0-256)
static void scene_palette(int m, int k, int h, int rim) {
  static const rgb G = { 255, 214, 160 };
  for (int i = 0; i < SCENE_COLOURS; i++) {
    int e = scene_colour[i].early;                           // bright colours near the sun catch the dawn first
    int ki = k * 3 / 2 - (256 - e) / 2;
    ki = ki < 0 ? 0 : ki > 256 ? 256 : ki;
    ki = ki * ki * (768 - 2 * ki) / 65536;                   // smooth
    rgb c = mix((rgb){ scene_colour[i].nr, scene_colour[i].ng, scene_colour[i].nb },
                (rgb){ scene_colour[i].r, scene_colour[i].g, scene_colour[i].b }, ki);
    int a = scene_colour[i].flash * h / 256;
    c = (rgb){ (c.r + G.r * a / 256) * m / 256, (c.g + G.g * a / 256) * m / 256, (c.b + G.b * a / 256) * m / 256 };
    cur[1 + i] = c;
    PALETTE[1 + i] = out(c, 256);
  }
  for (int l = 0; l < 12; l++) {
    int a = l * l * 256 / 121 * rim / 256 * k / 256;
    PALETTE[FIRST_MONO + l] = out((rgb){ 3 + 250 * a / 256, 3 + 185 * a / 256, 9 + 120 * a / 256 }, m);
  }
}

// Anti-aliased text over the painting: there is no transparency, so every tile of text has its own
// palette, where each grey level is the mix of the colour behind that tile and the ink, with alpha a.
static void text_palette(int layer, rgb ink, int a) {
  for (int j = 0; j < TEXT_TILES; j++) {
    if (text_tiles[j].layer != layer) continue;
    rgb bg = cur[text_tiles[j].behind];
    for (int i = 1; i < 16; i++) PALETTE[(PAL_TEXT + j) * 16 + i] = out(mix(bg, ink, a * i / 15), 256);
  }
}

// ---------------------------------------------------------------- the specks of light
#define MOTES 24
static struct { int x, y, vy, life, max; } mote[MOTES];
static void mote_spawn(int i) {
  mote[i].x = (204 + (int)(mc_rand() % 231)) << 8;          // (positions of the 360 x 270 version x 16 / 9)
  mote[i].y = (222 + (int)(mc_rand() % 80)) << 8;
  mote[i].vy = 71 + (int)(mc_rand() % 89);                  // 1/256 pixel per frame
  mote[i].max = 300 + (int)(mc_rand() % 300);
  mote[i].life = 0;
}

// ---------------------------------------------------------------- timeline (frames, 60 per second)
static int ramp(int t, int t0, int t1) {                     // 0 before t0, 256 after t1, smooth
  if (t <= t0) return 0;
  if (t >= t1) return 256;
  int u = (t - t0) * 256 / (t1 - t0);
  return u * u * (768 - 2 * u) / 65536;                     // smoothstep
}

static void load_map(int layer, const uint8_t *map, int pal) {
  for (int y = 0; y < scene_height; y++)
    for (int x = 0; x < scene_width; x++) {
      int v = map[2 * (y * scene_width + x)] | map[2 * (y * scene_width + x) + 1] << 8;
      mc_tile(layer, x, y, MC_TILE(v, pal, 0));
    }
}

static void start_music(void) {
  mc_pcm_play(0, music_l_file, music_l_size / 2, PCM_S16, 22050, PCM_NOLOOP, 255, 0);
  mc_pcm_play(1, music_r_file, music_r_size / 2, PCM_S16, 22050, PCM_NOLOOP, 0, 255);
}

int main(void) {
  mc_init();
  mc_video_mode(640, 480);                                  // interlaced: fields of 240 lines, 640 pixels wide
  mc_background(0);
  mc_layer(0, scene_chr, MC_TILE16 | MC_8BPP, 64, 32, 0);   // 64 x 32 tiles of 16 x 16: 1024 x 512 pixels
  mc_layer(1, logo_chr, MC_TILE16 | MC_4BPP, 64, 32, 2);
  mc_layer(2, prompt_chr, MC_TILE16 | MC_4BPP, 64, 32, 2);
  load_map(0, scene_map, 0);
  load_map(1, logo_map, 0);
  load_map(2, prompt_map, 0);
  for (int j = 0; j < TEXT_TILES; j++) {                     // each tile of text: its own palette
    const uint8_t *map = text_tiles[j].layer == 1 ? logo_map : prompt_map;
    int p = 2 * (text_tiles[j].y * scene_width + text_tiles[j].x);
    mc_tile(text_tiles[j].layer, text_tiles[j].x, text_tiles[j].y, MC_TILE(map[p] | map[p + 1] << 8, PAL_TEXT + j, 0));
  }
  for (int l = 0; l < 3; l++) mc_layer_scroll(l, 8, 8);       // the images are 656 x 496, shown from (8, 8)
  mc_sprite_sheet(mote_chr);
  for (int q = 0; q < 4; q++)                                // specks: four brightnesses, never darker than the sky
    for (int i = 1; i < 16; i++) {
      int v = 96 + (q + 1) * 40 * i / 15;
      PALETTE[(PAL_MOTE + q) * 16 + i] = MC_RGB(clamp(v + 40), clamp(v + 20), clamp(v - 10));
    }
  scene_palette(0, 0, 0, 0); text_palette(1, (rgb){ 0, 0, 0 }, 0); text_palette(2, (rgb){ 0, 0, 0 }, 0);
  mc_display(DISP_ON | DISP_LAYER(0) | DISP_SPRITES);
  for (int i = 0; i < MOTES; i++) { mote_spawn(i); mote[i].life = -(int)(mc_rand() % 400); }
  start_music();

  for (int t = 0;; t++) {
    mc_frame();
    if (mc_pad_pressed(1) & IN_START) { t = 0; start_music(); }
    // the light: dawn, rising sun (with a flash when the logo appears at 12 s), rim of the monolith
    int m = ramp(t, 20, 300), k = ramp(t, 60, 720), h = 0, rim = ramp(t, 600, 780);
    if (t >= 720) {
      int f = t - 720;
      h += f < 180 ? (180 - f) * 90 / 180 : 0;              // flash, fading in 3 s
      if (t > 1800) h += 14 + mc_sin((t - 1800) * 3) * 14 / 65536; // then a slow breathing
    }
    scene_palette(m, k, h, rim);
    text_palette(1, (rgb){ 255, 244, 225 }, ramp(t, 720, 870));
    mc_display(DISP_ON | DISP_LAYER(0) | DISP_SPRITES | (t >= 720 ? DISP_LAYER(1) : 0) | (t >= 960 ? DISP_LAYER(2) : 0));
    int pa = ramp(t, 960, 1080);
    if (t > 1080) pa = pa * (176 + mc_sin((t - 1080) * 4) * 80 / 65536) / 256;
    text_palette(2, (rgb){ 210, 196, 176 }, pa);
    // specks of light rising in the glow
    int n = t < 360 ? 0 : t < 900 ? (t - 360) * MOTES / 540 : MOTES;
    for (int i = 0; i < MOTES; i++) {
      if (i >= n || mote[i].life < 0) { mote[i].life++; mc_sprite_hide(i); continue; }
      mote[i].y -= mote[i].vy; mote[i].life++;
      mote[i].x += mc_sin(mote[i].life * 6 + i * 40) * 43 / 65536;
      if (mote[i].life > mote[i].max || mote[i].y < (60 << 8)) { mote_spawn(i); continue; }
      int sx = mote[i].x >> 8, sy = mote[i].y >> 8;
      if (sx > MONO_X0 - 16 && sx < MONO_X1 && sy > MONO_Y0 - 16 && sy < MONO_Y1) { mc_sprite_hide(i); continue; }   // behind the monolith
      int u = mote[i].life * 256 / mote[i].max;              // brightness: up, then down
      int q = (u < 128 ? u : 256 - u) * 4 / 129;
      mc_sprite(i, mote[i].x >> 8, mote[i].y >> 8, 0, 2, 2, PAL_MOTE + (q > 3 ? 3 : q), 1);
    }
  }
}
