// RV32IM self-check: same source compiled natively and for MONOCORE-1, results must match.
#include <stdint.h>
#ifdef NATIVE
#include <stdio.h>
#define OUT(c) putchar(c)
#else
#define OUT(c) (*(volatile uint32_t *)0x40000060 = (c))
#endif
static uint32_t s = 1;
static uint32_t rnd(void) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
static void hex(uint32_t v) { for (int i = 7; i >= 0; i--) OUT("0123456789abcdef"[(v >> (i * 4)) & 15]); OUT('\n'); }
int main(void) {
  uint32_t h = 0;
  for (int i = 0; i < 20000; i++) {
    uint32_t a = rnd(), b = rnd();
    if (i & 1) b >>= (rnd() & 31);
    if (i % 7 == 0) a = (int32_t)a >> (rnd() & 31);
    int32_t sa = (int32_t)a, sb = (int32_t)b;
    uint32_t r[16];
    r[0] = a + b; r[1] = a - b; r[2] = a ^ b; r[3] = a << (b & 31); r[4] = a >> (b & 31);
    r[5] = (uint32_t)(sa >> (b & 31)); r[6] = sa < sb; r[7] = a < b;
    r[8] = a * b; r[9] = (uint32_t)(((int64_t)sa * sb) >> 32); r[10] = (uint32_t)(((uint64_t)a * b) >> 32);
    r[11] = (uint32_t)(((int64_t)sa * (uint64_t)b) >> 32);
    r[12] = b ? a / b : 0; r[13] = b ? a % b : 0;
    r[14] = (sb && !(sa == INT32_MIN && sb == -1)) ? (uint32_t)(sa / sb) : 0;
    r[15] = (sb && !(sa == INT32_MIN && sb == -1)) ? (uint32_t)(sa % sb) : 0;
    int8_t c8 = (int8_t)a; int16_t c16 = (int16_t)b;
    for (int k = 0; k < 16; k++) h = (h * 31 + r[k]) ^ (h >> 7);
    h += (uint32_t)(int32_t)c8 + (uint32_t)(int32_t)c16;
  }
  hex(h);
  return 0;
}
