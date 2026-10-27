// GRAVITON: the levels. Screen 320 x 240, playfield from line GV_TOP. Angles: 1024 a turn,
// 0 = right, 256 = down, 512 = left, 768 = up. Checked with tools/solve.c.
#pragma once
#include "physics.h"

#define N(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))
#define NONE_P ((const gv_pod *)0), 0
#define NONE_W ((const gv_wall *)0), 0
#define NONE_R ((const gv_rect *)0), 0

// 1: one pod bends the shot up to the target
// 2: a wall to fly over
static const gv_wall w2[] = { { 160, 100, 160, 239, W_SOLID } };
// 3: a pod that pushes
static const gv_wall w3[] = { { 150, 90, 150, 150, W_SOLID }, { 150, 90, 170, 90, W_SOLID }, { 150, 150, 170, 150, W_SOLID } };
// 4: turn back, above the shelf
static const gv_wall w4[] = { { 0, 130, 210, 130, W_SOLID } };
// 5: bouncing walls
static const gv_wall w5[] = { { 40, 24, 300, 24, W_BOUNCE }, { 210, 100, 210, 239, W_SOLID }, { 250, 100, 319, 100, W_SOLID } };
// 6: forbidden zones
static const gv_wall w6[] = { { 110, 12, 110, 170, W_SOLID }, { 210, 80, 210, 239, W_SOLID } };
static const gv_rect r6[] = { { 0, 12, 108, 239 }, { 212, 12, 319, 239 } };
// 7: a planet in the way
static const gv_pod p7[] = { { 160, 126, 1, 200 } };
// 8: the box, free aim
static const gv_wall w8[] = { { 240, 80, 300, 80, W_SOLID }, { 300, 80, 300, 170, W_SOLID }, { 240, 170, 300, 170, W_SOLID },
                              { 240, 80, 240, 105, W_SOLID } , { 240, 145, 240, 170, W_SOLID } , { 120, 60, 120, 239, W_BOUNCE } };
// 9: slingshot around a planet, back behind the launcher
static const gv_pod p9[] = { { 220, 120, 1, 120 } };
static const gv_wall w9[] = { { 110, 12, 110, 95, W_SOLID }, { 110, 95, 20, 95, W_SOLID } };
// 10: the corridor
static const gv_wall w10[] = { { 0, 90, 230, 90, W_SOLID }, { 90, 165, 320, 165, W_SOLID } };
// 11: the mines
static const gv_pod p11[] = { { 110, 70, -1, 60 }, { 110, 180, -1, 60 }, { 190, 125, -1, 60 }, { 250, 60, -1, 60 }, { 250, 190, -1, 60 } };
// 12: the vault
static const gv_wall w12[] = { { 200, 90, 225, 90, W_SOLID }, { 255, 90, 280, 90, W_SOLID }, { 280, 90, 280, 200, W_SOLID },
                               { 200, 200, 280, 200, W_SOLID }, { 200, 90, 200, 200, W_SOLID } };
// 13: the black hole
static const gv_pod p13[] = { { 160, 130, 1, 250 } };
static const gv_wall w13[] = { { 60, 12, 60, 80, W_SOLID }, { 260, 180, 260, 239, W_SOLID } };
// 14: ping-pong
static const gv_wall w14[] = { { 70, 12, 70, 200, W_BOUNCE }, { 250, 50, 250, 239, W_BOUNCE }, { 140, 145, 180, 145, W_SOLID },
                               { 140, 110, 140, 145, W_SOLID }, { 180, 110, 180, 145, W_SOLID } };
static const gv_rect r14[] = { { 74, 12, 246, 100 } };
// 15: the hairpin
static const gv_wall w15[] = { { 120, 40, 260, 40, W_SOLID }, { 260, 40, 260, 210, W_SOLID }, { 260, 210, 80, 210, W_SOLID },
                               { 80, 210, 80, 40, W_SOLID }, { 80, 125, 215, 125, W_SOLID } };
// 16: the finale
static const gv_pod p16[] = { { 160, 70, -1, 90 }, { 160, 190, -1, 90 }, { 240, 130, 1, 160 } };
static const gv_wall w16[] = { { 100, 100, 100, 160, W_BOUNCE }, { 200, 12, 200, 95, W_SOLID }, { 200, 165, 200, 239, W_SOLID },
                               { 285, 100, 285, 160, W_SOLID }, { 285, 100, 300, 100, W_SOLID }, { 285, 160, 300, 160, W_SOLID } };
static const gv_rect r12[] = { { 196, 86, 284, 204 } };

static const gv_level gv_levels[] = {
  { "PREMIER VIRAGE",  24, 200,   0,   0,   0, 40, 290,  80, 1, 0, NONE_P,     NONE_W,      NONE_R },
  { "LE MUR",          24, 200,   0,   0,   0, 40, 290, 200, 2, 0, NONE_P,     w2, N(w2),   NONE_R },
  { "REPOUSSOIR",      24, 120,   0,   0,   0, 40, 300,  40, 0, 1, NONE_P,     w3, N(w3),   NONE_R },
  { "DEMI-TOUR",       30, 200,   0,   0,   0, 40,  40,  60, 1, 1, NONE_P,     w4, N(w4),   NONE_R },
  { "BILLARD",         30, 220, 840, 840, 840, 44, 280, 180, 1, 0, NONE_P,     w5, N(w5),   NONE_R },
  { "ZONE INTERDITE",  24, 200,   0,   0,   0, 40, 290, 200, 2, 1, NONE_P,     w6, N(w6),   r6, N(r6) },
  { "L'ASTRE",         24, 126,   0,   0,   0, 44, 296, 126, 1, 1, p7, N(p7),  NONE_W,      NONE_R },
  { "CANON LIBRE",     30, 126, 0, 900, 1148, 40, 270, 125, 2, 0, NONE_P,     w8, N(w8),   NONE_R },
  { "FRONDE",          50, 150,   0,   0,   0, 40,  40,  50, 1, 1, p9, N(p9),  w9, N(w9),   NONE_R },
  { "LE COULOIR",      24,  50,   0,   0,   0, 40,  20, 205, 2, 1, NONE_P,     w10, N(w10), NONE_R },
  { "CHAMP DE MINES",  24, 125,   0,   0,   0, 40, 300, 125, 2, 1, p11, N(p11), NONE_W,     NONE_R },
  { "LE COFFRE",       30, 180, 900, 800, 1000, 40, 240, 160, 2, 1, NONE_P,    w12, N(w12), r12, N(r12) },
  { "TROU NOIR",       24, 200,   0,   0,   0, 52, 300,  40, 1, 1, p13, N(p13), w13, N(w13), NONE_R },
  { "PING-PONG",      160, 225, 768, 700, 836, 44, 160, 128, 1, 1, NONE_P,    w14, N(w14), r14, N(r14) },
  { "LE LACET",        100, 22, 256, 256, 256, 36, 150, 170, 3, 1, NONE_P,    w15, N(w15), NONE_R },
  { "GRAND FINAL",     24, 130, 0, 960, 1088, 44, 300, 130, 2, 2, p16, N(p16), w16, N(w16), NONE_R },
};
#define GV_LEVELS ((int)(sizeof(gv_levels) / sizeof(gv_levels[0])))
