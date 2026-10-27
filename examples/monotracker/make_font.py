#!/usr/bin/env python3
"""Font of MONOTRACKER in 640 x 240: 8 x 8 cells for pixels twice as tall as they are wide.

usage: python3 make_font.py   -> font640.h (and /tmp/font640_preview.png)
Each glyph of DejaVu Sans Mono Bold (Bitstream Vera licence) is drawn in an 8 x 16 cell of square
pixels, as a VGA text font, then its pairs of rows are averaged into the 8 rows of a tile: on the
screen, the text looks like 80 x 30 characters of 8 x 16. Coverage is kept in 4 levels, colour
indices 1 (full), 4, 5 and 6 (fainter): the game blends them with the colour behind the text.
"""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf'
f = ImageFont.truetype(FONT, 14)
LEV = ((0.8, 1), (0.55, 4), (0.32, 5), (0.14, 6))
data = bytearray()
pv = Image.new('L', (8 * 32, 8 * 3 * 2), 0)
for c in range(32, 128):
    ch = chr(c) if c < 127 else '?'
    im = Image.new('L', (8, 16), 0)
    bb = f.getbbox(ch)
    ImageDraw.Draw(im).text(((8 - (bb[2] - bb[0])) // 2 - bb[0], 0), ch, font=f, fill=255)
    half = im.resize((8, 8), Image.BOX)                      # two rows of square pixels -> one row of the tile
    if ch == '=':                                            # its two bars would merge: one row each, a row apart
        half = Image.new('L', (8, 8), 0)
        for y in (3, 5):
            for x in range(1, 7): half.putpixel((x, y), 255)
    tile = bytearray(32)
    for y in range(8):
        for x in range(8):
            a, v = half.getpixel((x, y)) / 255, 0
            for t, idx in LEV:
                if a >= t: v = idx; break
            tile[y * 4 + x // 2] |= v << (4 * (x & 1))
            if v: pv.putpixel(((c - 32) % 32 * 8 + x, ((c - 32) // 32 * 8 + y) * 2), 255 if v == 1 else 150)
    data += tile
lines = ['// made by make_font.py: do not edit', '#include <stdint.h>', 'static const uint8_t font640[%d] = {' % len(data)]
for i in range(0, len(data), 32): lines.append('  ' + ', '.join(str(b) for b in data[i:i + 32]) + ',')
lines.append('};')
open(os.path.join(HERE, 'font640.h'), 'w').write('\n'.join(lines) + '\n')
pv.resize((pv.width * 3, pv.height * 3), Image.NEAREST).save('/tmp/font640_preview.png')
print('font640.h: %d bytes' % len(data))
