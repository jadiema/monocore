// MONOCORE-1 "easy": a small library for beginners.
//
// It sets the console up in one simple, fixed way:
//   - layer 0: text (8 x 8 font, 40 columns x 30 rows in 320 x 240), in front of everything;
//   - layer 1: the map (16 x 16 tiles), behind the sprites;
//   - 64 sprites of 16 x 16 pixels (or 32 x 32, 64 x 64: see sprite_size), drawn from one sprite sheet;
//   - palettes 0-7: text inks; palettes 8-15: sprites and map.
// The hardware registers (monocore.h) can still be used directly alongside.
#pragma once
#include "monocore.h"

// ---------------------------------------------------------------- start-up and time
void init(void);                     // call once, at the start of the program
void wait_frame(void);               // waits for the next frame (60 per second)
extern volatile uint32_t frame_count;  // frames since start-up
int  random(int n);                  // random number from 0 to n - 1

// ---------------------------------------------------------------- colours
// r, g, b from 0 to 255
void background(int r, int g, int b);                // screen background colour
void ink(int n, int r, int g, int b);                // colour of text ink n (0-7)
void load_palette(int n, const uint8_t *pal);        // palette n (8-15) from a .pal file

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
// The sprite sheet is an image 16 pixels wide: one 16 x 16 picture under the other.
// The first picture is number 0, the next one 1, and so on.
// For bigger sprites, the sheet is 32 or 64 pixels wide (32 x 32 or 64 x 64 pictures) and the
// program calls sprite_size(32) or sprite_size(64) once; all the sprites then have that size.
void load_sprites(const uint8_t *chr, const uint8_t *pal, int palette);
void sprite_size(int size);                          // 16 (default), 32 or 64: width of the sheet
void sprite(int n, int x, int y, int picture);       // shows sprite n (0-63)
void hide_sprite(int n);
void flip_sprite(int n, int horizontal, int vertical);
void scale_sprite(int n, int percent);               // 100 = normal size, 200 = double
void sprite_palette(int n, int palette);             // palette of sprite n (default: the sheet's)
int  collide(int x1, int y1, int x2, int y2);        // two sprites (of the sprite_size) touch

// ---------------------------------------------------------------- map
// Image converted into 16 x 16 tiles: graphics (.chr), palette (.pal), tile map (.map).
// width and height in tiles (at most 64 x 64, i.e. 1024 x 1024 pixels).
void load_map(const uint8_t *chr, const uint8_t *pal, const uint8_t *map, int width, int height, int palette);
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
// An 8-bit sample converted from a WAV file by build.py, played once.
void play_sample(int voice, const int8_t *data, uint32_t count, int rate, int volume);
