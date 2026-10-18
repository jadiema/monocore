# Generates sheet.png (logo + stars + font) and copper.png for the start-up screen
from PIL import Image
import math
font = Image.open('../sdk/font.png').convert('RGBA')
W, H = 256, 96
sheet = Image.new('P', (W, H), 0)
pal = [0, 0, 0] * 256
for i in range(1, 16): pal[3*i:3*i+3] = [i*16, i*16, i*16]   # placeholder palette (replaced in the code)
sheet.putpalette(pal)
px = sheet.load()
def glyph(ch):
    k = ord(ch) - 32
    gx, gy = (k % 16) * 8, (k // 16) * 8
    return [[font.getpixel((gx+x, gy+y))[:3] == (255, 255, 255) for x in range(8)] for y in range(8)]
# ---- logo: "MONOCORE-1", 8x8 letters scaled x3 wide, x4 high
text, SX, SY, OX, OY = "MONOCORE-1", 3, 4, 8, 3
shape = set()
for n, ch in enumerate(text):
    g = glyph(ch)
    for y in range(8):
        for x in range(8):
            if g[y][x]:
                for dy in range(SY):
                    for dx in range(SX):
                        shape.add((OX + n*8*SX + x*SX + dx, OY + y*SY + dy))
for (x, y) in shape:                       # drop shadow (index 14)
    if 0 <= x+3 < W and y+3 < 40: px[x+3, y+3] = 14
for (x, y) in shape:                       # outline (index 13)
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            if (x+dx, y+dy) not in shape and 0 <= x+dx < W and 0 <= y+dy < 40: px[x+dx, y+dy] = 13
for (x, y) in shape:                       # chrome gradient, line by line (indices 1-12)
    px[x, y] = 1 + min(11, (y - OY) * 12 // (8*SY))
# ---- stars: cells 160 (dot) and 161 (cross), cell row 5
px[3, 43] = 1
for (x, y, c) in ((11, 43, 2), (10, 43, 1), (12, 43, 1), (11, 42, 1), (11, 44, 1)): px[x, y] = c
# ---- SDK font for the scroller: cells from row 6 on (y = 48)
for y in range(48):
    for x in range(128):
        c = font.getpixel((x, y))
        if c[3] >= 128: px[x, 48 + y] = 1 if c[:3] == (255, 255, 255) else 2
sheet.save('sheet.png')
# ---- copper bar texture: one 16x16 tile, line r = colour r+1, last line transparent
cop = Image.new('P', (16, 16), 0); cop.putpalette(pal)
for y in range(15):
    for x in range(16): cop.putpixel((x, y), y + 1)
cop.save('copper.png')
# ---- sine table for the C code
print(', '.join(str(round(127*math.sin(2*math.pi*i/256))) for i in range(256)))
