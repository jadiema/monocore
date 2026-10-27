// PLATEFORMES: a model of platform game for MONOCORE-1, to copy and change.
//
// Everything that makes the level is in its bank, niveau.mgf, drawn with MONOPIXEL: the decor, what
// the tiles are (solid, platform, water...) and the objects placed in it. This program only says what
// each kind of object does, by the name of its sprite:
//   HEROS    the player: runs, jumps (higher while B1 is held), goes down through the platforms
//            (down + B1); water or a spike: a life lost, back to the start
//   PIECE    a coin: 10 points
//   BLOB     an enemy that walks, turning back at walls and at the edge of a drop (param of the
//            object in the map: its speed); jumped on, it is gone (50 points); touched, a life lost
//   DRAPEAU  the end of the level
// Another level: draw it in MONOPIXEL, export it (tools/mgfx/mgfx.py monopixel.sav 1 -o niveau.mgf)
// and build again. Another kind of object: a sprite with a new name and a case in update().
// The sounds come from bruits.mfx, made with MONOSFX, and are played by name (sound("SAUT")).
// Modules: mgfx (the bank), mjeu (objects, collisions, camera, save: sdk/MJEU.md) and msfx (sounds).
#include <stdio.h>
#include "mc.h"
#include "mgfx.h"
#include "mjeu.h"
#include "msfx.h"

#define MAX_OBJ 64
static mg_bank_t bank;
static ms_bank_t sfx;                            // bruits.mfx, made with MONOSFX
static mj_world_t world;
static mj_obj_t obj[MAX_OBJ];
static int n_obj;
static int HEROS, PIECE, BLOB, DRAPEAU;           // sprite numbers, found by name
static mj_obj_t *hero;
static int start_x, start_y, score, lives, best, hurt_timer;
static struct { uint32_t best; } save;

enum { TITLE, PLAY, WON, OVER };
static int scene, scene_timer;

static void sound(const char *name) { ms_play(&sfx, ms_find(&sfx, name)); }
static void text(int x, int y, const char *s) { mc_text(0, x, y, s, 0); }
static void clear_text(void) { for (int y = 0; y < 30; y++) text(0, y, "                                        "); }

// ---------------------------------------------------------------- the level
static void start_level(void) {
  mj_world(&world, &bank, 0, 1);
  n_obj = mj_from_map(&world, obj, MAX_OBJ);
  hero = 0;
  for (int i = 0; i < n_obj; i++) {
    mj_obj_t *o = &obj[i];
    if (o->sprite == HEROS && !hero) {
      hero = o;
      o->flags |= MJ_GRAVITY;
      mj_box(o, 3, 2, 10, 14);                     // a little smaller than the sprite: fairer
      start_x = mj_px(o); start_y = mj_py(o);
    } else if (o->sprite == BLOB) {
      o->flags |= MJ_GRAVITY;
      mj_box(o, 1, 6, 14, 10);
    } else if (o->sprite == PIECE) mj_box(o, 3, 1, 10, 14);
  }
  hurt_timer = 0;
}
static void lose_life(void) {
  if (--lives <= 0) { scene = OVER; scene_timer = 0; sound("PERDU"); return; }
  sound("AIE");
  mj_set_pos(hero, start_x, start_y);
  hero->vx = hero->vy = 0;
  hurt_timer = 90;                                  // blinks, cannot be hurt again for 1.5 s
}

// ---------------------------------------------------------------- what each object does
static void update_hero(mj_obj_t *o) {
  uint32_t p = mc_pad(1), pr = mc_pad_pressed(1);
  int target = (p & IN_LEFT) ? -28 : (p & IN_RIGHT) ? 28 : 0;
  o->vx += (int16_t)((target - o->vx) / 4);        // a little inertia
  if (target < 0) o->flags |= MJ_HFLIP;
  if (target > 0) o->flags &= ~MJ_HFLIP;
  bool ground = (o->touch & MJ_GROUND) != 0;
  if (ground && (pr & IN_B(1))) {
    if (p & IN_DOWN) o->flags |= MJ_DOWN;           // down + jump: through the platform
    else { o->vy = -76; sound("SAUT"); }
  }
  if (!(p & IN_B(1)) && o->vy < -24) o->vy = -24;  // jump button released: a shorter jump
  mj_move(&world, o);
  // the picture: running when moving on the ground, image 1 in the air, image 0 standing
  if (!(o->touch & MJ_GROUND)) { o->flags &= ~MJ_ANIM; o->frame = 1; }
  else if (o->vx > 4 || o->vx < -4) o->flags |= MJ_ANIM;
  else { o->flags &= ~MJ_ANIM; o->frame = 0; }
  if (hurt_timer) { hurt_timer--; if (hurt_timer & 4) o->flags |= MJ_HIDDEN; else o->flags &= ~MJ_HIDDEN; }
  if ((mj_touches(&world, o, MG_DANGER) || mj_py(o) > world.h) && !hurt_timer) lose_life();
}
static void update(mj_obj_t *o) {
  if (!(o->flags & MJ_ACTIVE)) return;
  if (o == hero) { update_hero(o); return; }
  if (o->sprite == PIECE) {
    if (mj_overlap(hero, o)) { o->flags &= ~MJ_ACTIVE; score += 10; sound("PIECE"); }
  } else if (o->sprite == BLOB) {
    mj_patrol(&world, o, 6 + o->param * 2, true);
    if (mj_overlap(hero, o) && !hurt_timer) {
      bool from_above = hero->vy > 0 && mj_py(hero) + hero->by + hero->bh - 6 < mj_py(o) + o->by;
      if (from_above) { o->flags &= ~MJ_ACTIVE; hero->vy = -56; score += 50; sound("ECRASE"); }
      else lose_life();
    }
  } else if (o->sprite == DRAPEAU) {
    if (mj_overlap(hero, o)) { scene = WON; scene_timer = 0; sound("BRAVO"); }
  }
}

// ---------------------------------------------------------------- scenes
static void hud(void) {
  char s[48];
  snprintf(s, sizeof s, "SCORE %5d   VIES %d   RECORD %5d", score, lives, best);
  text(1, 1, s);
}
int main(void) {
  mc_init();
  mc_video_mode(320, 240);
  if (mg_open(&bank, niveau_file, niveau_size) || !mg_load(&bank, 0, 16, MG_LOAD_ALL)) {
    mc_debug("niveau.mgf : banque illisible ou trop grande\n");
    for (;;) mc_frame();
  }
  ms_open(&sfx, bruits_file, bruits_size);        // a missing sound is simply silent
  HEROS = mg_sprite_find(&bank, "HEROS"); PIECE = mg_sprite_find(&bank, "PIECE");
  BLOB = mg_sprite_find(&bank, "BLOB"); DRAPEAU = mg_sprite_find(&bank, "DRAPEAU");
  mc_text_layer(0, 3);
  mc_color(15 * 16 + 1, MC_RGB(120, 200, 240));    // the sky
  mc_background(15 * 16 + 1);
  mc_display(DISP_ON | DISP_LAYER(0) | DISP_LAYER(1) | DISP_SPRITES);
  if (mj_load(&save, sizeof save)) best = (int)save.best;
  start_level();
  scene = TITLE;
  for (;;) {
    mj_frame();
    scene_timer++;
    uint32_t pr = mc_pad_pressed(1);
    if (scene == PLAY) {
      for (int i = 0; i < n_obj; i++) update(&obj[i]);
      if (hero) mj_camera(&world, hero);
      clear_text(); hud();
    } else {
      clear_text();
      if (scene == TITLE) { text(13, 9, "PLATEFORMES"); text(5, 13, "GAUCHE / DROITE : COURIR"); text(5, 15, "B1 : SAUTER (BAS + B1 : DESCENDRE)"); }
      if (scene == WON) { text(15, 10, "BRAVO !"); hud(); }
      if (scene == OVER) { text(14, 10, "PERDU..."); hud(); }
      if ((scene_timer / 30) & 1) text(10, 19, "APPUYEZ SUR B1");
      if ((pr & IN_B(1)) && scene_timer > 20) {
        if (scene != TITLE && score > best) { best = score; save.best = (uint32_t)best; mj_save(&save, sizeof save); }
        sound("MENU");
        score = 0; lives = 3;
        start_level();
        scene = PLAY; scene_timer = 0;
      }
    }
    mj_draw_all(&world, obj, n_obj, 1, 4);
  }
}
