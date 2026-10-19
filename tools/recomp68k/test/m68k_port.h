// Memory of the test bench (flat 24-bit space), used by the translated code.
#include <stdint.h>
extern uint8_t tmem[0x1000000];
static inline uint32_t RD8(uint32_t a) { return tmem[a & 0xFFFFFF]; }
static inline uint32_t RD16(uint32_t a) { a &= 0xFFFFFF; return (tmem[a] << 8) | tmem[(a + 1) & 0xFFFFFF]; }
static inline uint32_t RD32(uint32_t a) { return (RD16(a) << 16) | RD16(a + 2); }
static inline void WR8(uint32_t a, uint32_t v) { tmem[a & 0xFFFFFF] = v; }
static inline void WR16(uint32_t a, uint32_t v) { a &= 0xFFFFFF; tmem[a] = v >> 8; tmem[(a + 1) & 0xFFFFFF] = v; }
static inline void WR32(uint32_t a, uint32_t v) { WR16(a, v >> 16); WR16(a + 2, v); }
