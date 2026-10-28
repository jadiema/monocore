#!/usr/bin/env python3
"""GRAVITON: the 16 x 16 text font, rendered from a vector font with anti-aliasing (as in SOLFEGE).

usage: python3 make_font.py   -> font16.h

DejaVu Sans Mono Bold (Bitstream Vera licence), 96 characters of 16 x 16 at 4 bpp. Each pixel keeps
4 levels of coverage (colour indices 1 = full to 4 = faint): the game sets each ink palette as blends
of the ink with the black background.
"""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
MONO = '/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf'
LEVELS = (0.85, 0.6, 0.35, 0.12)

def level(a):
    for i, t in enumerate(LEVELS):
        if a >= t * 255: return i + 1
    return 0

def glyph(size, ch, w, h, dy):
    f = ImageFont.truetype(MONO, size)
    im = Image.new('L', (w, h), 0)
    bb = f.getbbox(ch)
    ImageDraw.Draw(im).text(((w - (bb[2] - bb[0])) // 2 - bb[0], dy), ch, font=f, fill=255)
    return im

data = bytearray()
for c in range(32, 128):
    im = glyph(19, chr(c) if c < 127 else '?', 16, 16, -3)
    for y in range(16):
        for x in range(0, 16, 2):
            data.append(level(im.getpixel((x, y))) | level(im.getpixel((x + 1, y))) << 4)
lines = ['// made by make_font.py: do not edit', '#include <stdint.h>',
         'static const uint8_t font16[%d] = {' % len(data)]
for i in range(0, len(data), 24): lines.append('  ' + ', '.join('%d' % b for b in data[i:i + 24]) + ',')
lines.append('};')
open(os.path.join(HERE, 'font16.h'), 'w').write('\n'.join(lines) + '\n')
print('font16.h: 96 characters, 16 x 16, 4 levels of coverage')
