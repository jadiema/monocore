#!/usr/bin/env python3
"""The demonstration bank of MONOPIXEL: original pixel art drawn by this script.

usage: python3 make_demo.py   -> examples/monopixel/demo.mgf (and /tmp/mgfx_demo/*.png to look at it)
The default palette of a new bank is DEFAULT_PALETTE of mgbank.py (the same as default_pal in MONOPIXEL).
"""
import os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mgbank import Bank, Sprite, Map, rgb555, DEFAULT_PALETTE

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '../../examples/monopixel/demo.mgf')

DEFAULT = DEFAULT_PALETTE
HERO = [(0, 0, 0), (18, 20, 34), (40, 150, 150), (120, 220, 210), (30, 34, 60), (110, 240, 255), (235, 70, 70),
        (130, 135, 150), (250, 250, 250)] + [(0, 0, 0)] * 7
GOLD = [(0, 0, 0), (60, 34, 10), (200, 120, 20), (250, 200, 50), (255, 250, 200), (255, 255, 255)] + [(0, 0, 0)] * 10

b = Bank('DEMO', 1)
b.pals = [[rgb555(*c) if i else 0 for i, c in enumerate(p)] for p in (DEFAULT, HERO, GOLD)]
rnd = random.Random(7)


def put(x0, y0, rows, keys='0123456789ABCDEF'):
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch != '.':
                b.set_pixel(x0 + x, y0 + y, keys.index(ch))


def tile16(t):
    return (t % 8) * 16, (t // 8) * 16


def fill(t, f):
    x0, y0 = tile16(t)
    for y in range(16):
        for x in range(16):
            c = f(x, y)
            if c:
                b.set_pixel(x0 + x, y0 + y, c)


# ---------------------------------------------------------------- tiles (16 x 16, palette 0)
def dirt(x, y):
    v = rnd.random()
    return 2 if v < 0.18 else 4 if v < 0.24 else 3
grass_edge = [rnd.choice((3, 4, 4, 5)) for _ in range(16)]
fill(1, lambda x, y: (7 if y < 2 else 6 if y < grass_edge[x] else 5 if y == grass_edge[x] else dirt(x, y)))
fill(2, dirt)
def brick(x, y):
    row = y // 4
    off = 0 if row % 2 == 0 else 4
    if y % 4 == 3 or (x + off) % 8 == 7: return 12
    return 15 if (x + off) % 8 and y % 4 else 2
fill(3, brick)
fill(4, lambda x, y: 11 if (x == 0 or y < 2) and not (x == 15 or y > 13) else 13 if x == 15 or y > 13 else
     (12 if rnd.random() > 0.1 else 13))
def water(x, y):
    import math
    w = 2 + round(1.2 * math.sin((x + 2) * 0.8))
    return 10 if abs(y - w) < 1 or abs(y - w - 8) < 1 else 9 if y > 4 else 8 + (y > w)
fill(5, water)
def crate(x, y):
    if x < 2 or x > 13 or y < 2 or y > 13: return 2 if (x in (0, 15) or y in (0, 15)) else 4
    return 4 if abs(x - y) <= 1 or abs(x - (15 - y)) <= 1 else 3
fill(6, crate)
fill(7, lambda x, y: (5 if (x * 7 + y * 3) % 5 == 0 else 7 if (x + y * 2) % 7 == 0 else 6)
     if (x - 7.5) ** 2 + (y - 8) ** 2 < 70 or y > 4 else 0)
fill(8, lambda x, y: (2 if x in (5, 10) else 3 if (y + x) % 5 else 2) if 5 <= x <= 10 else 0)
put(*tile16(9), ['................', '................', '................', '................', '......FF........',
                 '.....F44F.......', '......FF........', '.......6........', '.......6..66....', '..66...6.66.....',
                 '...66..666......', '....6666........', '......66........', '.......6........', '.......6........', '.......6........'])
def cloud(x, y):
    d = min((x - 5) ** 2 + (y - 9) ** 2, (x - 10) ** 2 + (y - 7) ** 2, (x - 13) ** 2 + (y - 11) ** 2 * 2)
    return (11 if y < 11 else 12) if d < 20 and y < 14 else 0
fill(10, cloud)

# ---------------------------------------------------------------- sprites
body = ['.......6........', '.......1........', '....11111111....', '...1333333221...', '..133222222221..',
        '..132444444421..', '..132455445421..', '..132444444421..', '..132222222221..', '..133222222221..',
        '...1222222221...', '....11111111....']
legs = [['....17....71....', '....17....71....', '...177....771...', '...111....111...'],
        ['.....17..71.....', '.....17..71.....', '....177..771....', '....111..111....'],
        ['....17.....71...', '...17......71...', '..177......771..', '..111......111..'],
        ['.....17..71.....', '.....17..71.....', '....177..771....', '....111..111....']]
for f in range(4):
    bob = 1 if f % 2 else 0
    rows = (['................'] * bob + body + legs[f])[:16]
    put(f * 16, 64, rows)
for f, w in enumerate((6, 4, 1, 4)):        # coin: an ellipse that turns
    for y in range(16):
        for x in range(16):
            dx, dy = (x - 7.5) / (w + 0.6), (y - 7.5) / 6.6
            d = dx * dx + dy * dy
            if d < 1:
                c = 1 if d > 0.72 else 3 if x < 7.5 else 2
                if w > 1 and 0.2 < d < 0.4 and x < 7: c = 4
                b.set_pixel(64 + f * 16 + x, 64 + y, c)
from PIL import Image, ImageDraw
import math
for f in range(2):                          # star: two sizes
    im = Image.new('L', (64, 64), 0)
    r1, r2 = (30, 12) if f == 0 else (24, 10)
    pts = [(32 + (r1 if k % 2 == 0 else r2) * math.sin(k * math.pi / 5), 33 - (r1 if k % 2 == 0 else r2) * math.cos(k * math.pi / 5)) for k in range(10)]
    ImageDraw.Draw(im).polygon(pts, fill=255)
    im = im.resize((16, 16), Image.BOX)
    for y in range(16):
        for x in range(16):
            a = im.getpixel((x, y))
            if a > 90:
                b.set_pixel(f * 16 + x, 80 + y, 1 if a < 150 else 4 if (x + y < 12 and a > 200) else 3)
put(32, 80, ['.11..11.', '1FF11FF1', '1FBFFFF1', '1FFFFFF1', '.1FFFF1.', '..1FF1..', '...11...', '........'])

b.sprites = [Sprite(0, 8, 2, 2, 1, 4, 8, 0, 'HEROS'), Sprite(8, 8, 2, 2, 2, 4, 6, 0, 'PIECE'),
             Sprite(0, 10, 2, 2, 2, 2, 15, 0, 'ETOILE'), Sprite(4, 10, 1, 1, 0, 1, 0, 0, 'COEUR')]

# ---------------------------------------------------------------- a decor of 64 x 32 tiles of 16 pixels
m = Map(64, 32, True)
for x in range(64):
    for y in range(11, 32):
        m.set(x, y, 1 if y == 11 else 2)
for x in range(34, 38):
    m.set(x, 11, 5); m.set(x, 12, 5)
for x in range(10, 15): m.set(x, 7, 3)
for x in range(25, 28): m.set(x, 8, 4)
for x in range(50, 56): m.set(x, 6, 3)
m.set(18, 10, 6); m.set(19, 10, 6); m.set(19, 9, 6); m.set(41, 10, 6)
for t in (45, 58):
    m.set(t, 10, 8); m.set(t, 9, 8)
    for dx in (-1, 0, 1):
        m.set(t + dx, 8, 7); m.set(t + dx, 7, 7)
for x in (3, 22, 30, 52):
    m.set(x, 10, 9)
for x, y in ((5, 2), (6, 2), (30, 3), (31, 3), (47, 1), (48, 1), (60, 3)):
    m.set(x, y, 10)
b.maps = [m]

data = b.to_bytes()
open(OUT, 'wb').write(data)
print('%s: %d bytes' % (os.path.normpath(OUT), len(data)))
os.makedirs('/tmp/mgfx_demo', exist_ok=True)
b.sheet_image(0, 3).save('/tmp/mgfx_demo/sheet.png')
b.map_image(0, (120, 200, 240)).save('/tmp/mgfx_demo/map.png')
for i in range(len(b.sprites)):
    b.sprite_image(i, 0, 6).save('/tmp/mgfx_demo/sprite%d.png' % i)
assert Bank.from_bytes(data).to_bytes() == data
