// Included first by the bench build: the translated code calls bench_tick() to measure time
// (and to sample where the program spends it).
#include <stdint.h>
void bench_tick(uint32_t pc);
#define M68K_TICK(c, pc) bench_tick(pc)
