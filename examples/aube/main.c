// MONOCORE-1 - AUBE, the start-up screen (boot.rom), in 360 x 270: a dawn over a plain where a black monolith
// stands, an orchestral piece, the name of the console, then "INSÉREZ UNE CARTOUCHE".
// Everything moves through the palette: scene.png only says where each pixel is (sky band and
// glow level, plain, monolith, hills), and the 256 colours are computed at every frame from the
// time (see make_assets.py). The music is a 30 s stereo recording made by make_music.py, played
// by two PCM voices. START plays the sequence again.
#include "mc.h"

extern const uint8_t scene_chr[], scene_map[], logo_chr[], logo_map[], prompt_chr[], prompt_map[];
extern const uint8_t mote_chr[];
extern const int scene_width, scene_height;
extern const uint8_t music_l_file[], music_r_file[];
extern const uint32_t music_l_size, music_r_size;

#define PAL_LOGO   20                  // 4 bpp layers and sprites, after the 256 colours of the scene
#define PAL_PROMPT 21
#define PAL_MOTE   22                  // 22-25: four brightnesses of the specks of light

// ---------------------------------------------------------------- colours (0-255 per channel)
typedef struct { int r, g, b; } rgb;
static rgb mix(rgb a, rgb b, int t) {  // t = 0..256
  return (rgb){ a.r + (b.r - a.r) * t / 256, a.g + (b.g - a.g) * t / 256, a.b + (b.b - a.b) * t / 256 };
}
static int clamp(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
static rgb plain_behind;               // colour of the plain behind the text (set by scene_palette)
static uint16_t out(rgb c, int m) { return MC_RGB(clamp(c.r * m / 256), clamp(c.g * m / 256), clamp(c.b * m / 256)); }

// colour of the sky at height u (0 = top, 256 = horizon)
static rgb sky_at(rgb top, rgb mid, rgb hor, int u) {
  return u < 141 ? mix(top, mid, u * 256 / 141) : mix(mid, hor, (u - 141) * 256 / 115);
}
static rgb add_light(rgb c, rgb l, int a) { return (rgb){ c.r + l.r * a / 256, c.g + l.g * a / 256, c.b + l.b * a / 256 }; }

// m: overall brightness, k: dawn, h: glow of the rising sun, rim: light on the monolith (0-256)
static void scene_palette(int m, int k, int h, int rim) {
  static const rgb G = { 255, 214, 160 };
  rgb top = mix((rgb){ 2, 3, 10 }, (rgb){ 12, 18, 58 }, k);
  rgb mid = mix((rgb){ 6, 6, 22 }, (rgb){ 86, 56, 118 }, k);
  rgb hor = mix((rgb){ 14, 12, 34 }, (rgb){ 255, 150, 84 }, k);
  for (int s = 0; s < 64; s++) PALETTE[1 + s] = out(sky_at(top, mid, hor, s * 256 / 63), m);
  static const int g10[11] = { 0, 7, 21, 39, 59, 85, 112, 142, 175, 214, 256 };   // (g / 10) ^ 1.6
  for (int s = 0; s < 12; s++) {
    rgb c = sky_at(top, mid, hor, s * 256 / 11);
    for (int g = 1; g <= 10; g++) PALETTE[65 + s * 10 + g - 1] = out(add_light(c, G, g10[g] * h / 256), m);
  }
  rgb far = mix((rgb){ 10, 8, 22 }, (rgb){ 64, 40, 58 }, k), near = mix((rgb){ 3, 3, 8 }, (rgb){ 10, 10, 20 }, k);
  plain_behind = mix(far, near, 170);
  plain_behind = (rgb){ plain_behind.r * m / 256, plain_behind.g * m / 256, plain_behind.b * m / 256 };
  static const int r4[4] = { 0, 49, 139, 256 };              // (r / 3) ^ 1.5
  for (int b = 0; b < 12; b++) {
    rgb c = mix(far, near, b * 256 / 11);
    for (int r = 0; r < 4; r++) PALETTE[185 + b * 4 + r] = out(add_light(c, G, r4[r] * h / 256 * 140 / 256), m);
  }
  for (int l = 0; l < 12; l++) {
    int a = l * l * 256 / 121 * rim / 256 * k / 256;
    PALETTE[233 + l] = out((rgb){ 5 + 250 * a / 256, 5 + 185 * a / 256, 9 + 120 * a / 256 }, m);
  }
  rgb hill = mix((rgb){ 6, 6, 16 }, (rgb){ 46, 28, 52 }, k);
  for (int l = 0; l < 10; l++) PALETTE[245 + l] = out(add_light(hill, G, l * l * 256 / 81 * h / 256 * 150 / 256), m);
}

// Anti-aliased text over the plain: there is no transparency, so each grey level is the mix of the
// plain's colour behind the text and the ink, with alpha a (0-256).
static void text_palette(int pal, rgb ink, int a) {
  for (int i = 1; i < 16; i++) PALETTE[pal * 16 + i] = out(mix(plain_behind, ink, a * i / 15), 256);
}

// ---------------------------------------------------------------- the specks of light
#define MOTES 24
static struct { int x, y, vy, life, max; } mote[MOTES];
static void mote_spawn(int i) {
  mote[i].x = (115 + (int)(mc_rand() % 130)) << 8;
  mote[i].y = (125 + (int)(mc_rand() % 45)) << 8;
  mote[i].vy = 40 + (int)(mc_rand() % 50);                  // 1/256 pixel per frame
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
  mc_video_mode(360, 270);                                  // x 4 on a 1080p screen: every pixel sharp
  mc_background(0);
  mc_layer(0, scene_chr, MC_TILE16 | MC_8BPP, 32, 32, 0);
  mc_layer(1, logo_chr, MC_TILE16 | MC_4BPP, 32, 32, 2);
  mc_layer(2, prompt_chr, MC_TILE16 | MC_4BPP, 32, 32, 2);
  load_map(0, scene_map, 0);
  load_map(1, logo_map, PAL_LOGO);
  load_map(2, prompt_map, PAL_PROMPT);
  for (int l = 0; l < 3; l++) mc_layer_scroll(l, 4, 1);       // the images are 368 x 272, shown from (4, 1)
  mc_sprite_sheet(mote_chr);
  for (int q = 0; q < 4; q++)                                // specks: four brightnesses, never darker than the sky
    for (int i = 1; i < 16; i++) {
      int v = 96 + (q + 1) * 40 * i / 15;
      PALETTE[(PAL_MOTE + q) * 16 + i] = MC_RGB(clamp(v + 40), clamp(v + 20), clamp(v - 10));
    }
  scene_palette(0, 0, 0, 0); text_palette(PAL_LOGO, (rgb){ 0, 0, 0 }, 0); text_palette(PAL_PROMPT, (rgb){ 0, 0, 0 }, 0);
  mc_display(DISP_ON | DISP_LAYER(0) | DISP_SPRITES);
  for (int i = 0; i < MOTES; i++) { mote_spawn(i); mote[i].life = -(int)(mc_rand() % 400); }
  start_music();

  for (int t = 0;; t++) {
    mc_frame();
    if (mc_pad_pressed(1) & IN_START) { t = 0; start_music(); }
    // the light: dawn, rising sun (with a flash when the logo appears at 12 s), rim of the monolith
    int m = ramp(t, 20, 300), k = ramp(t, 60, 720), h = ramp(t, 240, 720) * 230 / 256, rim = ramp(t, 600, 780);
    if (t >= 720) {
      int f = t - 720;
      h += f < 180 ? (180 - f) * 70 / 180 : 0;              // flash, fading in 3 s
      if (t > 1800) h += mc_sin((t - 1800) * 3) * 14 / 65536; // then a slow breathing
    }
    scene_palette(m, k, h, rim);
    text_palette(PAL_LOGO, (rgb){ 255, 244, 225 }, ramp(t, 720, 870));
    mc_display(DISP_ON | DISP_LAYER(0) | DISP_SPRITES | (t >= 720 ? DISP_LAYER(1) : 0) | (t >= 960 ? DISP_LAYER(2) : 0));
    int pa = ramp(t, 960, 1080);
    if (t > 1080) pa = pa * (176 + mc_sin((t - 1080) * 4) * 80 / 65536) / 256;
    text_palette(PAL_PROMPT, (rgb){ 210, 196, 176 }, pa);
    // specks of light rising in the glow
    int n = t < 360 ? 0 : t < 900 ? (t - 360) * MOTES / 540 : MOTES;
    for (int i = 0; i < MOTES; i++) {
      if (i >= n || mote[i].life < 0) { mote[i].life++; mc_sprite_hide(i); continue; }
      mote[i].y -= mote[i].vy; mote[i].life++;
      mote[i].x += mc_sin(mote[i].life * 6 + i * 40) * 24 / 65536;
      if (mote[i].life > mote[i].max || mote[i].y < (34 << 8)) { mote_spawn(i); continue; }
      int sx = mote[i].x >> 8, sy = mote[i].y >> 8;
      if (sx > 162 && sx < 192 && sy > 62 && sy < 172) { mc_sprite_hide(i); continue; }   // behind the monolith
      int u = mote[i].life * 256 / mote[i].max;              // brightness: up, then down
      int q = (u < 128 ? u : 256 - u) * 4 / 129;
      mc_sprite(i, mote[i].x >> 8, mote[i].y >> 8, 0, 1, 1, PAL_MOTE + (q > 3 ? 3 : q), 1);
    }
  }
}
