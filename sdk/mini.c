// Minimal freestanding C support for MONOCORE-1 programs (the compiler may call these).
#include <stddef.h>
#include <stdint.h>

void *memset(void *d, int c, size_t n) {
  uint8_t *p = d;
  while (n && ((uintptr_t)p & 3)) { *p++ = (uint8_t)c; n--; }
  uint32_t w = (uint8_t)c * 0x01010101u;
  for (; n >= 4; n -= 4, p += 4) *(uint32_t *)p = w;
  while (n--) *p++ = (uint8_t)c;
  return d;
}

void *memcpy(void *d, const void *s, size_t n) {
  uint8_t *pd = d; const uint8_t *ps = s;
  if (!(((uintptr_t)pd | (uintptr_t)ps) & 3))
    for (; n >= 4; n -= 4, pd += 4, ps += 4) *(uint32_t *)pd = *(const uint32_t *)ps;
  while (n--) *pd++ = *ps++;
  return d;
}

void *memmove(void *d, const void *s, size_t n) {
  uint8_t *pd = d; const uint8_t *ps = s;
  if (pd < ps) while (n--) *pd++ = *ps++;
  else { pd += n; ps += n; while (n--) *--pd = *--ps; }
  return d;
}

int memcmp(const void *a, const void *b, size_t n) {
  const uint8_t *x = a, *y = b;
  for (; n; n--, x++, y++) if (*x != *y) return *x - *y;
  return 0;
}

size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

// 64-bit division and remainder (the compiler calls them for uint64_t / and %)
uint64_t __udivmoddi4(uint64_t n, uint64_t d, uint64_t *rem) {
  uint64_t q = 0, r = 0;
  if (d == 0) { if (rem) *rem = n; return ~(uint64_t)0; }
  for (int i = 63; i >= 0; i--) {
    r = (r << 1) | ((n >> i) & 1);
    if (r >= d) { r -= d; q |= (uint64_t)1 << i; }
  }
  if (rem) *rem = r;
  return q;
}
uint64_t __udivdi3(uint64_t n, uint64_t d) { return __udivmoddi4(n, d, 0); }
uint64_t __umoddi3(uint64_t n, uint64_t d) { uint64_t r; __udivmoddi4(n, d, &r); return r; }
int64_t __divdi3(int64_t a, int64_t b) {
  int neg = (a < 0) != (b < 0);
  uint64_t q = __udivdi3(a < 0 ? -(uint64_t)a : (uint64_t)a, b < 0 ? -(uint64_t)b : (uint64_t)b);
  return neg ? -(int64_t)q : (int64_t)q;
}
int64_t __moddi3(int64_t a, int64_t b) {
  uint64_t r = __umoddi3(a < 0 ? -(uint64_t)a : (uint64_t)a, b < 0 ? -(uint64_t)b : (uint64_t)b);
  return a < 0 ? -(int64_t)r : (int64_t)r;
}
