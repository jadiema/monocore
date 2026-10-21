#!/usr/bin/env python3
"""Writes the sources of the APU stress ROM (tools/apu/stress): main.c (600 small functions, about
200 KB of code for the CPU to fetch from the ROM, called in a loop that also reads ROM data with
no locality) and noise.bin (5 MB of pseudo-random data, played as 16-bit PCM by 8 voices).
Both are generated, not kept in git. usage: python3 make_stress.py && python3 ../../../sdk/build.py ."""
import os
import numpy as np
here = os.path.dirname(os.path.abspath(__file__))
N, TERMS = 600, 18

def function(i):
    body = ' '.join('x = x * %du + %du; x ^= x >> %d;' % (2654435761 + i + j, 7 * i + j, 3 + (i + j) % 13)
                    for j in range(TERMS))
    return '__attribute__((noinline)) static uint32_t f%d(uint32_t x) { %s return x; }\n' % (i, body)

src = ['#include "mc.h"\n', 'extern const uint8_t noise_file[];\n', 'volatile uint32_t sink;\n']
src += [function(i) for i in range(N)]
src.append('static uint32_t (*const fn[%d])(uint32_t) = {%s};\n' % (N, ','.join('f%d' % i for i in range(N))))
src.append('''int main(void) {
  mc_init();
  mc_video_mode(320, 224);
  mc_display(DISP_ON);
  for (int v = 0; v < 8; v++)          // 8 voices of 16-bit PCM at 22050 Hz, from the ROM, looping
    mc_pcm_play(v, noise_file + v * 65536, 32768, 1, 22050, 0, 60, 60);
  uint32_t x = 1, k = 0;
  for (;;) {
    x = fn[k](x); k = (k + 1) %% %d;
    sink = x ^ noise_file[(x & 0x3FFFF0) + 1048576];       // ROM data reads with no locality
  }
}
''' % N)
open(os.path.join(here, 'main.c'), 'w').write(''.join(src))
np.random.default_rng(2026).integers(0, 256, 5 * 1024 * 1024, dtype=np.uint8).tofile(os.path.join(here, 'noise.bin'))
print('main.c and noise.bin written')
