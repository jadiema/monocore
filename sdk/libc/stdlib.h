// Subset of <stdlib.h> for MONOCORE-1 (mc SDK)
#pragma once
#include <stddef.h>
#define RAND_MAX 0x7FFFFFFF
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
void *malloc(size_t n);
void *calloc(size_t count, size_t n);
void *realloc(void *p, size_t n);
void  free(void *p);
int   abs(int v);
long  labs(long v);
int   atoi(const char *s);
long  strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
int   rand(void);
void  srand(unsigned seed);
void  qsort(void *base, size_t count, size_t size, int (*cmp)(const void *, const void *));
void  exit(int code) __attribute__((noreturn));   // stops the console (endless loop)
void  abort(void) __attribute__((noreturn));
