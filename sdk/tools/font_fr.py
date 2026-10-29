#!/usr/bin/env python3
"""French letters of the SDK font: draws glyphs 96-127 of sdk/font.png and writes sdk/font_fr.h.

usage: python3 sdk/tools/font_fr.py

The 8 x 8 font of sdk/font.png has 96 glyphs (characters 32 to 127, rows 0-5 of the picture).
This script adds two rows: 32 glyphs made from the existing letters with an accent drawn above
them (the capitals are squeezed to 5 lines to leave room for it), plus a few signs. The shadow
(the dark colour, one pixel right and down) is computed again for the new glyphs. font_fr.h holds
the Unicode code of each new glyph and the decoder of UTF-8 text shared by mc_text and print.
Run it again after a change to the list below: the C table and the picture stay in step.
"""
import os
from PIL import Image

SDK = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PNG = os.path.join(SDK, 'font.png')
WHITE, DARK, CLEAR = (255, 255, 255, 255), (20, 20, 40, 255), (0, 0, 0, 0)

ACCENTS = {                                  # two lines (the lowercase letters start at line 2)
    'acute': ('.....#..', '....#...'),
    'grave': ('..#.....', '...#....'),
    'circ':  ('...##...', '..#..#..'),
    'trema': ('........', '..#..#..'),
}
# (character, base, accent): glyphs 96 + index; the order is that of the C table
GLYPHS = [
    ('é', 'e', 'acute'), ('è', 'e', 'grave'), ('ê', 'e', 'circ'), ('ë', 'e', 'trema'),
    ('à', 'a', 'grave'), ('â', 'a', 'circ'), ('ç', 'c', 'cedilla'), ('î', 'i', 'circ'),
    ('ï', 'i', 'trema'), ('ô', 'o', 'circ'), ('ù', 'u', 'grave'), ('û', 'u', 'circ'),
    ('ü', 'u', 'trema'), ('œ', None, None),
    ('É', 'E', 'acute'), ('È', 'E', 'grave'), ('Ê', 'E', 'circ'), ('Ë', 'E', 'trema'),
    ('À', 'A', 'grave'), ('Â', 'A', 'circ'), ('Ç', 'C', 'cedilla'), ('Î', 'I', 'circ'),
    ('Ï', 'I', 'trema'), ('Ô', 'O', 'circ'), ('Ù', 'U', 'grave'), ('Û', 'U', 'circ'),
    ('Œ', None, None),
    ('«', None, None), ('»', None, None), ('…', None, None), ('°', None, None), ('€', None, None),
]
DRAWN = {                                    # glyphs drawn whole (white pixels only)
    'œ': ('........', '........', '.##.##..', '#..#..#.', '#..####.', '#..#....', '.##.###.', '........'),
    'Œ': ('.######.', '#..#....', '#..###..', '#..#....', '.######.', '........', '........', '........'),
    '«': ('........', '........', '..#..#..', '.#..#...', '#..#....', '.#..#...', '..#..#..', '........'),
    '»': ('........', '........', '#..#....', '.#..#...', '..#..#..', '.#..#...', '#..#....', '........'),
    '…': ('........', '........', '........', '........', '........', '........', '#..#..#.', '........'),
    '°': ('..##....', '.#..#...', '.#..#...', '..##....', '........', '........', '........', '........'),
    '€': ('...###..', '..#.....', '#####...', '..#.....', '####....', '..#.....', '...###..', '........'),
}

im = Image.open(PNG).convert('RGBA')
im96 = im.crop((0, 0, 128, 48))              # the 96 original glyphs, kept as they are
def white(ch):
    i = ord(ch) - 32
    x0, y0 = (i % 16) * 8, (i // 16) * 8
    return [[im96.getpixel((x0 + x, y0 + y)) == WHITE for x in range(8)] for y in range(8)]
def rows(strs):
    return [[c == '#' for c in s] for s in strs]

out = Image.new('RGBA', (128, 64), CLEAR)
out.paste(im96, (0, 0))
assert len(GLYPHS) == 32
for k, (ch, base, acc) in enumerate(GLYPHS):
    if ch in DRAWN:
        g = rows(DRAWN[ch])
    else:
        g = white(base)
        if base.isupper():                   # capital: lines 0-6 squeezed into lines 2-6
            body = [g[0], g[2], g[3], g[5], g[6]] if base not in 'AO' else [g[0], g[1], g[3], g[5], g[6]]
            g = [[False] * 8, [False] * 8] + body + [[False] * 8]
        if acc == 'cedilla':
            g[7] = [x in (3, 4) and (x == 4) for x in range(8)]
        else:
            g[0], g[1] = rows(ACCENTS[acc])  # also removes the dot of i
    x0, y0 = ((96 + k) % 16) * 8, ((96 + k) // 16) * 8
    for y in range(8):
        for x in range(8):
            if g[y][x]: out.putpixel((x0 + x, y0 + y), WHITE)
            elif x > 0 and y > 0 and g[y - 1][x - 1]: out.putpixel((x0 + x, y0 + y), DARK)
out.save(PNG)

# 1-1 mappings: text that has no glyph of its own but an obvious equivalent
SUBST = {0x00A0: ' ', 0x202F: ' ', 0x2009: ' ', 0x2019: "'", 0x2018: "'", 0x201C: '"', 0x201D: '"',
         0x2013: '-', 0x2014: '-', 0x2212: '-', 0x00D7: 'x'}
# Latin-1 letters 0xC0-0xFF without a glyph of their own: shown without their accent
LATIN1 = 'AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty'
codes = [ord(ch) for ch, _, _ in GLYPHS]
h = ['// made by sdk/tools/font_fr.py: do not edit (glyphs 96-127 of sdk/font.png)',
     '// UTF-8 text -> glyph of the SDK font: font_next(&p) returns the glyph number + 32 (so 32-127',
     '// are the ASCII characters themselves, 128-159 the French letters below), 10 for a new line,',
     '// 0 at the end of the text, \'?\' for a character the font does not have (or broken UTF-8).',
     '#pragma once', '#include <stdint.h>', '',
     'static const uint16_t font_fr_codes[32] = {%s};   // %s' % (', '.join('0x%04X' % c for c in codes), ' '.join(ch for ch, _, _ in GLYPHS)),
     'static const char font_fr_latin1[65] = "%s";' % LATIN1,
     '', 'static inline int font_glyph(uint32_t u) {           // Unicode code -> glyph + 32',
     '  if (u >= 32 && u < 127) return (int)u;',
     '  for (int i = 0; i < 32; i++) if (font_fr_codes[i] == u) return 128 + i;']
h += ['  if (u == 0x%04X) return \'%s\';' % (c, '\\\'' if s == "'" else s) for c, s in sorted(SUBST.items())]
h += ['  if (u >= 0xC0 && u <= 0xFF) return font_fr_latin1[u - 0xC0];',
      '  return \'?\';', '}', '',
      'static inline int font_next(const char **pp) {      // next character of UTF-8 text',
      '  const uint8_t *p = (const uint8_t *)*pp;',
      '  uint32_t u = *p;',
      '  if (!u) return 0;',
      '  p++;',
      '  if (u >= 0x80) {',
      '    int n = u >= 0xF0 ? 3 : u >= 0xE0 ? 2 : u >= 0xC0 ? 1 : 0;',
      '    if (!n || (u & 0xC0) == 0x80) u = 0;              // stray continuation byte',
      '    else {',
      '      u &= 0x3Fu >> n;',
      '      for (; n && (*p & 0xC0) == 0x80; n--) u = u << 6 | (*p++ & 0x3Fu);',
      '      if (n) u = 0;                                   // sequence cut short',
      '    }',
      '    while ((*p & 0xC0) == 0x80) p++;',
      '  }',
      '  *pp = (const char *)p;',
      '  return u == \'\\n\' ? \'\\n\' : u ? font_glyph(u) : \'?\';',
      '}']
open(os.path.join(SDK, 'font_fr.h'), 'w', encoding='utf-8').write('\n'.join(h) + '\n')
big = out.resize((512, 256), Image.NEAREST); big.save('/tmp/font_preview.png')
print('font.png: 128 glyphs; font_fr.h written; preview in /tmp/font_preview.png')
