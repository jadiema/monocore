// Registers, flags and memory access shared by the translated 6809 code and the runtime.
#pragma once
#include <stdint.h>

extern uint8_t mem[0x10000];                 // the MO5 address space (video RAM excepted)
uint8_t vram_read(uint16_t a);               // $0000-$1FFF: forme or couleur, chosen by $A7C0
void    vram_write(uint16_t a, uint8_t v);
void    io_write(uint16_t a, uint8_t v);

static inline uint8_t RD(uint16_t a) { return a < 0x2000 ? vram_read(a) : mem[a]; }
static inline void WR(uint16_t a, uint8_t v) {
  if (a < 0x2000) vram_write(a, v);
  else { mem[a] = v; if ((a & 0xFFC0) == 0xA7C0) io_write(a, v); }
}
static inline uint16_t RD16(uint16_t a) { return (uint16_t)(RD(a) << 8 | RD((uint16_t)(a + 1))); }
static inline void WR16(uint16_t a, uint16_t v) { WR(a, (uint8_t)(v >> 8)); WR((uint16_t)(a + 1), (uint8_t)v); }

extern uint8_t A, B, DP, fN, fZ, fV, fC, fH, fI, fF, fE;
extern uint16_t X, Y, U, S;
#define D ((uint16_t)(A << 8 | B))
#define SETD(v) do { uint16_t d_ = (uint16_t)(v); A = (uint8_t)(d_ >> 8); B = (uint8_t)d_; } while (0)
#define NZ8(r) (fN = ((r) >> 7) & 1, fZ = ((uint8_t)(r)) == 0)
#define NZ16(r) (fN = ((r) >> 15) & 1, fZ = ((uint16_t)(r)) == 0)
#define GETCC() ((uint8_t)(fE << 7 | fF << 6 | fH << 5 | fI << 4 | fN << 3 | fZ << 2 | fV << 1 | fC))
#define SETCC(v) do { uint8_t c_ = (uint8_t)(v); fE = c_ >> 7; fF = c_ >> 6 & 1; fH = c_ >> 5 & 1; \
  fI = c_ >> 4 & 1; fN = c_ >> 3 & 1; fZ = c_ >> 2 & 1; fV = c_ >> 1 & 1; fC = c_ & 1; } while (0)
#define PUSH8(sp, v) do { sp--; WR(sp, (uint8_t)(v)); } while (0)
#define PUSH16(sp, v) do { uint16_t v_ = (uint16_t)(v); sp -= 2; WR16(sp, v_); } while (0)
static inline uint8_t pull8(uint16_t *sp) { uint8_t v = RD(*sp); (*sp)++; return v; }
static inline uint16_t pull16(uint16_t *sp) { uint16_t v = RD16(*sp); *sp += 2; return v; }
#define PULL8(sp) pull8(&sp)
#define PULL16(sp) pull16(&sp)

static inline uint8_t add8(uint8_t a, uint8_t b, uint8_t c) {
  unsigned r = a + b + c;
  fH = ((a ^ b ^ r) >> 4) & 1; fV = (((a ^ r) & (b ^ r)) >> 7) & 1; fC = (r >> 8) & 1; NZ8(r);
  return (uint8_t)r;
}
static inline uint8_t sub8(uint8_t a, uint8_t b, uint8_t c) {
  unsigned r = (unsigned)a - b - c;
  fV = (((a ^ b) & (a ^ r)) >> 7) & 1; fC = (r >> 8) & 1; NZ8(r);
  return (uint8_t)r;
}
static inline uint16_t add16(uint16_t a, uint16_t b) {
  uint32_t r = (uint32_t)a + b;
  fV = (((a ^ r) & (b ^ r)) >> 15) & 1; fC = (r >> 16) & 1; NZ16(r);
  return (uint16_t)r;
}
static inline uint16_t sub16(uint16_t a, uint16_t b) {
  uint32_t r = (uint32_t)a - b;
  fV = (((a ^ b) & (a ^ r)) >> 15) & 1; fC = (r >> 16) & 1; NZ16(r);
  return (uint16_t)r;
}

void cpu_exec(uint16_t entry);       // runs the translated routine at entry until its RTS
void cpu_bad_jump(uint16_t pc);
void mo5_swi(uint8_t fn);            // MO5 monitor calls: 0x02 PUTC (B), 0x0C keyboard (B)
void mo5_print_string(void);         // BASIC ROM $CE2C: prints the zero-terminated string at X
void mo5_print_int(void);            // BASIC ROM $D83E: prints D as a decimal number
