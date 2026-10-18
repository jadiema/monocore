// <assert.h> for MONOCORE-1 (mc SDK): prints the message on DEBUG_OUT and stops
#pragma once
void __mc_assert(const char *expr, const char *file, int line) __attribute__((noreturn));
#ifdef NDEBUG
#define assert(e) ((void)0)
#else
#define assert(e) ((e) ? (void)0 : __mc_assert(#e, __FILE__, __LINE__))
#endif
