// Small C library for MONOCORE-1 (mc SDK). memset, memcpy, memmove, memcmp and strlen are in mini.c.
#include <stdint.h>
#include <stdarg.h>
#include "libc/string.h"
#include "libc/stdlib.h"
#include "libc/stdio.h"
#include "libc/ctype.h"
#include "mc.h"

// ---------------------------------------------------------------- strings
void *memchr(const void *s, int c, size_t n) {
  const unsigned char *p = s;
  for (; n; n--, p++) if (*p == (unsigned char)c) return (void *)p;
  return 0;
}
int strcmp(const char *a, const char *b) {
  while (*a && *a == *b) { a++; b++; }
  return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t n) {
  for (; n; n--, a++, b++) { if (*a != *b || !*a) return (unsigned char)*a - (unsigned char)*b; }
  return 0;
}
char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++) != 0) continue; return r; }
char *strncpy(char *d, const char *s, size_t n) {
  char *r = d;
  for (; n && *s; n--) *d++ = *s++;
  for (; n; n--) *d++ = 0;
  return r;
}
char *strcat(char *d, const char *s) { strcpy(d + strlen(d), s); return d; }
char *strncat(char *d, const char *s, size_t n) {
  char *p = d + strlen(d);
  for (; n && *s; n--) *p++ = *s++;
  *p = 0;
  return d;
}
char *strchr(const char *s, int c) {
  for (;; s++) { if (*s == (char)c) return (char *)s; if (!*s) return 0; }
}
char *strrchr(const char *s, int c) {
  const char *r = 0;
  for (;; s++) { if (*s == (char)c) r = s; if (!*s) return (char *)r; }
}
char *strstr(const char *h, const char *n) {
  size_t l = strlen(n);
  if (!l) return (char *)h;
  for (; *h; h++) if (*h == *n && !strncmp(h, n, l)) return (char *)h;
  return 0;
}

// ---------------------------------------------------------------- numbers
int abs(int v) { return v < 0 ? -v : v; }
long labs(long v) { return v < 0 ? -v : v; }
unsigned long strtoul(const char *s, char **end, int base) {
  while (isspace(*s)) s++;
  if ((base == 0 || base == 16) && s[0] == '0' && (s[1] | 32) == 'x') { s += 2; base = 16; }
  else if (base == 0) base = s[0] == '0' ? 8 : 10;
  unsigned long v = 0;
  for (;; s++) {
    int d = isdigit(*s) ? *s - '0' : isalpha(*s) ? (*s | 32) - 'a' + 10 : 99;
    if (d >= base) break;
    v = v * (unsigned long)base + (unsigned long)d;
  }
  if (end) *end = (char *)s;
  return v;
}
long strtol(const char *s, char **end, int base) {
  while (isspace(*s)) s++;
  int neg = *s == '-';
  if (*s == '-' || *s == '+') s++;
  long v = (long)strtoul(s, end, base);
  return neg ? -v : v;
}
int atoi(const char *s) { return (int)strtol(s, 0, 10); }
int rand(void) { return (int)(mc_rand() & RAND_MAX); }
void srand(unsigned s) { mc_srand(s); }

void qsort(void *base, size_t count, size_t size, int (*cmp)(const void *, const void *)) {
  // Shell sort: no recursion, no extra memory
  unsigned char *a = base;
  for (size_t gap = count / 2; gap; gap /= 2)
    for (size_t i = gap; i < count; i++)
      for (size_t j = i; j >= gap && cmp(a + (j - gap) * size, a + j * size) > 0; j -= gap) {
        unsigned char *x = a + (j - gap) * size, *y = a + j * size;
        for (size_t k = 0; k < size; k++) { unsigned char t = x[k]; x[k] = y[k]; y[k] = t; }
      }
}

// ---------------------------------------------------------------- heap (malloc)
// Blocks chained in address order: header = block size (multiple of 8) | 1 when in use.
// First fit, free neighbours are merged when a block is freed or searched.
extern char __bss_end[], __stack_top[];
typedef struct { uint32_t size; uint32_t pad; } hdr_t;
static hdr_t *heap_lo, *heap_hi;

static void heap_init(void) {
  uintptr_t lo = ((uintptr_t)__bss_end + 7) & ~(uintptr_t)7;
  uintptr_t hi = ((uintptr_t)__stack_top - MC_STACK) & ~(uintptr_t)7;
  heap_lo = (hdr_t *)lo; heap_hi = (hdr_t *)hi;
  heap_lo->size = (uint32_t)(hi - lo);
}
static hdr_t *next(hdr_t *h) { return (hdr_t *)((char *)h + (h->size & ~7u)); }

void *malloc(size_t n) {
  if (!heap_lo) heap_init();
  uint32_t need = (uint32_t)((n + sizeof(hdr_t) + 7) & ~(size_t)7);
  for (hdr_t *h = heap_lo; h < heap_hi; h = next(h)) {
    if (h->size & 1) continue;
    while (next(h) < heap_hi && !(next(h)->size & 1)) h->size += next(h)->size;   // merge
    if (h->size >= need) {
      if (h->size - need >= 16) { hdr_t *r = (hdr_t *)((char *)h + need); r->size = h->size - need; h->size = need; }
      h->size |= 1;
      return h + 1;
    }
  }
  return 0;
}
void free(void *p) {
  if (!p) return;
  hdr_t *h = (hdr_t *)p - 1;
  h->size &= ~1u;
  while (next(h) < heap_hi && !(next(h)->size & 1)) h->size += next(h)->size;
}
void *calloc(size_t c, size_t n) {
  size_t t = c * n;
  if (n && t / n != c) return 0;
  void *p = malloc(t);
  if (p) memset(p, 0, t);
  return p;
}
void *realloc(void *p, size_t n) {
  if (!p) return malloc(n);
  if (!n) { free(p); return 0; }
  hdr_t *h = (hdr_t *)p - 1;
  size_t old = (h->size & ~7u) - sizeof(hdr_t);
  if (old >= n) return p;
  void *q = malloc(n);
  if (q) { memcpy(q, p, old); free(p); }
  return q;
}
size_t mc_heap_free(void) {
  if (!heap_lo) heap_init();
  size_t f = 0;
  for (hdr_t *h = heap_lo; h < heap_hi; h = next(h)) if (!(h->size & 1)) f += h->size - sizeof(hdr_t);
  return f;
}

// ---------------------------------------------------------------- printf
static void out(char *buf, size_t size, size_t *pos, char c) { if (*pos + 1 < size) buf[*pos] = c; (*pos)++; }

int vsnprintf(char *buf, size_t size, const char *f, va_list ap) {
  size_t pos = 0;
  for (; *f; f++) {
    if (*f != '%') { out(buf, size, &pos, *f); continue; }
    f++;
    int zero = 0, left = 0, width = 0, lng = 0, plus = 0, prec = -1;
    for (;; f++) { if (*f == '0') zero = 1; else if (*f == '-') left = 1; else if (*f == '+') plus = 1; else break; }
    while (isdigit(*f)) width = width * 10 + (*f++ - '0');
    if (*f == '.') { f++; prec = 0; while (isdigit(*f)) prec = prec * 10 + (*f++ - '0'); }
    while (*f == 'l') { lng++; f++; }
    char tmp[24]; const char *s = tmp; int len = 0, neg = 0;
    switch (*f) {
    case 'd': case 'i': {
      long long v = lng >= 2 ? va_arg(ap, long long) : lng ? va_arg(ap, long) : va_arg(ap, int);
      unsigned long long u = v < 0 ? (neg = 1, -(unsigned long long)v) : (unsigned long long)v;
      do { tmp[len++] = (char)('0' + u % 10); u /= 10; } while (u);
      break; }
    case 'u': case 'x': case 'X': case 'p': {
      unsigned long long u = *f == 'p' ? (uintptr_t)va_arg(ap, void *) :
                             lng >= 2 ? va_arg(ap, unsigned long long) : lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
      unsigned b = *f == 'u' ? 10 : 16;
      const char *dg = *f == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
      do { tmp[len++] = dg[u % b]; u /= b; } while (u);
      if (*f == 'p') { tmp[len++] = 'x'; tmp[len++] = '0'; }
      break; }
    case 'c': tmp[len++] = (char)va_arg(ap, int); break;
    case 's': s = va_arg(ap, const char *); if (!s) s = "(null)";
      while (s[len] && (prec < 0 || len < prec)) len++;     // %.Ns: at most N characters
      break;
    case '%': tmp[len++] = '%'; break;
    default: tmp[len++] = '?'; break;
    }
    int rev = *f != 's';
    char sign = neg ? '-' : (plus && (*f == 'd' || *f == 'i')) ? '+' : 0;
    int total = len + (sign != 0);
    if (!left && !zero) for (int i = total; i < width; i++) out(buf, size, &pos, ' ');
    if (sign) out(buf, size, &pos, sign);
    if (!left && zero) for (int i = total; i < width; i++) out(buf, size, &pos, '0');
    for (int i = 0; i < len; i++) out(buf, size, &pos, rev ? s[len - 1 - i] : s[i]);
    if (left) for (int i = total; i < width; i++) out(buf, size, &pos, ' ');
    if (!*f) break;
  }
  if (size) buf[pos < size ? pos : size - 1] = 0;
  return (int)pos;
}
int snprintf(char *b, size_t n, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsnprintf(b, n, f, ap); va_end(ap); return r; }
int sprintf(char *b, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsnprintf(b, 0x7FFFFFFF, f, ap); va_end(ap); return r; }
int putchar(int c) { mc_debug_putc((char)c); return c; }
int puts(const char *s) { while (*s) mc_debug_putc(*s++); mc_debug_putc('\n'); return 0; }
int printf(const char *f, ...) {
  char b[256]; va_list ap; va_start(ap, f); int r = vsnprintf(b, sizeof b, f, ap); va_end(ap);
  for (char *p = b; *p; p++) mc_debug_putc(*p);
  return r;
}
void exit(int code) { printf("\n[exit %d]\n", code); for (;;) wfi(); }
void abort(void) { puts("\n[abort]"); for (;;) wfi(); }
void __mc_assert(const char *e, const char *file, int line) { printf("\n[assert] %s:%d: %s\n", file, line, e); for (;;) wfi(); }
