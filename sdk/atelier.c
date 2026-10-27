// MONOCORE-1 "atelier": several programs in one ROM (see atelier.h and ATELIER.md).
#include <string.h>
#include "mc.h"
#include "atelier.h"

typedef struct { uint32_t base; char title[16]; uint32_t reserved; } at_entry_t;
static const volatile uint8_t *table(void) { return (const volatile uint8_t *)(uintptr_t)AT_TABLE; }
static uint32_t rd(uint32_t o) { const volatile uint8_t *t = table(); return t[o] | (t[o + 1] << 8) | (t[o + 2] << 16) | ((uint32_t)t[o + 3] << 24); }

bool at_present(void) {
  // the header of this ROM (always the current one) must say so: the memory after a shorter ROM may
  // still hold the table of an atelier loaded before
  static volatile uintptr_t rom0 = 0;     // (the ROM starts at address 0: read through a variable)
  const volatile uint8_t *h = (const volatile uint8_t *)rom0;
  uint32_t size = h[12] | (h[13] << 8) | (h[14] << 16) | ((uint32_t)h[15] << 24);
  static const char tag[] = "MONOATELIER";
  for (int i = 0; tag[i]; i++) if (h[0x20 + i] != (uint8_t)tag[i]) return false;
  if (size < AT_TABLE + 8) return false;
  const volatile uint8_t *t = table();
  return t[0] == 'M' && t[1] == 'A' && t[2] == 'T' && t[3] == 'L' && rd(4) >= 1 && rd(4) <= AT_PROGRAMS;
}
int at_count(void) { return at_present() ? (int)rd(4) : 0; }
const char *at_title(int i) {
  static char s[17];
  memset(s, 0, sizeof s);
  if (i < 0 || i >= at_count()) return s;
  for (int k = 0; k < 16; k++) s[k] = (char)table()[8 + i * 24 + 4 + k];
  return s;
}
int at_find(const char *title) {
  for (int i = 0; i < at_count(); i++) if (!strcmp(at_title(i), title)) return i;
  return -1;
}
void at_run(int i) {
  if (i < 0 || i >= at_count()) return;
  uint32_t base = rd(8 + i * 24);
  mc_display(0);
  __asm__ volatile("csrci mstatus, 8");   // no interrupt while the next program starts
  ((void (*)(void))(uintptr_t)(base + 0x100))();   // its start-up code clears the WRAM and the video
  for (;;) {}
}
void at_exit(void) { at_run(0); }
