#!/usr/bin/env python3
"""Font of MONOTRACKER in 640 x 480: 8 x 16 characters, as two 8 x 8 tiles (the top one, then the bottom one).

usage: python3 make_font.py   -> font480.h (and /tmp/font480_preview.png)
DejaVu Sans Mono Bold (Bitstream Vera licence), anti-aliased in 4 levels: colour indices 1 (full),
4, 5 and 6 (fainter), which the game blends with the colour behind the text. The strokes are at least
two pixels thick, so that nothing flickers in the interlaced picture.
"""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf'
f = ImageFont.truetype(FONT, 14)
LEV = ((0.78, 1), (0.52, 4), (0.3, 5), (0.12, 6))
def tile(im, y0):
    t = bytearray(32)
    for y in range(8):
        for x in range(8):
            a, v = im.getpixel((x, y0 + y)) / 255, 0
            for th, idx in LEV:
                if a >= th: v = idx; break
            t[y * 4 + x // 2] |= v << (4 * (x & 1))
    return t
data = bytearray()
pv = Image.new('L', (8 * 32, 16 * 3), 0)
for c in range(32, 128):
    ch = chr(c) if c < 127 else '?'
    im = Image.new('L', (8, 16), 0)
    bb = f.getbbox(ch)
    ImageDraw.Draw(im).text(((8 - (bb[2] - bb[0])) // 2 - bb[0], 0), ch, font=f, fill=255)
    pv.paste(im, ((c - 32) % 32 * 8, (c - 32) // 32 * 16))
    data += tile(im, 0) + tile(im, 8)
lines = ['// made by make_font.py: do not edit', '#include <stdint.h>', 'static const uint8_t font480[%d] = {' % len(data)]
for i in range(0, len(data), 32): lines.append('  ' + ', '.join(str(b) for b in data[i:i + 32]) + ',')
lines.append('};')
open(os.path.join(HERE, 'font480.h'), 'w').write('\n'.join(lines) + '\n')
pv.resize((pv.width * 3, pv.height * 3), Image.NEAREST).save('/tmp/font480_preview.png')
print('font480.h: %d bytes' % len(data))
