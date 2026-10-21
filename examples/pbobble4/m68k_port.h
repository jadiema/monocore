// Memory accesses of the translated program: ROM and work RAM inline, the rest through neort.c.
// Both memories keep 16-bit words little-endian, as p1.bin does (byte access at address ^ 1).
#include <stdint.h>
#include "neort.h"
#define M68K_IDLE(c) neo_idle()           // the wait loops given to the translator with --idle
#ifdef NEO_READ_TRACE                      // debug (pc/bench.c): last RAM address read
extern uint32_t neo_last_read;
#define READ_TRACE(a) (neo_last_read = (a))
#else
#define READ_TRACE(a)
#endif
static inline uint32_t RD16(uint32_t a) {
  READ_TRACE(a);
  a &= 0xFFFFFF;
  if (a < 0x100000) return *(const uint16_t *)(neo_rom + a);
  if (a < 0x200000) return *(uint16_t *)(neo_ram + (a & 0xFFFF));
  return neo_rd16(a);
}
static inline uint32_t RD8(uint32_t a) {
  READ_TRACE(a);
  a &= 0xFFFFFF;
  if (a < 0x100000) return neo_rom[a ^ 1];
  if (a < 0x200000) return neo_ram[(a & 0xFFFF) ^ 1];
  return neo_rd8(a);
}
static inline uint32_t RD32(uint32_t a) { return (RD16(a) << 16) | RD16(a + 2); }
#ifdef NEO_WATCH                           // debug (pc/bench.c): writes to watched words of RAM
void neo_watch(uint32_t a, uint32_t v);
#define WATCH(a, v) neo_watch(a, v)
#else
#define WATCH(a, v)
#endif
#ifdef NEO_RAM_OWNER                       // debug (pc/bench.c): which task writes each byte of RAM
void neo_ram_owner(uint32_t a);
#define RAM_OWNER(a) neo_ram_owner(a)
#else
#define RAM_OWNER(a)
#endif
static inline void WR16(uint32_t a, uint32_t v) {
  RAM_OWNER(a); WATCH(a, v);
  a &= 0xFFFFFF;
  if ((a & 0xF00000) == 0x100000) { *(uint16_t *)(neo_ram + (a & 0xFFFF)) = v; return; }
  neo_wr16(a, v);
}
static inline void WR8(uint32_t a, uint32_t v) {
  RAM_OWNER(a);
  a &= 0xFFFFFF;
  if ((a & 0xF00000) == 0x100000) { neo_ram[(a & 0xFFFF) ^ 1] = v; return; }
  neo_wr8(a, v);
}
static inline void WR32(uint32_t a, uint32_t v) { WR16(a, v >> 16); WR16(a + 2, v); }
