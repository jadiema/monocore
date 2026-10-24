// MONOCORE-1 "easy": a small library for beginners.
//
// It sets the console up in one simple, fixed way:
//   - layer 0: text (8 x 8 font, 40 columns x 30 rows in 320 x 240), in front of everything;
//   - layer 1: the map (16 x 16 tiles), behind the sprites;
//   - 64 sprites of 16 x 16, 32 x 32 or 64 x 64 pixels, drawn from the sprite sheets;
//   - 8 text inks, and 8 colour palettes (0-7) for the sheets and the maps
//     (hardware palettes 0-7 and 8-15; the numbers 8-15 are also accepted for the colour palettes).
// The hardware registers (monocore.h) can still be used directly alongside.
#pragma once
#include "monocore.h"

// ---------------------------------------------------------------- files of project.txt
// build.py turns each file into one name, the file name without its extension:
//   sprites = hero.png      -> hero       number of the first picture of the sheet: sprite(0, x, y, hero + 2)
//   maps    = land.png      -> land       a map, for load_map(land)
//   sounds  = boom.wav      -> boom       a sound, for play_sample(0, boom, 200)
// These names are declared by build.py: there is nothing to write before using them.
// init() loads all the sheets and the first map, each with its own colour palette, in the order of
// project.txt: first the sheets, then the maps (8 at most in all).
typedef struct { const uint8_t *chr, *pal, *map; uint32_t width, height, palette; } map_t;   // size in tiles
typedef struct { const int8_t *data; uint32_t count, rate; } sound_t;

// ---------------------------------------------------------------- start-up and time
void init(void);                     // call once, at the start of the program
void wait_frame(void);               // waits for the next frame (60 per second)
extern volatile uint32_t frame_count;  // frames since start-up
int  random(int n);                  // random number from 0 to n - 1

// ---------------------------------------------------------------- colours
// r, g, b from 0 to 255
void background(int r, int g, int b);                // screen background colour
void ink(int n, int r, int g, int b);                // colour of text ink n (0-7)
void load_palette(int n, const uint8_t *pal);        // colour palette n (0-7) from hero_pal, land_pal...

// ---------------------------------------------------------------- text
// columns 0-39, rows 0-29 (in 320 x 240)
void cls(void);                                      // clears all text
void print(int col, int row, const char *text);      // in ink 0 (white)
void print_ink(int col, int row, const char *text, int ink);
void print_num(int col, int row, int value, int digits, int ink);

// ---------------------------------------------------------------- joypads
#define UP     IN_UP
#define DOWN   IN_DOWN
#define LEFT   IN_LEFT
#define RIGHT  IN_RIGHT
#define B1     IN_B(1)
#define B2     IN_B(2)
#define B3     IN_B(3)
#define B4     IN_B(4)
#define B5     IN_B(5)
#define B6     IN_B(6)
#define START  IN_START
#define COIN   IN_COIN
int held(int player, uint32_t button);      // the button is down (player 1 to 4)
int pressed(int player, uint32_t button);   // the button has just gone down (once per press)

// ---------------------------------------------------------------- sprites
// A sprite sheet is an image 16, 32 or 64 pixels wide: square pictures one under the other, all
// the size of the width. The pictures of all the sheets are numbered one after the other; the name
// of a sheet is the number of its first picture.
void sprite(int n, int x, int y, int picture);       // shows sprite n (0-63) with a picture
void hide_sprite(int n);
void flip_sprite(int n, int horizontal, int vertical);
void scale_sprite(int n, int percent);               // 100 = normal size, 200 = double
void sprite_palette(int n, int palette);             // palette (0-7) of sprite n; -1 = its sheet's
int  collide(int a, int b);                          // sprites a and b touch

// ---------------------------------------------------------------- map
// Image cut into 16 x 16 tiles, at most 64 x 64 tiles (1024 x 1024 pixels).
void load_map(map_t map);                            // changes the map (init() shows the first one)
void scroll_map(int x, int y);                       // map position of the screen's top-left corner

// ---------------------------------------------------------------- sound
// channels 0-7, volume 0-255, frequency in hertz
void tone(int channel, int frequency, int volume);   // square wave
void noise(int channel, int volume);                 // noise (explosions, drums)
void quiet(int channel);
// A tune: notes separated by spaces, one per beat.
//   C D E F G A B, followed if needed by # (sharp) and the octave digit (4 by default);
//   - holds the previous note one more beat; . is a rest.
//   example: "C D E C C D E C E F G - E F G -"
// frames_per_beat: 8 = fast, 15 = moderate, 30 = slow. It plays by itself, in a loop.
void play(int channel, const char *tune, int frames_per_beat, int volume);
void stop(int channel);
// A sound of project.txt, played once on voice 0-7.
void play_sample(int voice, sound_t sound, int volume);
