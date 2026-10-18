// PC test bench: plays the translated game with a scripted keyboard and saves screenshots.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../mo5rt.h"
#include "../game.h"
extern const uint8_t aigle_image[0x7F00];
extern const uint8_t intro_bin[0xC01], intro_des[0xDFC];
static long ticks; static const char *script; static int shots;
static int cur_key = -1;
void plat_byte_changed(unsigned o) { (void)o; }
uint32_t plat_rand(void) { return (uint32_t)rand(); }
void plat_beep(int f, int ms) { (void)f; (void)ms; }
void plat_wait_ms(int ms) { (void)ms; }
void plat_border(int c) { (void)c; }
static void shot(void) {
  char n[32]; sprintf(n, "shot%02d.vram", shots++);
  FILE *f = fopen(n, "wb"); fwrite(vram_forme, 1, 8000, f); fwrite(vram_color, 1, 8000, f); fclose(f);
  printf("tick %ld: %s (room N, P+157=%d)\n", ticks, n, mem[0x5000 + 157]);
}
// script: list of "tick:action" ; actions: k<n> hold MO5 key n, k- release, s shot, m<char> menu answer, q quit
static char menu_answers[64]; static int menu_pos;
static void run_script(void) {
  const char *p = script;
  while (*p) {
    long t = strtol(p, (char **)&p, 10); p++;
    char a = *p++; char arg[16]; int n = 0; while (*p && *p != ' ') arg[n++] = *p++; arg[n] = 0; while (*p == ' ') p++;
    if (t != ticks) continue;
    if (a == 'k') cur_key = arg[0] == '-' ? -1 : atoi(arg);
    else if (a == 's') shot();
    else if (a == 'q') { shot(); exit(0); }
  }
}
int plat_key(void) { return cur_key; }
static int last_room=-1;
void plat_tick(void) { ticks++; if (mem[0x5000+157]!=last_room){last_room=mem[0x5000+157]; printf("tick %ld: salle %d\n",ticks,last_room+1);} run_script(); if (ticks > 100000) exit(1); }
int plat_menu_key(const char *keys) {
  shot();
  if (!keys[0]) return 0;
  char c = menu_answers[menu_pos] ? menu_answers[menu_pos++] : keys[0];
  printf("menu [%s] -> %c\n", keys, c); return c;
}
int main(int argc, char **argv) {
  script = argc > 1 ? argv[1] : "";
  if (argc > 2) strncpy(menu_answers, argv[2], 63);
  S = 0xAFF0;
  mo5_init();
  if (getenv("INTRO")) {             // INTRO=1 ./game ... : title sequence first
    memcpy(&mem[0x5000], intro_bin, sizeof intro_bin); memcpy(&mem[0x9204], intro_des, sizeof intro_des);
    intro_run();
  }
  memcpy(&mem[0x2100], aigle_image, sizeof aigle_image);
  game_run();
  return 0;
}
void cpu_bad_jump(uint16_t pc) { printf("BAD JUMP %04X\n", pc); exit(2); }
