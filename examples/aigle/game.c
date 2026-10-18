// L'Aigle d'Or (Loriciels 1985, Louis-Marie Rocques), MO5 version: the main BASIC program
// translated by hand into C. Line numbers of the original are given as labels and comments.
// The machine code it calls (EXEC) is the original 6809 code translated by recomp6809.py.
#include "mo5rt.h"
#include "game.h"

// addresses defined at lines 800 and 805
enum { P = 0x5000, SC = 0x58F7, FL = 0x5A0C, EF = 0x5A28, MT = 0x5900, PT = 0x5946, XM = 0x5986,
       XP = 0x59CC, RO = 0x500D, RB = 0x500C, CL = 0x5022, MF = 0x5023, FO = 0x5021, IM = 0x5013,
       LG = 0x5016, LR = 0x5017, NS = 0x501E, EL = 0x5AD9, MS = 0x5A4D, PS = 0x5A96, SA = 0x42C0,
       TP = 0x4390, EN = 0x4399, BO = 0x43A2, IO = 0x43B4 };

static int W, E, V, R, N, L, F, C, H, I, Dr, Xr;   // BASIC variables (DEFINT A-Z); D and X renamed

static inline int PEEK(int a) { return mem[(uint16_t)a]; }
static inline void POKE(int a, int v) { WR((uint16_t)a, (uint8_t)v); }
static inline void EXEC(int a) { cpu_exec((uint16_t)a); }
static void PSET(int x, int y, int c) { mo5_pset(x, y, c); }
static void PSETF(int x, int y) { mo5_pset(x, y, mo5_fg()); }          // PSET(x,y) in the forme colour
static void LINE(int x0, int y0, int x1, int y1) { mo5_line(x0, y0, x1, y1, mo5_fg()); }
static void LINETO(int x1, int y1) { mo5_line(mo5_gx, mo5_gy, x1, y1, mo5_fg()); }
static void BOX(int x0, int y0, int x1, int y1) { mo5_box(x0, y0, x1, y1, mo5_fg()); }
static void PRINT(const char *s) { mo5_puts(s); }
static void PRINTLN(const char *s) { mo5_puts(s); mo5_putc('\r'); mo5_putc('\n'); }
static void SCREEN(int f, int b) { mo5_screen(f, b); }

// ---------------------------------------------------------------- small subroutines (1-30)
static void gosub1(void) { if (PEEK(SA + 2 + W) == 0) POKE(SA + 2 + W, E); }
static void gosub2(void) {
  if (PEEK(SA + W) == 0) POKE(SA + W, E);
  if (PEEK(SA + W + 1) == 0) POKE(SA + 1 + W, E);
}
static void gosub1_full(void) { gosub1(); gosub2(); }            // line 1 falls into 2 and 3
static void gosub4(void) { V = PEEK(SA + W); }
static void gosub5(void) { plat_tick(); mo5_locate(0, 0); EXEC(BO); }
static void gosub10(void) { LINE(0, 168, 64, 88); LINETO(255, 88); LINETO(319, 168); }
static void gosub12(void) { LINE(0, 168, 48, 108); LINETO(271, 108); LINETO(319, 168); }
static void gosub14(void) { LINE(0, 168, 32, 128); LINETO(287, 128); LINETO(319, 168); }
static void gosub26(void) { mo5_play("O5A9L9T3SILASOFAMIREDOO2A1DO"); }
static int  gosub27(const char *keys) { return mo5_key_wait(keys); }   // K$=INPUT$(1)
static void gosub30(void) {
  mo5_console(3, 20); SCREEN(0, 0); mo5_cls(); mo5_color(7, 0); mo5_locate(12, 12);
  PRINT("Il fait noir ici !"); mo5_color(7, 4); mo5_console(0, 2);
}
static void gosub60(void) { mo5_cls(); EXEC(0x43E1); L = 2; }
static void gosub70(void) { mo5_cls(); EXEC(0x43EA); L = 3; }
static void gosub80(void) { mo5_cls(); EXEC(0x43F3); L = 0; }
static void gosub90(void) {
  SCREEN(7, 0); EXEC(0x43FC);
  if (PEEK(P + 186) == 0) return;
  for (;;) { mo5_locate(9, 22); PRINT("Tu es trop fort!"); plat_tick(); }      // line 95
}

// ---------------------------------------------------------------- corridors (110-188)
static void gosub110(void) {
  gosub10(); POKE(IM, 4);
  for (V = 0; V <= 4; V++) { PSET(56, 18 + V * 14, -1); POKE(RO, 1); EXEC(PT); }
  for (V = 0; V <= 4; V++) { PSET(240, 18 + V * 14, -1); POKE(RO, 1); EXEC(XP); }
  F = 0;
}
static void gosub120(void) {
  gosub12(); POKE(IM, 4);
  for (V = 0; V <= 5; V++) { PSET(40, 24 + V * 14, -1); POKE(RO, 1); EXEC(PT); }
  for (V = 0; V <= 5; V++) { PSET(256, 24 + V * 14, -1); POKE(RO, 1); EXEC(XP); }
  F = 1;
}
static void gosub130(void) {
  gosub10(); PSETF(64, 83); LINETO(64, 37); PSETF(77, 37); LINETO(77, 83); PSETF(253, 83); LINETO(253, 37);
  PSETF(240, 37); LINETO(240, 83); POKE(IM, 2); PSET(56, 24, -1); EXEC(PT); PSET(232, 24, -1); EXEC(PT);
  POKE(IM, 3); PSET(56, 84, -1); EXEC(PT); PSET(232, 84, -1); EXEC(PT);
  F = 0;
}
static void gosub140(void) {
  LINE(0, 168, 32, 128); LINETO(72, 98); LINETO(120, 88); LINETO(200, 88); LINETO(248, 98);
  LINETO(287, 128); LINETO(319, 168);
  POKE(SA + 24, 255); POKE(SA + 35, 255); F = 0;
}
static void gosub150(void) {
  gosub14(); LINE(32, 128, 32, 68); LINETO(24, 34); LINETO(16, 24); LINETO(8, 54); LINETO(0, 108); LINETO(0, 168);
  LINE(287, 128, 287, 68); LINETO(295, 34); LINETO(303, 24); LINETO(311, 54); LINETO(319, 108); LINETO(319, 168);
  F = 2;
}
static void gosub160(void) {
  gosub14();
  for (V = 0; V <= 5; V++) {
    PSET(32 + 48 * V, 127, -1); POKE(IM, 32); EXEC(PT);
    LINE(35 + 48 * V, 126, 35 + 48 * V, 30); LINE(44 + 48 * V, 126, 44 + 48 * V, 30);
    PSETF(32 + 48 * V, 24); POKE(IM, 33); EXEC(PT);
  }
  F = 2;
}
static void gosub180(void) {
  gosub14(); mo5_line(207, 128, 120, 128, -1); LINETO(128, 108); LINETO(199, 108); LINETO(207, 128);
  LINE(120, 128, 120, 24); LINE(128, 108, 128, 24); LINE(199, 108, 199, 24); LINE(207, 128, 207, 24);
  LINE(32, 128, 32, 24); LINE(287, 128, 287, 24);
  for (W = 48; W <= 51; W++) POKE(SA + W, 0);
  F = 1;
}

// ---------------------------------------------------------------- doors (210-290)
static void gosub230(void) { POKE(RB, 1); POKE(IM, 5); PSET(142, 24 + F * 20, -1); EXEC(PT); }
static void gosub210(void) { gosub230(); POKE(IM, 7); PSETF(150, 32 + F * 20); EXEC(PT); }
static void gosub220(void) { gosub230(); POKE(IM, 8); PSETF(150, 34 + F * 20); EXEC(PT); }
static void gosub260(void) { POKE(RB, 1); POKE(IM, 6); PSET(0, 76, -1); EXEC(PT); }
static void gosub240(void) { gosub260(); POKE(IM, 9); PSETF(8, 88); EXEC(PT); }
static void gosub250(void) { gosub260(); POKE(IM, 10); PSETF(8, 94); EXEC(PT); }
static void gosub290(void) { POKE(RB, 1); POKE(IM, 6); PSET(288, 76, -1); EXEC(XP); }
static void gosub270(void) { gosub290(); POKE(IM, 9); PSETF(296, 88); EXEC(XP); }
static void gosub280(void) { gosub290(); POKE(IM, 10); PSETF(296, 94); EXEC(XP); }

// ---------------------------------------------------------------- furniture (310-450)
static void gosub310(void) { POKE(RB, 1); POKE(IM, 12); PSET(142, 44 + F * 20, -1); EXEC(PT); }
static void gosub320(void) {
  gosub310(); BOX(143, 74 + F * 20, 144, 75 + F * 20); W = 20 * F + 7; POKE(SA + W + 3, E); POKE(W + SA, 0xF5);
}
static void gosub330(void) { C = 1; }
static void gosub332(void) { POKE(IM, 18); PSET(248, 86, -1); EXEC(MT); W = 55; gosub1_full(); POKE(SA + 35, 255); }
static void gosub340(void) { POKE(RB, 1); POKE(IM, 16); PSETF(208 + 16 * F, 67 + F * 20); EXEC(PT); W = 34 + 20 * F; gosub1_full(); }
static void gosub350(void) { POKE(IM, 15); PSET(200, 62 + F * 20, -1); EXEC(PT); W = 32 + 20 * F; gosub1_full(); }
static void gosub360(void) { POKE(IM, 14); PSETF(88, 26 + F * 20); EXEC(PT); }
static void gosub370(void) { POKE(IM, 13); PSETF(210, 26 + F * 20); EXEC(PT); }
static void gosub380(void) { C = 2; }
static void gosub382(void) { POKE(IM, 18); PSET(40, 86, -1); EXEC(XM); W = 42; gosub1_full(); POKE(SA + 24, 255); }
static void gosub390(void) { C = 3; }
static void gosub392(void) { POKE(IM, 19); PSET(248, 75, -1); EXEC(MT); W = 55; gosub1_full(); POKE(SA + 35, 255); }
static void gosub400(void) { C = 4; }
static void gosub402(void) { POKE(IM, 19); PSET(40, 75, -1); EXEC(XM); W = 42; gosub1_full(); POKE(SA + 24, 255); }
static void gosub410(void) { POKE(IM, 38); PSETF(96 - 16 * F, 26 + F * 10); EXEC(PT); }
static void gosub420(void) { H = 1; }
static void gosub422(void) { POKE(IM, 35); PSET(110 - 16 * F, 46 + F * 20, -1); EXEC(MT); }
static void gosub430(void) {
  POKE(IM, 3); PSET(200 + 8 * F, 92 + F * 20, -1); EXEC(PT); POKE(IM, 36); PSET(208 + 8 * F, 58 + F * 20, -1);
  EXEC(PT); W = 32 + 20 * F; gosub1_full();
}
static void gosub440(void) { POKE(IM, 34); PSET(158, 46 + F * 20, -1); EXEC(PT); BOX(146, 42 + F * 20, 182, 82 + 20 * F); }
static void gosub450(void) { POKE(IM, 37); PSETF(208 + 8 * F, 56 + F * 20); EXEC(PT); }

// ---------------------------------------------------------------- objects (510-650)
static void gosub510(void) {
  PSET(232, 94, -1); POKE(IM, 17); EXEC(MT); W = 54; E = 0xF4; gosub2(); POKE(SA + 34, 255);
  if (N == 63 && PEEK(P + 209) == 1) { PSET(232, 78, -1); POKE(IM, 23); POKE(CL, 128); EXEC(PT); }
}
static void gosub520(void) { PSET(56, 99 + 20 * F, -1); POKE(IM, 25); EXEC(MS); W = 23 + 20 * F; gosub2(); }
static void gosub530(void) { PSET(88, 100 + 20 * F, -1); POKE(IM, 26); EXEC(MS); W = 25 + 20 * F; gosub2(); }
static void gosub540(void) { PSET(56, 110, -1); POKE(IM, 26); POKE(CL, 128); EXEC(PS); W = 43; gosub2(); }
static void gosub550(void) { PSET(248, 100, -1); POKE(IM, 29); EXEC(MS); W = 35; gosub2(); }
static void gosub560(void) { gosub510(); PSET(232, 80, -1); POKE(IM, 30); EXEC(MS); }
static void gosub570(void) { PSET(72, 90, -1); POKE(IM, 23); POKE(CL, 128); EXEC(PS); W = 24; gosub2(); }
static void gosub580(void) { PSET(248, 94 + 20 * F, -1); POKE(IM, 28); EXEC(MS); W = 35 + 20 * F; gosub2(); }
static void gosub610(void) { PSET(72, 92 + 20 * F, -1); POKE(IM, 27); EXEC(MS); W = 24 + 20 * F; gosub2(); }
static void gosub630(void) { gosub510(); PSET(232, 73, -1); POKE(IM, 24); EXEC(MS); }
static void gosub640(void) { gosub510(); PSET(224, 92, -1); POKE(IM, 22); POKE(RB, 1); EXEC(MS); }
static void gosub650(void) { gosub510(); PSET(232, 78, -1); POKE(IM, 23); EXEC(MS); }

// ---------------------------------------------------------------- skulls and bones (700-770)
static void g752(void) { PSET(V, 140, -1); POKE(IM, 114); EXEC(PT); }
static void g754(void) { PSET(V, 142, -1); POKE(IM, 115); EXEC(PT); }
static void g756(void) { PSET(V + 24, 140, -1); POKE(IM, 114); EXEC(XP); }
static void g758(void) { PSET(V + 24, 150, -1); POKE(IM, 116); EXEC(XP); }
static void gosub700(void) {
  E = 1; Xr = 0;
  for (;;) {                                                                  // 730
    W = (int)((mo5_rnd(-N * 2 + Xr) * 11) >> 16) + 64; gosub4();
    if (V > 0) { Xr++; continue; }
    break;
  }
  gosub2(); POKE(P + 220, (W - 60) * 2); V = 16 * (W % 20);                  // 740
  if (PEEK(P + 136) > 0) { PSET(V, 140, -1); POKE(IM, 31); EXEC(PT); return; }   // 770
  if (N == 32) { g754(); g756(); g758(); return; }
  if (PEEK(P + 202) > 0) return;                                              // 745
  switch ((int)((mo5_rnd(-N) * 4) >> 16) + 1) {                                       // 750
  case 1: g752(); break; case 2: g754(); break; case 3: g756(); break; case 4: g758(); break;
  }
}

// ---------------------------------------------------------------- 5000
static void gosub5000(void) {
  E = PEEK(P + 140) > 0 ? 255 : 2;
  if (mo5_rnd(-N * 4) < 32768) { POKE(P + 218, 0); W = 66; } else { POKE(P + 218, 1); W = 73; }
  gosub2();
  if (PEEK(P + 202) > 0) return;
  if (W == 66) { for (V = 0; V <= 4; V++) PSETF(112 - V * 4, 134 + 7 * V); return; }
  PSETF(216, 60); LINETO(216, 99); PSETF(218, 90); LINETO(218, 124);          // 5030
}

typedef void (*sub_t)(void);
static void on_gosub(int k, const sub_t *t, int n) { if (k >= 1 && k <= n) t[k - 1](); }
#define ON_GOSUB(k, ...) do { static const sub_t t_[] = { __VA_ARGS__ }; \
  on_gosub((k), t_, (int)(sizeof t_ / sizeof t_[0])); } while (0)

// ---------------------------------------------------------------- main program (800-4000)
void game_run(void) {
  int k;
run800:
  W = E = V = R = N = L = F = C = H = I = Dr = Xr = 0;
  POKE(P + 192, 99); EXEC(0x43AB);                                            // 807
l810:
  N = 0; mo5_console(0, 24); mo5_cls(); gosub90(); mo5_screen(7, 4); mo5_border(0);
  mo5_locate(5, 5); mo5_putc(20); PRINT("Aventurier, tu peux :");
  mo5_locate(9, 9); PRINT("1. Entrer dans le chateau"); mo5_locate(9, 11); PRINT("2. Aller chez le marchand");
  for (;;) {                                                                  // 820
    k = gosub27("12");
    if (k == '1') goto l900;
    if (k == '2') break;
  }
  mo5_cls(); EXEC(0x43BD); mo5_console(16, 24);                               // 830
l840:
  mo5_cls(); EXEC(0x43C6);
  for (;;) {                                                                  // 845
    k = gosub27("12340");
    mo5_putc((uint8_t)k); mo5_putc('\r'); mo5_putc('\n');
    if (k == '0') goto l810;
    if (k > '0' && k < '5') { POKE(0x50AF, k - '0'); break; }
    goto l840;
  }
  EXEC(0x43CF); mo5_delay_loop(800); goto l840;                               // 850
l900:
  POKE(P + 172, 4); POKE(P + 171, 0); POKE(P + 165, 0); POKE(P + 167, 3); POKE(P + 164, 0);
  POKE(P + 157, 11);                                                          // 980
l1000:
  V = PEEK(P + 157) + 1;
  if (N == 12 && V == 1) goto l810;
  N = V; mo5_cls(); mo5_console(0, 20); mo5_cls(); SCREEN(0, 0); mo5_border(0); mo5_console(21, 24);
  SCREEN(7, 4); mo5_console(0, 20);
  if (PEEK(P + 190) > 0) {
    mo5_locate(9, 11); mo5_color(7, 0); PRINT("C'est un passage secret!"); mo5_delay_loop(4000);
    SCREEN(0, 0); mo5_cls();
  }
  EXEC(TP); C = 0; H = 0; mo5_color(0, 0); POKE(CL, 0); Dr = PEEK(P + 128) + 1;  // 1020
  ON_GOSUB(Dr, gosub110, gosub120, gosub130, gosub140, gosub150, gosub160, gosub150, gosub180);
  if (PEEK(P + 228) == 2) gosub26();
  ON_GOSUB(PEEK(P + 145), gosub210, gosub220, gosub230);                       // 1030
  ON_GOSUB(PEEK(P + 146), gosub240, gosub250, gosub260);
  ON_GOSUB(PEEK(P + 147), gosub270, gosub280, gosub290);
  if (Dr == 8) F = 2;
  if (PEEK(P + 139) > 0) gosub5000();                                         // 1055
  if (PEEK(P + 137) > 0 && PEEK(P + 202) == 0) { PSET(32, 77, -1); POKE(IM, 113); EXEC(PT); }
  E = 254;                                                                    // 1060
  ON_GOSUB(PEEK(P + 142), gosub310, gosub320, gosub330, gosub340, gosub350, gosub360, gosub370, gosub380,
           gosub390, gosub400, gosub410, gosub420, gosub430, gosub440, gosub450);
  POKE(IM, 11);
  if (PEEK(P + 129) == 1) { PSET(16, 57, -1); EXEC(PT); }
  if (PEEK(P + 131) == 1) { PSET(288, 57, -1); EXEC(XP); }                    // 1072
  if (PEEK(P + 135) > 0) gosub700();                                          // 1080
  if (N == 62) { mo5_locate(21, 15); PRINT("*"); }                            // 1085
  if (PEEK(P + 140) > 0) { POKE(P + 0x49, 85 + 2 * PEEK(P + 218)); POKE(P + 0x43, 2); EXEC(0x55D0); }
  if (N == 3 || N == 62) POKE(SA + 50, 3);                                    // 1095
  mo5_console(0, 2); SCREEN(4, 4); mo5_console(3, 20);                        // 1100
  if (PEEK(P + 0x21) > 0) gosub30(); else SCREEN(5, 0);
  if (H == 1) gosub422();                                                     // 1102
  POKE(RB, 1); ON_GOSUB(C, gosub332, gosub382, gosub392, gosub402); POKE(RB, 0);   // 1110
  if (C != 0) goto l1200;
  E = 4; POKE(NS, 3);                                                         // 1120
  ON_GOSUB(PEEK(P + 141), gosub510, gosub520, gosub530, gosub540, gosub550, gosub560, gosub570, gosub580,
           gosub580, gosub580, gosub610, gosub610, gosub630, gosub640, gosub650);
l1200:
  if (PEEK(P + 144) != 0) {
    POKE(IM, 21);                                                             // 1210
    if (mo5_rnd(-N) > 32768) { PSETF(0, 24); EXEC(MT); } else { PSETF(280, 24); EXEC(XM); }
  }
  if (PEEK(P + 143) != 0) {                                                   // 1300
    E = 5; POKE(IM, 20);                                                      // 1310
    if (F == 2 || mo5_rnd(-N) > 32768) { PSET(116, 140, -1); W = 67; gosub1_full(); EXEC(MT); }
    else { PSET(176, 120, -1); EXEC(XM); W = 51; gosub1_full(); }
  }
l1400:
  mo5_color(7, 4); mo5_locate(1, 22); PRINT("Forces: "); mo5_putc(20); mo5_console(0, 2); EXEC(EN);
  for (;;) {
    gosub5();                                                                 // 1420
    if (R == 0) { ON_GOSUB(L, gosub60, gosub70, gosub80); if (PEEK(P + 186) == 0) R = 40; }
    switch (PEEK(P + 175)) {                                                  // 1425
    case 1: goto l1000;
    case 2: goto l1500;
    case 3: case 7: case 8: R = 40; break;                                    // line 9 -> 1430
    case 4:                                                                   // line 50
      mo5_cls(); EXEC(0x43D8); L = 1; if (PEEK(P + 186) == 0) R = 40; break;
    case 5: goto l40;
    case 6: PSETF(152, 62); POKE(RB, 1); POKE(IM, 117); EXEC(MT); break;     // 1700
    case 9: gosub30(); break;                                                 // 4000
    default: break;
    }
    if (PEEK(P + 168) == 1) {                                                 // 1430
      I = 4; mo5_locate(15, 2); mo5_color(7, 1); PRINT("IMPOSSIBLE"); mo5_color(7, 4);
    }
    if (I > 0) { I--; if (I == 1) { mo5_locate(15, 2); PRINT("          "); } }   // 1435
    // 1437: the footstep. On the MO5 this PLAY held the loop up for the whole note, which left a
  // silence between two steps; the note is cut shorter than a pass of the loop to keep that gap.
  if (PEEK(P + 228) > 0) mo5_play_short("O5A200T1SI", 50);
    if (R > 0) { R--; if (R == 1) mo5_cls(); }                                // 1440
  }
l1500:
  POKE(FO, 0); mo5_console(0, 20); mo5_color(0, 0); mo5_cls(); gosub26();
  LINE(96, 167, 110, 145); LINETO(140, 130); LINETO(180, 130); LINETO(210, 145); LINETO(223, 167);   // 1510
  PSETF(0, 24); POKE(LG, 12); POKE(LR, 144); POKE(MF, 170); POKE(CL, 0); EXEC(FL); PSETF(224, 24); EXEC(FL);
  mo5_console(3, 20); SCREEN(5, 0);                                           // 1520
  if (mo5_rnd(-7 * N) > 32768) { PSET(110, 100, -1); POKE(IM, 35); EXEC(MT); }
  if (mo5_rnd(-2 * N) > 39321) { PSET(140, 155, -1); POKE(IM, 20); EXEC(MT); }   // 1530
  mo5_console(0, 2); EXEC(IO); SCREEN(7, 4); mo5_cls(); PRINTLN("");          // 1590
  mo5_locate(7, mo5_row()); PRINT("C'est une oubliette."); PRINTLN("");
  goto l1400;
l40:
  POKE(FO, 0); mo5_console(0, 24); mo5_cls(); SCREEN(7, 0); EXEC(0x4405);
  mo5_locate(6, 22); PRINT("Veux-tu rejouer ? (O/N)");
  while (gosub27("ON") != 'O') {}                                              // 42
  goto run800;
}
