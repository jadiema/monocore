// GRAVITON: the levels. Screen 320 x 240, playfield from line GV_TOP. Angles in tenths of a
// degree: 0 = right, 900 = down, -900 = up. Levels 1-12: aim only, fixed pods; from 13 on, the
// player also places pods. Checked with tools/solve.c (each level: no straight shot, and a solution).
#pragma once
#include "physics.h"

#define N(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))
#define NONE_P ((const gv_pod *)0), 0
#define NONE_W ((const gv_wall *)0), 0
#define NONE_R ((const gv_rect *)0), 0
#define LEFT   0, -890, 890                // launcher on the left edge: any angle into the playfield
#define P      GV_POD_POWER

// ----------------------------------------------------------------- aim only
// 1: just aim
// 2: a pod in the way
static const gv_pod a2p[] = { { 160, 120, 1, P } };
// 3: over the wall
static const gv_wall a3w[] = { { 150, 110, 150, 239, W_SOLID } };
static const gv_pod a3p[] = { { 155, 125, 1, 224 } };
// 4: the window
static const gv_wall a4w[] = { { 160, 12, 160, 100, W_SOLID }, { 160, 132, 160, 239, W_SOLID } };
static const gv_pod a4p[] = { { 212, 147, 1, 156 }, { 89, 52, 1, 64 } };
// 5: the bounce
static const gv_wall a5w[] = { { 40, 30, 260, 30, W_BOUNCE }, { 200, 120, 200, 239, W_SOLID } };
// 6: two pods
static const gv_wall a6w[] = { { 200, 90, 200, 150, W_SOLID } };
static const gv_pod a6p[] = { { 110, 60, 1, P }, { 110, 180, 1, P } };
// 7: the planet
static const gv_pod a7p[] = { { 160, 126, 1, 200 } };
// 8: the cup
static const gv_wall a8w[] = { { 230, 130, 230, 200, W_SOLID }, { 290, 130, 290, 200, W_SOLID }, { 230, 200, 290, 200, W_SOLID },
                               { 90, 24, 319, 24, W_BOUNCE } };
static const gv_pod a8p[] = { { 150, 150, 1, 80 } };
// 9: the slingshot
static const gv_wall a9w[] = { { 0, 100, 110, 100, W_SOLID } };
static const gv_pod a9p[] = { { 111, 105, 1, 231 } };
// 10: the star field
static const gv_pod a10p[] = { { 90, 60, 1, P }, { 90, 180, 1, P }, { 160, 120, 1, P }, { 230, 60, 1, P }, { 230, 180, 1, P } };
static const gv_wall a10w[] = { { 300, 90, 300, 150, W_SOLID } };
// 11: the bent corridor
static const gv_wall a11w[] = { { 0, 100, 230, 100, W_SOLID } };
static const gv_pod a11p[] = { { 243, 81, 1, 216 }, { 58, 97, 1, 230 } };
// 12: needle's eye
static const gv_wall a12w[] = { { 120, 12, 120, 105, W_SOLID }, { 120, 130, 120, 239, W_SOLID },
                                { 240, 12, 240, 140, W_SOLID }, { 240, 168, 240, 239, W_SOLID } };
static const gv_pod a12p[] = { { 180, 200, 1, 100 } };

// ----------------------------------------------------------------- pods to place
static const gv_wall p13w[] = { { 160, 120, 160, 239, W_SOLID } };
static const gv_wall p14w[] = { { 0, 130, 210, 130, W_SOLID } };
static const gv_wall p15w[] = { { 110, 12, 110, 170, W_SOLID }, { 210, 80, 210, 239, W_SOLID } };
static const gv_rect p15r[] = { { 0, 12, 108, 239 }, { 212, 12, 319, 239 } };
static const gv_wall p16w[] = { { 0, 90, 230, 90, W_SOLID }, { 90, 165, 319, 165, W_SOLID } };
static const gv_wall p17w[] = { { 200, 90, 225, 90, W_SOLID }, { 255, 90, 280, 90, W_SOLID }, { 280, 90, 280, 200, W_SOLID },
                                { 200, 200, 280, 200, W_SOLID }, { 200, 90, 200, 200, W_SOLID } };
static const gv_rect p17r[] = { { 196, 86, 284, 204 } };
static const gv_pod p18p[] = { { 160, 130, 1, 250 } };
static const gv_wall p18w[] = { { 60, 12, 60, 80, W_SOLID }, { 260, 160, 260, 185, W_SOLID }, { 260, 160, 319, 160, W_SOLID } };
static const gv_wall p19w[] = { { 120, 40, 260, 40, W_SOLID }, { 260, 40, 260, 210, W_SOLID }, { 260, 210, 80, 210, W_SOLID },
                                { 80, 210, 80, 40, W_SOLID }, { 80, 125, 215, 125, W_SOLID } };
static const gv_pod p20p[] = { { 160, 70, -1, 90 }, { 160, 190, -1, 90 }, { 240, 130, 1, 160 } };
static const gv_wall p20w[] = { { 100, 100, 100, 160, W_BOUNCE }, { 200, 12, 200, 95, W_SOLID }, { 200, 165, 200, 239, W_SOLID },
                                { 285, 100, 285, 160, W_SOLID }, { 285, 100, 300, 100, W_SOLID }, { 285, 160, 300, 160, W_SOLID } };

static const gv_level gv_levels[] = {
  { "PREMIER TIR",      0, 120, LEFT, 40, 290,  60, 0, 0, NONE_P,      NONE_W,        NONE_R },
  { "UNE ETOILE",       0, 120, LEFT, 40, 300, 120, 0, 0, a2p, N(a2p),  NONE_W,        NONE_R },
  { "PAR-DESSUS",       0, 200, LEFT, 40, 290, 200, 0, 0, a3p, N(a3p),  a3w, N(a3w),   NONE_R },
  { "LA FENETRE",       0, 200, LEFT, 40, 290, 116, 0, 0, a4p, N(a4p),  a4w, N(a4w),   NONE_R },
  { "REBOND",           0, 200, LEFT, 44, 260, 180, 0, 0, NONE_P,      a5w, N(a5w),   NONE_R },
  { "DEUX ETOILES",     0, 120, LEFT, 40, 300, 120, 0, 0, a6p, N(a6p),  a6w, N(a6w),   NONE_R },
  { "L'ASTRE",          0, 126, LEFT, 44, 300, 126, 0, 0, a7p, N(a7p),  NONE_W,        NONE_R },
  { "LA COUPE",         0, 200, LEFT, 40, 260, 160, 0, 0, a8p, N(a8p),  a8w, N(a8w),   NONE_R },
  { "LA FRONDE",        0, 170, LEFT, 40,  40,  50, 0, 0, a9p, N(a9p),  a9w, N(a9w),   NONE_R },
  { "CHAMP D'ETOILES",  0, 120, LEFT, 40, 285, 120, 0, 0, a10p, N(a10p), a10w, N(a10w), NONE_R },
  { "LE COUDE",         0,  50, LEFT, 40,  40, 180, 0, 0, a11p, N(a11p), a11w, N(a11w), NONE_R },
  { "LE CHAS",          0, 117, LEFT, 40, 300, 170, 0, 0, a12p, N(a12p), a12w, N(a12w), NONE_R },
  { "A VOUS DE POSER",  0, 200, LEFT, 40, 290, 200, 1, 0, NONE_P,      p13w, N(p13w), NONE_R },
  { "DEMI-TOUR",        0, 200, LEFT, 40,  40,  60, 1, 1, NONE_P,      p14w, N(p14w), NONE_R },
  { "ZONE INTERDITE",   0, 200, LEFT, 40, 290, 200, 2, 1, NONE_P,      p15w, N(p15w), p15r, N(p15r) },
  { "LE COULOIR",       0,  50, LEFT, 40,  20, 205, 2, 1, NONE_P,      p16w, N(p16w), NONE_R },
  { "LE COFFRE",        0, 180, LEFT, 40, 240, 160, 2, 1, NONE_P,      p17w, N(p17w), p17r, N(p17r) },
  { "TROU NOIR",        0, 200, LEFT, 52, 300, 200, 1, 1, p18p, N(p18p), p18w, N(p18w), NONE_R },
  { "LE LACET",       100,  12, 900, 10, 1790, 36, 150, 170, 3, 1, NONE_P, p19w, N(p19w), NONE_R },
  { "GRAND FINAL",      0, 130, LEFT, 44, 300, 130, 2, 2, p20p, N(p20p), p20w, N(p20w), NONE_R },
};
#define GV_LEVELS ((int)(sizeof(gv_levels) / sizeof(gv_levels[0])))
