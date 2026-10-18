#!/usr/bin/env python3
"""Draws the pixel art of the manual's examples: sprite sheet, map, sound."""
import math, os, random, struct, wave
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EX = os.path.join(ROOT, 'examples')

# ---------------------------------------------------------------- sprite sheet (16 x 16 each)
PAL = {'.': None, 'k': (20, 20, 40), 'w': (255, 255, 255), 'g': (150, 160, 190), 'b': (60, 120, 255),
       'c': (90, 220, 255), 'r': (230, 50, 50), 'o': (255, 150, 40), 'y': (255, 230, 60), 'p': (240, 100, 200),
       'd': (90, 90, 110), 'G': (60, 200, 90), 'Y': (255, 250, 180)}
PICTURES = {
'ship': """
.......ww.......
......wccw......
......wccw......
.....wbccbw.....
.....wbbbbw.....
....wbbwwbbw....
...wbbbwwbbbw...
..wbbbbbbbbbbw..
.wbbrbbbbbbrbbw.
wbbrrbbbbbbrrbbw
wbbbbbggggbbbbbw
.wwwbbg..gbbwww.
....wwo..oww....
.....oy..yo.....
......y..y......
................""",
'star': """
.......kk.......
......kyyk......
......kyyk......
.....kyYYyk.....
kkkkkkyYYykkkkkk
kyyyyyyYYyyyyyyk
.kyyyyyYYyyyyyk.
..kyyyYYYYyyyk..
...kyyyYYyyyk...
...kyyyyyyyyk...
..kyyyyyyyyyyk..
..kyyyykkyyyyk..
.kyyykk..kkyyyk.
.kyykk....kkyyk.
kyykk......kkyyk
kkk..........kkk""",
'star2': """
.......ww.......
.......yy.......
......kyyk......
.....kyYYyk.....
.kkkkkyYYykkkkk.
.kyyyyyYYyyyyyk.
..kyyyyYYyyyyk..
...kyyYYYYyyk...
...kyyyYYyyyk...
...kyyyyyyyyk...
..kyyyyyyyyyyk..
..kyyyykkyyyyk..
.kyyykk..kkyyyk.
.kyykk....kkyyk.
.kkk........kkk.
................""",
'bomb': """
.........o......
........oyo.....
.........o.k....
..........k.....
.....kkkkkkk....
....kddddddk....
...kddddddddk...
..kddwdddddddk..
..kdwwdddddddk..
..kddwdddddddk..
..kddddddddddk..
..kddddddddddk..
...kddddddddk...
....kddddddk....
.....kkkkkkk....
................""",
'explosion': """
.....o....o.....
..o...o..o...o..
...o.oyoyyo.o...
....oyyYYyyo....
.o.oyYYwwYYyo.o.
..oyYwwwwwwYyo..
.oyyYwwwwwwYyyo.
ooyYwwwwwwwwYyoo
.oyYwwwwwwwwYyo.
..oyYwwwwwwYyo..
.o.oyYYwwYYyo.o.
....oyyYYyyo....
...o.oyyyyo.o...
..o...o..o...o..
.....o....o.....
................""",
'heart': """
................
..kkkk....kkkk..
.krrrrk..krrrrk.
krrwwrrkkrrrrrrk
krwwrrrrrrrrrrrk
krrrrrrrrrrrrrrk
krrrrrrrrrrrrrrk
.krrrrrrrrrrrrk.
..krrrrrrrrrrk..
...krrrrrrrrk...
....krrrrrrk....
.....krrrrk.....
......krrk......
.......kk.......
................
................""",
}
ORDER = ['ship', 'star', 'star2', 'bomb', 'explosion', 'heart']

def sheet(path, names):
    im = Image.new('RGBA', (16, 16 * len(names)), (0, 0, 0, 0))
    for i, n in enumerate(names):
        rows = PICTURES[n].strip('\n').split('\n')
        assert len(rows) == 16, n
        for y, row in enumerate(rows):
            assert len(row) == 16, (n, y)
            for x, ch in enumerate(row):
                c = PAL[ch]
                if c: im.putpixel((x, 16 * i + y), c + (255,))
    im.save(path)

def preview(path_in, path_out, scale=8):
    im = Image.open(path_in).convert('RGBA')
    bg = Image.new('RGBA', im.size, (40, 40, 60, 255))
    bg.alpha_composite(im)
    bg.resize((im.width * scale, im.height * scale), Image.NEAREST).save(path_out)

# ---------------------------------------------------------------- map: 1024 x 240 (64 tiles: it wraps around seamlessly)
def landscape(path):
    W, H = 1024, 240
    im = Image.new('RGB', (W, H))
    sky = [(10, 10, 40), (14, 16, 60), (20, 24, 84), (30, 36, 110), (44, 52, 136), (64, 70, 160)]
    random.seed(3)
    for y in range(H):
        for x in range(W):
            band = min(5, y // 32)
            im.putpixel((x, y), sky[band])
    # background stars, aligned on the tiles so that they repeat
    for t in range(40):
        tx, ty = random.randrange(W // 16), random.randrange(6)
        im.putpixel((tx * 16 + 7, ty * 16 + 5), (200, 200, 255))
    # mountains
    for x in range(W):
        h = 150 + int(18 * math.sin(x * 2 * math.pi * 5 / W) + 10 * math.sin(x * 2 * math.pi * 23 / W + 1))
        for y in range(h, 192):
            c = (70, 60, 110) if (x + y) % 7 else (90, 80, 130)
            im.putpixel((x, y), c)
    # ground: grass and earth in 16-pixel tiles
    for y in range(192, H):
        for x in range(W):
            if y < 200: c = (60, 200, 90) if (x // 4 + y) % 3 else (40, 160, 70)
            else: c = (120, 80, 40) if ((x % 16) // 8 + (y % 16) // 8) % 2 else (100, 66, 34)
            im.putpixel((x, y), c)
    im = im.quantize(colors=15, method=Image.MEDIANCUT, dither=Image.NONE)
    # indexed palette: index 0 is transparent, so shift by one
    pal = im.getpalette()[:45]
    out = Image.new('P', im.size)
    out.putpalette([0, 0, 0] + pal + [0] * (768 - 48))
    out.putdata([p + 1 for p in im.get_flattened_data()] if hasattr(im, 'get_flattened_data') else [p + 1 for p in im.getdata()])
    out.save(path)

def boom(path, rate=16000):
    random.seed(1)
    n = int(rate * 0.5)
    frames = bytearray()
    lp = 0.0
    for i in range(n):
        env = math.exp(-i / (rate * 0.12))
        lp += 0.25 * (random.uniform(-1, 1) - lp)
        v = int(max(-1, min(1, lp * 2.2 * env)) * 32000)
        frames += struct.pack('<h', v)
    w = wave.open(path, 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate); w.writeframes(bytes(frames)); w.close()

if __name__ == '__main__':
    for d in ('04_sprites', '06_scroll', '07_stars'):
        os.makedirs(os.path.join(EX, d), exist_ok=True)
        sheet(os.path.join(EX, d, 'sheet.png'), ORDER)
    landscape(os.path.join(EX, '06_scroll', 'landscape.png'))
    landscape(os.path.join(EX, '07_stars', 'landscape.png'))
    boom(os.path.join(EX, '07_stars', 'boom.wav'))
    os.makedirs(os.path.join(ROOT, 'images'), exist_ok=True)
    preview(os.path.join(EX, '04_sprites', 'sheet.png'), os.path.join(ROOT, 'images', 'sheet_x8.png'))
