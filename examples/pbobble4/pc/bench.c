// PC test bench of the port: runs the translated program on neort.c, renders the Neo Geo screen
// (reference renderer, from the converted tiles of prepare.py) and reports untranslated addresses.
//
// usage: bench DIR FRAMES [SCRIPT]
//   DIR     folder with p1.bin, spr.bin, spr_map.bin, fix.bin (examples/pbobble4)
//   SCRIPT  actions "frame:action", separated by spaces:
//           frame:shot=NAME   writes NAME.ppm (320 x 224)
//           frame:p1=HEX      P1 buttons (bits 0 up, 1 down, 2 left, 3 right, 4 A, 5 B, 6 C, 7 D)
//           frame:st=HEX      start/select (bit 0 P1 start, bit 2 P2 start)
//           frame:dump=NAME   writes the video RAM to NAME.vram
// Exit code 2 and a line "UNKNOWN xxxxxx" when the program reaches an untranslated address.
// Time: one frame every TICKS_PER_FRAME calls of M68K_TICK (backward branches and dispatches).
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "neort.h"
#include "m68k_port.h"

#define TICKS_PER_FRAME 200000
static jmp_buf done;
static m68k_t cpu;
static uint32_t ticks, frames_max;
static const char *script;
static uint8_t *spr, *fix;
static uint16_t *spr_map;
static uint32_t spr_count, spr_map_count;
static int exit_code;

static uint8_t *load(const char *dir, const char *name, uint32_t *size) {
  char path[512]; snprintf(path, sizeof path, "%s/%s", dir, name);
  FILE *f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "missing %s (run tools/prepare.py)\n", path); exit(1); }
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  uint8_t *p = malloc(n); if (fread(p, 1, n, f) != (size_t)n) exit(1); fclose(f);
  if (size) *size = n;
  return p;
}

// --- reference renderer ---
static const uint8_t zoom_x[16][16] = {   // pixels kept by the horizontal shrink (LSPC)
  {0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0}, {0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0},
  {0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0}, {0,0,1,0,1,0,0,0,1,0,0,0,1,0,0,0},
  {0,0,1,0,1,0,0,0,1,0,0,0,1,0,1,0}, {0,0,1,0,1,0,1,0,1,0,0,0,1,0,1,0},
  {0,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0}, {1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0},
  {1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0}, {1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,0},
  {1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1}, {1,0,1,1,1,0,1,1,1,1,1,0,1,0,1,1},
  {1,0,1,1,1,0,1,1,1,1,1,0,1,1,1,1}, {1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1},
  {1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1}, {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}};

static void rgb(uint16_t c, uint8_t *o) {
  int r = ((c >> 7) & 0x1E) | ((c >> 14) & 1), g = ((c >> 3) & 0x1E) | ((c >> 13) & 1), b = ((c << 1) & 0x1E) | ((c >> 12) & 1);
  o[0] = (r << 3) | (r >> 2); o[1] = (g << 3) | (g >> 2); o[2] = (b << 3) | (b >> 2);
}

static void render(const char *name) {
  static uint16_t line[384];
  static uint8_t img[224][320][3];
  const uint16_t *pal = neo_pal[neo.pal_bank];    // the bank selected (shown and accessed)
  for (int y = 0; y < 224; y++) {
    int sl = y + 16;                              // LSPC scanline (the screen starts at line 16)
    for (int x = 0; x < 384; x++) line[x] = 0xFFFF;
    int px = 0, py = 0, prows = 0, pzy = 0, pw = 0;
    for (int n = 1; n < 381; n++) {
      uint16_t s2 = neo_vram[0x8000 + n], s3 = neo_vram[0x8200 + n], s4 = neo_vram[0x8400 + n];
      int zx = (s2 >> 8) & 15, zy, rows, sx, sy;
      if (s3 & 0x40) { sx = (px + pw) & 0x1FF; sy = py; rows = prows; zy = pzy; }
      else { sx = s4 >> 7; sy = 0x200 - (s3 >> 7); rows = s3 & 0x3F; zy = s2 & 0xFF; }
      px = sx; py = sy; prows = rows; pzy = zy; pw = zx + 1;
      if (!rows) continue;
      int sline = (sl - sy) & 0x1FF, zline = sline & 0xFF, invert = sline & 0x100;
      if (invert) zline ^= 0xFF;
      if (rows > 0x20) {
        zline %= (zy + 1) << 1;
        if (zline > zy) { zline = ((zy + 1) << 1) - 1 - zline; invert = !invert; }
      } else if (sline >= (rows << 4) || zline > zy) continue;
      int src = zy == 0xFF ? zline : zline * 256 / (zy + 1);    // approximate vertical shrink
      int tile = (src >> 4) & 0x1F, ty = src & 15;
      if (invert) { ty ^= 15; tile ^= 0x1F; }
      uint16_t t0 = neo_vram[n * 64 + tile * 2], t1 = neo_vram[n * 64 + tile * 2 + 1];
      uint32_t num = t0 | ((t1 & 0xF0) << 12);
      if (t1 & 8) num = (num & ~7u) | ((neo.frame >> 3) & 7);
      else if (t1 & 4) num = (num & ~3u) | ((neo.frame >> 3) & 3);
      if (t1 & 2) ty ^= 15;
      if (num >= spr_map_count) continue;
      const uint8_t *row = spr + spr_map[num] * 128 + ty * 8;
      int x = sx >= 0x1E0 ? sx - 0x200 : sx;
      for (int k = 0; k < 16; k++) {
        int kk = (t1 & 1) ? 15 - k : k;
        if (!zoom_x[zx][kk]) continue;
        int p = (row[kk >> 1] >> ((kk & 1) * 4)) & 15;
        if (p && x >= 0 && x < 320) line[x] = (t1 >> 8) * 16 + p;
        x++;
      }
    }
    for (int x = 0; x < 320; x++) {              // fix layer on top
      uint16_t w = neo_vram[0x7000 + (x >> 3) * 32 + (sl >> 3)];
      const uint8_t *t = fix + (w & 0xFFF) * 32 + (sl & 7) * 4;
      int p = (t[(x & 7) >> 1] >> ((x & 1) * 4)) & 15;
      uint16_t c = p ? pal[(w >> 12) * 16 + p] : line[x] != 0xFFFF ? pal[line[x]] : pal[0xFFF];
      rgb(c, img[y][x]);
    }
  }
  char path[256]; snprintf(path, sizeof path, "%s.ppm", name);
  FILE *f = fopen(path, "wb"); fprintf(f, "P6\n320 224\n255\n"); fwrite(img, 1, sizeof img, f); fclose(f);
}

// --- time and script ---
static void actions(uint32_t frame) {
  for (const char *p = script; p && *p;) {
    while (*p == ' ') p++;
    char *e; uint32_t f = strtoul(p, &e, 10);
    if (*e != ':') break;
    const char *a = e + 1, *end = strchr(a, ' '); if (!end) end = a + strlen(a);
    if (f == frame) {
      if (!strncmp(a, "shot=", 5)) { char n[128]; int l = end - a - 5; snprintf(n, sizeof n, "%.*s", l, a + 5); render(n); }
      else if (!strncmp(a, "dump=", 5)) {        // raw VRAM of this frame: NAME.vram
        char n[128]; int l = end - a - 5; snprintf(n, sizeof n, "%.*s.vram", l, a + 5);
        FILE *f = fopen(n, "wb"); fwrite(neo_vram, 2, 0x8800, f); fclose(f);
      }
      else if (!strncmp(a, "p1=", 3)) neo.p1 = strtoul(a + 3, 0, 16);
      else if (!strncmp(a, "p2=", 3)) neo.p2 = strtoul(a + 3, 0, 16);
      else if (!strncmp(a, "st=", 3)) neo.status_b = strtoul(a + 3, 0, 16);
    }
    p = end;
  }
}

static uint32_t hot[0x88000];          // samples per 68000 word address (ROM and RAM), for the report
void bench_tick(uint32_t pc) {
  if (pc < 0x110000) hot[pc >> 1]++;
  if (++ticks < TICKS_PER_FRAME) return;
  ticks = 0;
  actions(neo.frame + 1);
  if (getenv("BENCH_HASH")) {           // one hash of the work RAM per frame, to compare two builds
    uint32_t h = 2166136261u;
    for (int i = 0; i < 0x10000; i++) h = (h ^ neo_ram[i]) * 16777619u;
    printf("frame %u ram %08X\n", neo.frame, h);
  }
  neo_vblank(&cpu);
  if (neo.frame >= frames_max) longjmp(done, 1);
}
uint32_t neo_platform_line(void) { return ticks * 264 / TICKS_PER_FRAME; }
void neo_platform_wait(uint32_t frame) { while (neo.frame == frame) { ticks = TICKS_PER_FRAME - 1; bench_tick(0xFFFFFFFF); } }
void neo_platform_fix(uint32_t x, uint32_t y, uint32_t entry) { (void)x; (void)y; (void)entry; }
void neo_platform_color(uint32_t index, uint32_t color) { (void)index; (void)color; }
void neo_platform_bank(void) {}
void neo_platform_sound(uint32_t prefix, uint32_t code) { (void)prefix; (void)code; }
void neo_sound_trace(uint32_t command) { if (getenv("BENCH_SOUND")) printf("SND %u %02X\n", neo.frame, command); }
static uint16_t vram_mem[0x8800];

uint32_t m68k_external(m68k_t *c, uint32_t pc) {
  uint32_t r = neo_bios(c, pc);
  if (r) return r;
  printf("UNKNOWN %06X frame %u sp %06X top %08X\n", pc, neo.frame, c->a[7],
         (RD16(c->a[7]) << 16) | RD16(c->a[7] + 2));
  exit_code = 2; longjmp(done, 1);
}
void m68k_stop(m68k_t *c) { while (c->ipl <= (c->srh & 7)) bench_tick(0xFFFFFFFF); }

static void report(void) {
  uint32_t best[8] = {0};
  for (int k = 0; k < 8; k++) {
    uint32_t m = 0;
    for (uint32_t a = 0; a < 0x88000; a++) if (hot[a] > hot[m]) { int seen = 0; for (int j = 0; j < k; j++) seen |= best[j] == a; if (!seen) m = a; }
    best[k] = m;
  }
  printf("hot:"); for (int k = 0; k < 8; k++) printf(" %06X(%u)", best[k] * 2, hot[best[k]]); printf("\n");
  uint32_t nv = 0, np = 0;
  for (int i = 0; i < 0x8800; i++) nv += neo_vram[i] != 0;
  for (int i = 0; i < 0x1000; i++) np += neo_pal[0][i] != 0 || neo_pal[1][i] != 0;
  printf("request %u, mode %02X, user mode %02X, VRAM words used %u, colours %u, palette bank %u, fix %s\n",
         neo.user_request, neo_ram[0xFD80 ^ 1], neo_ram[0xFDAF ^ 1], nv, np, neo.pal_bank, neo.fix_cart ? "cart" : "board");
}

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: bench DIR FRAMES [SCRIPT]\n"); return 1; }
  uint32_t n;
  neo_rom = load(argv[1], "p1.bin", &neo_rom_size);
  spr = load(argv[1], "spr.bin", &spr_count); spr_count /= 128;
  spr_map = (uint16_t *)load(argv[1], "spr_map.bin", &spr_map_count); spr_map_count /= 2;
  fix = load(argv[1], "fix.bin", &n);
  frames_max = atoi(argv[2]); script = argc > 3 ? argv[3] : "";
  neo_vram = vram_mem;
  neo_reset(&cpu);
  if (!setjmp(done)) m68k_run(&cpu);
  if (getenv("BENCH_REPORT")) report();
  if (getenv("BENCH_DUMP")) {            // raw VRAM and palettes, for scripts
    FILE *f = fopen("vram.bin", "wb"); fwrite(neo_vram, 2, 0x8800, f); fclose(f);
    f = fopen("pal.bin", "wb"); fwrite(neo_pal, 2, 0x2000, f); fclose(f);
  }
  printf("frames %u, sound commands %u\n", neo.frame, neo.sound_count);
  return exit_code;
}
