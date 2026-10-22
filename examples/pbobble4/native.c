// Routines of the game rewritten in C (examples/pbobble4/private/, kept out of the repository: they
// are the game's code, rewritten): with private/native.c and private/native.s the build uses them,
// without them the translated code. Each one is checked on the bench against the translated code
// (work RAM and video RAM identical, frame by frame).
#include <stdint.h>
#include "neort.h"
#if __has_include("private/native.c")
#include "private/native.c"
#else
uint32_t neo_native(m68k_t *c, uint32_t pc) { (void)c; (void)pc; return 0; }
#endif
