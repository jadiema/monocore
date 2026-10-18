// Subset of <stdio.h> for MONOCORE-1 (mc SDK): output goes to DEBUG_OUT
#pragma once
#include <stddef.h>
#include <stdarg.h>
#define EOF (-1)
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int printf(const char *fmt, ...);
int puts(const char *s);
int putchar(int c);
