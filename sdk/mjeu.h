// MONOCORE-1 "mjeu": the common parts of a small game, on top of a MONOPIXEL bank (mgfx).
//
// A world is a map of the bank shown on a layer; its tiles say what they are (MG_SOLID,
// MG_PLATFORM, MG_LADDER, MG_WATER, MG_DANGER... set in MONOPIXEL). Objects (the hero, the coins,
// the enemies) are made from the objects placed in the map: each has a position, a speed, a hit box
// and a sprite of the bank. mjeu moves them against the tiles, tells what they touch, follows one
// with the camera and draws them. What each object does is the game's own code, usually chosen by
// the name of its sprite:
//
//     static mg_bank_t bank; static mj_world_t w; static mj_obj_t obj[64];
//     mg_open(&bank, level_file, level_size);
//     mg_load(&bank, 0, 16, MG_LOAD_ALL);
//     mj_world(&w, &bank, 0, 1);                       // map 0 on layer 1
//     int n = mj_from_map(&w, obj, 64);                // one object per object of the map
//     int HEROS = mg_sprite_find(&bank, "HEROS");
//     for (;;) {
//       mj_frame();                                  // mc_frame() and the sound effects
//       for (int i = 0; i < n; i++) {
//         mj_obj_t *o = &obj[i];
//         if (o->sprite == HEROS) { o->vx = ...; if ((o->touch & MJ_GROUND) && jump) o->vy = -70; }
//         mj_move(&w, o);
//       }
//       mj_camera(&w, hero);
//       for (int i = 0; i < n; i++) mj_draw(&w, &obj[i], 1 + i, 4);
//     }
// In project.txt: library = mc, modules = mgfx mjeu. Documentation: MJEU.md; example:
// examples/plateformes (a game made only from its bank: decor, tiles, objects). Random: mc_rand.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "mgfx.h"

#define MJ_FIX 16                     // positions and speeds in 1/16 pixel

typedef struct {
  mg_bank_t *bank;
  int map, layer;
  int w, h;                           // size of the map in pixels
  int tile;                           // 8 or 16
  int cam_x, cam_y;                   // camera: the top left of the screen, in pixels of the map
  int screen_w, screen_h;             // the video mode when mj_world is called
  int gravity, max_fall;              // 1/16 pixel a frame per frame; 1/16 pixel a frame (4, 96)
  bool walls;                         // the left and right edges of the map are walls (true)
} mj_world_t;

typedef struct {
  int32_t x, y;                       // top left of the sprite, 1/16 pixel
  int16_t vx, vy;                     // speed, 1/16 pixel a frame
  int8_t  bx, by;                     // hit box, from the top left of the sprite, pixels
  uint8_t bw, bh;
  uint8_t sprite;                     // sprite of the bank
  uint8_t frame;                      // image shown when MJ_ANIM is not set
  uint8_t flags;                      // MJ_ACTIVE...
  uint8_t touch;                      // what mj_move met: MJ_GROUND, MJ_CEILING, MJ_LEFT, MJ_RIGHT
  uint8_t param;                      // from the editor (the param of the object of the map)
  uint8_t type;                       // free for the game
  int16_t timer, data[3];             // free for the game
} mj_obj_t;

#define MJ_ACTIVE   0x01              // moved and drawn; off, the object is gone
#define MJ_GRAVITY  0x02              // falls
#define MJ_HFLIP    0x04              // drawn mirrored (facing left)
#define MJ_ANIM     0x08              // drawn with the animation of its sprite (else: image .frame)
#define MJ_GHOST    0x10              // goes through the tiles (no collision)
#define MJ_HIDDEN   0x20              // moved but not drawn (blinking...)
#define MJ_DOWN     0x40              // drop through the platforms this frame (down + jump...)

#define MJ_GROUND   0x01
#define MJ_CEILING  0x02
#define MJ_LEFT     0x04
#define MJ_RIGHT    0x08

// the world: shows map m of the bank on a layer (the bank loaded with mg_load before)
int  mj_world(mj_world_t *w, mg_bank_t *b, int map, int layer);
int  mj_attr(const mj_world_t *w, int px, int py);   // attributes at a pixel (sides: MG_SOLID if walls)

// objects
void mj_obj(mj_obj_t *o, const mj_world_t *w, int sprite, int px, int py);   // active, box = the sprite
int  mj_from_map(const mj_world_t *w, mj_obj_t *objs, int max);  // one object per object of the map
int  mj_px(const mj_obj_t *o);                        // position in pixels
int  mj_py(const mj_obj_t *o);
void mj_set_pos(mj_obj_t *o, int px, int py);
void mj_box(mj_obj_t *o, int bx, int by, int bw, int bh);

// moves by the speed (and the gravity) against the tiles: walls stop it, a platform holds it from
// above; sets o->touch. Then: is there a tile with attribute a under its box? Do two boxes meet?
void mj_move(const mj_world_t *w, mj_obj_t *o);
bool mj_touches(const mj_world_t *w, const mj_obj_t *o, int attr);
bool mj_overlap(const mj_obj_t *a, const mj_obj_t *b);
// walks at speed v (1/16 pixel), turning back at walls and, if ledges, at the edge of a drop
void mj_patrol(const mj_world_t *w, mj_obj_t *o, int v, bool ledges);

// camera and drawing
void mj_camera(mj_world_t *w, const mj_obj_t *o);     // follows o (a dead zone in the middle)
void mj_camera_at(mj_world_t *w, int x, int y);       // puts the top left of the screen there
void mj_draw(const mj_world_t *w, const mj_obj_t *o, int hw, int prio);   // hardware sprite hw
void mj_draw_all(const mj_world_t *w, const mj_obj_t *objs, int n, int first_hw, int prio);

// sound effects on the PSG: a tone gliding from f0 to f1 Hz over a number of frames, fading out
// (wave PSG_SQUARE, PSG_TRIANGLE, PSG_SAW, PSG_NOISE; volume 0-255); choose channels the music does
// not use. mj_frame() = mc_frame() plus the steps of the effects, these and those of the msfx module
// (MONOSFX banks) when the game has it: call it instead of mc_frame().
void mj_sfx(int ch, int wave, int f0, int f1, int frames, int vol);
void mj_frame(void);

// a small save in the NVRAM (high scores, progress): data of n bytes (up to 4000), checked
bool mj_load(void *data, uint32_t n);                 // false: nothing saved yet (data untouched)
void mj_save(const void *data, uint32_t n);
