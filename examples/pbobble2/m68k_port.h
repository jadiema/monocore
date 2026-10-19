// Memory accesses of the translated program: ROM and work RAM inline, the rest through neort.c.
// Both memories keep 16-bit words little-endian, as p1.bin does (byte access at address ^ 1).
#include <stdint.h>
#include "neort.h"
static inline uint32_t RD16(uint32_t a) {
  a &= 0xFFFFFF;
  if (a < 0x100000) return *(const uint16_t *)(neo_rom + a);
  if (a < 0x200000) return *(uint16_t *)(neo_ram + (a & 0xFFFF));
  return neo_rd16(a);
}
static inline uint32_t RD8(uint32_t a) {
  a &= 0xFFFFFF;
  if (a < 0x100000) return neo_rom[a ^ 1];
  if (a < 0x200000) return neo_ram[(a & 0xFFFF) ^ 1];
  return neo_rd8(a);
}
static inline uint32_t RD32(uint32_t a) { return (RD16(a) << 16) | RD16(a + 2); }
static inline void WR16(uint32_t a, uint32_t v) {
  a &= 0xFFFFFF;
  if ((a & 0xF00000) == 0x100000) { *(uint16_t *)(neo_ram + (a & 0xFFFF)) = v; return; }
  neo_wr16(a, v);
}
static inline void WR8(uint32_t a, uint32_t v) {
  a &= 0xFFFFFF;
  if ((a & 0xF00000) == 0x100000) { neo_ram[(a & 0xFFFF) ^ 1] = v; return; }
  neo_wr8(a, v);
}
static inline void WR32(uint32_t a, uint32_t v) { WR16(a, v >> 16); WR16(a + 2, v); }
