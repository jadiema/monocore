#!/usr/bin/env python3
"""Pictures of SOLFEGE in 640 x 480: an anti-aliased font and the music symbols, rendered from vector fonts.

usage: python3 make_assets.py   -> assets.h (and /tmp/solfege_preview.png, to look at the staff)

  - text: DejaVu Sans Mono Bold (Bitstream Vera licence), 16 x 16 cells for the text layer
  - small text: the same font, 8 x 16, for the names under the keys of the piano
  - clefs, whole note, sharp and flat: GNU FreeSerif (GPL with the font exception), the Unicode
    musical symbols, sized for a staff whose lines are 16 pixels apart
Each pixel keeps 4 levels of coverage (colour indices 1 = full to 4 = faint): the game sets the
palettes as blends of the ink with the background behind it (dark screen or paper).
"""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
MONO = '/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf'
SERIF = '/usr/share/fonts/truetype/freefont/FreeSerif.ttf'
LEVELS = (0.85, 0.6, 0.35, 0.12)

def level(a):
    for i, t in enumerate(LEVELS):
        if a >= t * 255: return i + 1
    return 0

def glyph(fontfile, size, ch, w, h, dx=0, dy=0):
    """ch drawn in a w x h box: centred horizontally, at baseline dy (pixels from the top)"""
    f = ImageFont.truetype(fontfile, size)
    im = Image.new('L', (w, h), 0)
    d = ImageDraw.Draw(im)
    bb = f.getbbox(ch)
    d.text(((w - (bb[2] - bb[0])) // 2 - bb[0] + dx, dy), ch, font=f, fill=255)
    return im

def symbol(fontfile, size, ch, pad=2):
    """a symbol cropped to its ink, with its box in a canvas of whole 8 x 8 cells"""
    f = ImageFont.truetype(fontfile, size)
    bb = f.getbbox(ch)
    w, h = bb[2] - bb[0] + 2 * pad, bb[3] - bb[1] + 2 * pad
    W, H = (w + 7) // 8 * 8, (h + 7) // 8 * 8
    im = Image.new('L', (W, H), 0)
    ImageDraw.Draw(im).text(((W - (bb[2] - bb[0])) // 2 - bb[0], (H - (bb[3] - bb[1])) // 2 - bb[1]), ch, font=f, fill=255)
    return im

def pack4(im, x0, y0, w, h):
    """4 bpp bytes of a w x h block, rows top to bottom, low nibble = left pixel"""
    out = bytearray()
    for y in range(h):
        for x in range(0, w, 2):
            a, b = level(im.getpixel((x0 + x, y0 + y))), level(im.getpixel((x0 + x + 1, y0 + y)))
            out.append(a | b << 4)
    return out

def cells(im):
    """the 8 x 8 cells of a sprite picture, row major (cell (cx, cy) = first + cy * w + cx)"""
    out = bytearray()
    for cy in range(im.height // 8):
        for cx in range(im.width // 8):
            out += pack4(im, cx * 8, cy * 8, 8, 8)
    return out

def find_size(fontfile, ch, target_h):
    best = None
    for s in range(8, 200):
        bb = ImageFont.truetype(fontfile, s).getbbox(ch)
        if best is None or abs((bb[3] - bb[1]) - target_h) < abs(best[1] - target_h): best = (s, bb[3] - bb[1])
    return best[0]

def cstr(name, data):
    lines = ['static const uint8_t %s[%d] = {' % (name, len(data))]
    for i in range(0, len(data), 24): lines.append('  ' + ', '.join('%d' % b for b in data[i:i + 24]) + ',')
    lines.append('};')
    return '\n'.join(lines)

out = ['// made by make_assets.py: do not edit', '#include <stdint.h>']
# ------------------------------------------------------------ fonts
font16 = bytearray(); small = bytearray()
for c in range(32, 128):
    ch = chr(c) if c < 127 else '?'
    font16 += pack4(glyph(MONO, 19, ch, 16, 16, 0, -3), 0, 0, 16, 16)
    sm = glyph(MONO, 11, ch, 8, 16, 0, 1)
    small += pack4(sm, 0, 0, 8, 8) + pack4(sm, 0, 8, 8, 8)           # two cells, one above the other
out.append(cstr('font16', font16))
out.append(cstr('small16', small))
# ------------------------------------------------------------ music symbols
syms = {}
specs = [('head', '\U0001D15D', 15), ('treble', '\U0001D11E', 104), ('bass', '\U0001D122', 50),
         ('sharp', '\u266F', 42), ('flat', '\u266D', 38)]
for name, ch, h in specs:
    im = symbol(SERIF, find_size(SERIF, ch, h), ch)
    syms[name] = im
    out.append(cstr('spr_' + name, cells(im)))
    out.append('#define %s_W %d\n#define %s_H %d' % (name.upper(), im.width // 8, name.upper(), im.height // 8))
# where each symbol sits: the row of the picture that goes on the line it refers to
def ink_rows(im, x0, x1):
    return [y for y in range(im.height) if any(im.getpixel((x, y)) > 128 for x in range(x0, x1))]
t = syms['treble']
# the G line goes through the middle of the spiral: the middle of the rows the lower loop spans,
# found on the left part of the clef, between the top third and the tail
rows = ink_rows(t, 0, t.width // 3)
loop = [y for y in rows if t.height * 0.45 < y < t.height * 0.85]
g_row = (min(loop) + max(loop)) // 2 if loop else t.height * 62 // 100
b = syms['bass']
dots = ink_rows(b, b.width - 6, b.width)                # the two dots, on the right
f_row = (min(dots) + max(dots)) // 2 if dots else b.height // 3
s, fl = syms['sharp'], syms['flat']
fl_rows = ink_rows(fl, fl.width // 2, fl.width)          # the bowl of the flat
flat_row = (min(fl_rows) + max(fl_rows)) // 2 if fl_rows else fl.height * 2 // 3
out.append('#define TREBLE_G_ROW %d   // row of the clef on the G line' % g_row)
out.append('#define BASS_F_ROW %d     // row of the clef on the F line' % f_row)
out.append('#define SHARP_ROW %d\n#define FLAT_ROW %d\n#define HEAD_ROW %d' % (s.height // 2, flat_row, syms['head'].height // 2))
open(os.path.join(HERE, 'assets.h'), 'w').write('\n'.join(out) + '\n')

# ------------------------------------------------------------ a preview: a staff with the symbols, as in the game
pv = Image.new('RGB', (420, 170), (244, 236, 214))
d = ImageDraw.Draw(pv)
Y0 = 120
for k in range(5): d.rectangle((10, Y0 - 16 * k, 410, Y0 - 16 * k + 1), fill=(60, 54, 46))
def paste(im, x, y):
    ink = Image.new('RGB', im.size, (40, 36, 30))
    pv.paste(ink, (x, y), im)
paste(t, 16, Y0 - 32 - g_row)                            # G line: second line from the bottom
for i, step in enumerate((-2, 0, 2, 4, 6, 8, 10)):
    hx = 90 + i * 44
    paste(syms['head'], hx, Y0 - step * 8 - syms['head'].height // 2)
paste(s, 150, Y0 - 16 - s.height // 2)
paste(fl, 240, Y0 - 48 - flat_row)
paste(b, 340, Y0 - 48 - f_row)                           # (bass clef shown on the same staff, F line = 4th)
pv.resize((840, 340), Image.LANCZOS).save(os.path.join(HERE, 'preview.png'))
print('assets.h: font 16 x 16, small font 8 x 16, symbols', {k: (v.width, v.height) for k, v in syms.items()},
      'G row', g_row, 'F row', f_row, 'flat row', flat_row)
