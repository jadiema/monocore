#!/usr/bin/env python3
"""Pictures of the AUBE start-up screen (indexed PNGs and scene_colors.h; main.c animates the colours).

scene.png (8 bpp, 656 x 496 for a 640 x 480 interlaced screen, hardware version 5) is monolithe.png (the
painting of the black monolith at sunset) reduced to 640 x 480 and split in three groups of colours, so that main.c can
make the dawn rise through the palette without redrawing a pixel:
    1-140    sky and hills (140 colours, dithered)
    141-230  water (90 colours, dithered)
    231-242  monolith: rim light level 0-11 (its edges light up when the logo appears)
  scene_colors.h gives, for every index, the colour of the painting, its colour at night, how early
  it catches the dawn (bright colours near the sun first) and how much the flash of the sun lights it.
logo.png, prompt.png (4 bpp): MONOCORE-1 and the prompt, anti-aliased in 15 grey levels. There is no
  transparency, so each 16 x 16 tile of text gets its own palette, mixed with the colour of the
  painting behind it (text_tiles[] in scene_colors.h: tile and scene index behind it).
mote.png: a 16 x 16 speck of light (sprite).
Needs Pillow and numpy; the generated files are in the repository, so building does not.
"""
import numpy as np
from PIL import Image, ImageDraw, ImageFont

SW, SH, OX, OY = 640, 480, 8, 8                 # 640 x 480 interlaced; images 656 x 496, shown from (8, 8)
W, H = 656, 496
S = 16 / 9                                      # scale from the first version, drawn for 360 x 270
HOR = round(171 * S)                            # first row of water (screen coordinates)
N_SKY, N_WATER, FIRST_MONO = 165, 65, 231

src = Image.open('monolithe.png').convert('RGB')
sc = src.resize((SW, round(SW * src.height / src.width)), Image.LANCZOS)
top = (sc.height - SH) // 2
sc = np.asarray(sc.crop((0, top, SW, top + SH))).astype(np.uint8)
full = np.pad(sc, ((OY, H - SH - OY), (OX, W - SW - OX), (0, 0)), mode='edge')
yy, xx = np.mgrid[0:H, 0:W]; yy = yy - OY; xx = xx - OX

# the monolith: the dark rectangle in the middle
lum = full.astype(int).sum(2)
dark = (lum < 70) & (xx > 150 * S) & (xx < 210 * S) & (yy > 60 * S) & (yy < HOR)
ys, xs = np.nonzero(dark)
MX0, MX1, MY0, MY1 = xs.min() - OX, xs.max() - OX + 1, ys.min() - OY, ys.max() - OY + 1
mono = (xx >= MX0) & (xx < MX1) & (yy >= MY0) & (yy < MY1)
print('monolith: x %d-%d, y %d-%d' % (MX0, MX1 - 1, MY0, MY1 - 1))

water = (yy >= HOR) & ~mono
sky = ~water & ~mono
idx = np.zeros((H, W), int)
colours = [(0, 0, 0)] * 256

def quantize(mask, n, first):
    pix = full[mask].reshape(1, -1, 3)
    palimg = Image.fromarray(pix, 'RGB').quantize(n, method=Image.Quantize.MEDIANCUT, kmeans=2)
    q = np.asarray(Image.fromarray(full, 'RGB').quantize(palette=palimg, dither=Image.Dither.NONE))
    idx[mask] = first + q[mask]
    p = palimg.getpalette()[:3 * n]
    for i in range(n): colours[first + i] = tuple(p[3 * i:3 * i + 3])
quantize(sky, N_SKY, 1)
quantize(water, N_WATER, 1 + N_SKY)
BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16.0
for y in range(MY0, MY1):                       # rim light level, as in the first AUBE
    for x in range(MX0, MX1):
        e = min(x - MX0, MX1 - 1 - x)
        lit = max(np.exp(-e / (1.6 * S)), 0.7 * np.exp(-(y - MY0) / (1.5 * S))) * (0.55 + 0.45 * (y - MY0) / (MY1 - MY0))
        idx[y + OY, x + OX] = FIRST_MONO + int(np.clip(lit * 11 + BAYER[(y + 8) % 4, (x + 8) % 4] * 0.8, 0, 11))

img = Image.fromarray(idx.astype(np.uint8), 'P')
pal = []
for i in range(256): pal += list(colours[i])
img.putpalette(pal); img.save('scene.png')      # the true colours, as a preview (main.c computes its own)

# per index: night colour, how early it catches the dawn (0-256), how much the flash lights it (0-256)
def night(c):
    r, g, b = c; L = (0.3 * r + 0.59 * g + 0.11 * b) / 255
    return (round(0.07 * r + 20 * L + 2), round(0.07 * g + 16 * L + 2), round(0.07 * b + 52 * L + 8))
rows = []
for i in range(1, FIRST_MONO):
    c = colours[i]; L = (0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]) / 255
    early = min(255, round(256 * L ** 0.6))
    warm = max(0.0, (c[0] - c[2]) / 255)
    flash = min(255, round(256 * min(1.0, 1.6 * L ** 1.5 * (0.3 + warm))))
    rows.append('  { %3d, %3d, %3d,  %3d, %3d, %3d,  %3d, %3d },' % (c + night(c) + (early, flash)))

# text layers: anti-aliased white in indices 1-15, and the scene colour behind each tile
def text_image(lines, name):
    im = Image.new('L', (W, H), 0)
    dr = ImageDraw.Draw(im)
    for text, size, y, spacing, font in lines:
        y += OY
        f = ImageFont.truetype(font, size)
        widths = [dr.textlength(c, font=f) for c in text]
        total = sum(widths) + spacing * (len(text) - 1)
        x = OX + (SW - total) / 2
        for c, w in zip(text, widths):
            dr.text((x, y), c, fill=255, font=f); x += w + spacing
    a = np.array(im).astype(float) / 255.0
    q = np.clip(np.round(a * 15), 0, 15).astype(np.uint8)
    out = Image.fromarray(q, 'P')
    p = []
    for i in range(16): v = i * 17; p += [v, v, v]
    out.putpalette(p + [0] * (768 - len(p))); out.save(name)
    tiles = []
    for ty in range(H // 16):
        for tx in range(W // 16):
            t = q[ty * 16:ty * 16 + 16, tx * 16:tx * 16 + 16].astype(float) / 15
            if not t.any(): continue
            wgt = t * (1 - t) + 0.05 * t                        # the edges matter most
            pix = full[ty * 16:ty * 16 + 16, tx * 16:tx * 16 + 16].astype(float)
            mean = (pix * wgt[..., None]).sum((0, 1)) / wgt.sum()
            cand = [i for i in range(1, FIRST_MONO)]
            best = min(cand, key=lambda i: sum((colours[i][k] - mean[k]) ** 2 for k in range(3)))
            tiles.append((tx, ty, best))
    return tiles
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans-ExtraLight.ttf'
FONTB = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
logo_tiles = text_image([('MONOCORE', round(30 * S), round(190 * S), round(11 * S), FONT),
                         ('—  1  —', round(12 * S), round(227 * S), round(2 * S), FONTB)], 'logo.png')
prompt_tiles = text_image([('INSÉREZ UNE CARTOUCHE', round(10 * S), round(250 * S), round(3 * S), FONTB)], 'prompt.png')

with open('scene_colors.h', 'w') as f:
    f.write('// Generated by make_assets.py from monolithe.png: do not edit.\n')
    f.write('#define SCENE_COLOURS %d   // indices 1-%d; then the monolith, %d-%d\n' % (FIRST_MONO - 1, FIRST_MONO - 1, FIRST_MONO, FIRST_MONO + 11))
    f.write('#define FIRST_MONO %d\n' % FIRST_MONO)
    f.write('// colour of the painting, colour at night, early (0-256: when it catches the dawn), flash (0-256)\n')
    f.write('static const struct { uint8_t r, g, b, nr, ng, nb, early, flash; } scene_colour[SCENE_COLOURS] = {\n')
    f.write('\n'.join(rows) + '\n};\n')
    f.write('// text tiles: layer (1 logo, 2 prompt), tile x, tile y, scene index behind it\n')
    f.write('#define TEXT_TILES %d\n' % (len(logo_tiles) + len(prompt_tiles)))
    f.write('static const struct { uint8_t layer, x, y, behind; } text_tiles[TEXT_TILES] = {\n')
    f.write('\n'.join('  { %d, %2d, %2d, %3d },' % ((l,) + t) for l, ts in ((1, logo_tiles), (2, prompt_tiles)) for t in ts) + '\n};\n')
    f.write('#define MONO_X0 %d\n#define MONO_X1 %d\n#define MONO_Y0 %d\n#define MONO_Y1 %d\n' % (MX0, MX1, MY0, MY1))

# a speck of light (sprite, 16 x 16 sheet: cell 0 at the top left)
sp = np.zeros((16, 16), np.uint8)
for y in range(16):
    for x in range(16):
        v = np.exp(-((x - 7.5) ** 2 + (y - 7.5) ** 2) / (1.3 * S * S))
        sp[y, x] = int(np.clip(round(v * 15), 0, 15)) if v > 0.3 else 0   # no dark fringe
m = Image.fromarray(sp, 'P'); p = []
for i in range(16): v = i * 17; p += [v, v, v]
m.putpalette(p + [0] * (768 - len(p))); m.save('mote.png')
print('scene: %d colours used, %d text tiles' % (len(np.unique(idx)), len(logo_tiles) + len(prompt_tiles)))
