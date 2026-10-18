// L'Aigle d'Or: the title sequence (the loader program of the cassette version) translated into C.
// It uses the drawing engine at $5000 (the same code as the game's, already translated) with the
// pictures of DES.BIN at $9204. The page of commands describes the pad instead of the keyboard.
#include "mo5rt.h"
#include "game.h"

enum { IM = 0x5013, MT = 0x5900, EF = 0x5A28, FL = 0x5A0C, LG = 0x5016, LR = 0x5017, CL = 0x5022,
       FM = 0x5023, CG = 0x5025 };

static void POKE(int a, int v) { WR((uint16_t)a, (uint8_t)v); }
static void EXEC(int a) { cpu_exec((uint16_t)a); }
static void PRINTLN(const char *s) { mo5_puts(s); mo5_putc('\r'); mo5_putc('\n'); }
static void gosub20(void) { mo5_delay_loop(700); }
static void gosub30(const char *t) { mo5_play(t); mo5_play("T5A2L50DO#L30FAL90RE"); }
static void gosub40(void) { gosub30("O3"); gosub30("O2"); gosub30("O1"); gosub20(); }
static void gosub200(void) {          // frame of the picture and the two halves of the title
  mo5_pset(0, 0, -1); POKE(CL, 4); POKE(LG, 40); POKE(LR, 88); POKE(FM, 0); EXEC(FL);
  mo5_pset(200, 5, -5); POKE(IM, 2); EXEC(MT); mo5_pset(216, 6, -5); POKE(IM, 6); EXEC(MT);
}
static void gosub300(void) {          // the eagle
  mo5_pset(80, 0, -5); POKE(IM, 3); POKE(CG, 4);
  for (int x = 1; x <= 4; x++) { EXEC(MT); EXEC(EF); }
}
static void gosub400(void) {
  mo5_pset(160, 90, -1); POKE(IM, 5); EXEC(MT); mo5_pset(16, 100, -1); POKE(IM, 4); EXEC(MT);
  POKE(IM, 7); mo5_pset(80, 100, -1); EXEC(MT);
  mo5_locate(8, 21); mo5_puts("par ROCQUES Louis-Marie");
}

void intro_run(void) {
  mo5_console(0, 24); mo5_cls(); mo5_screen(5, 0); mo5_border(0); mo5_putc(20);   // 100
  gosub40(); gosub20(); gosub20();
  POKE(0x5021, 0); POKE(0x5012, 0);                                                // 110
  gosub200(); gosub300(); gosub20(); gosub300(); gosub400(); gosub20(); gosub300(); // 120
  mo5_locate(0, 24); mo5_color(7, 0); mo5_puts("    Copyright LORICIELS Fevrier 85");
  gosub20(); gosub300(); gosub20(); gosub40(); gosub20();

  mo5_cls(); mo5_screen(7, 4); mo5_border(4);                                      // 130
  for (int i = 0; i < 4; i++) PRINTLN("");
  PRINTLN("   Apr\x16" "Aes plusieurs jours de cheval,vous arrivez en vue du Chateau de l'Aigle,   situ\x16"
          "Be aux confins de la Wesphalie."); PRINTLN("");
  PRINTLN("   Le vieux mage disait donc vrai..."); PRINTLN("");
  PRINTLN("   Il avait ajout\x16" "Be : 'Pour mener \x16" "Aa bien ta quete, il te faudra acquerir "
          "SAVOIR, RICHESSE et PUISSANCE.'");                                       // 132
  for (int i = 0; i < 4; i++) PRINTLN("");                                         // 140
  PRINTLN("   La l\x16" "Begende du pays raconte que le    Chateau recelle d'innombrables tr\x16"
          "Besors  et richesses, mais que nombreux furent  ceux qui y p\x16" "Berirent...");
  mo5_locate(8, 24); mo5_puts("<APPUYEZ SUR UN BOUTON>"); mo5_key_wait(""); mo5_cls();   // 145

  mo5_color(7, 1); mo5_locate(15, 0); mo5_puts(" COMMANDES "); mo5_color(7, 4);     // 150
  PRINTLN(""); PRINTLN("");
  PRINTLN("Croix : Marcher/Courir (Pressez 2 fois)");
  PRINTLN("START : Arret/Debout");
  PRINTLN("B5 : Accroupir"); PRINTLN("COIN+B1 : Boire");
  PRINTLN("B3 : Sauter (en longueur)"); PRINTLN("B4 : Sauter (en hauteur)");
  PRINTLN("B2 : Ouvrir"); PRINTLN("B1 : Prendre"); PRINTLN("COIN+B2 : Lire");        // 160
  PRINTLN("B6 : Grimper"); PRINTLN("COIN+B3 : Inventaire"); PRINTLN("COIN+B4 : Suicide");
  mo5_color(7, 1); mo5_locate(13, 15); mo5_puts(" OBJET EN MAIN "); mo5_color(7, 4);
  mo5_locate(0, 16); PRINTLN("(COIN + gauche ou droite pour changer)");
  PRINTLN("0 : Vide"); PRINTLN("1 : Torche"); PRINTLN("2 : Crucifix");               // 165
  PRINTLN("3 : Pied de biche"); PRINTLN("4 : Clef en fer"); PRINTLN("5 : Clef en or");
  mo5_puts("6 : Fiole");
  mo5_locate(8, 24); mo5_puts("<APPUYEZ SUR UN BOUTON>"); mo5_key_wait("");

  mo5_cls(); mo5_screen(5, 0); mo5_border(0); gosub200(); gosub400(); gosub40();     // 170
  mo5_locate(8, 24); mo5_color(7, 0); mo5_puts("Chargement en cours...");
}
