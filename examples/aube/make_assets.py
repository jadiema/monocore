#!/usr/bin/env python3
"""Pictures of the AUBE start-up screen (indexed PNGs; the colours are computed by main.c).

scene.png (8 bpp, 368 x 272 for a 360 x 270 screen): a dawn sky over a dark plain, a range of hills
  and a black monolith. Each pixel index says WHERE it is, not its colour: main.c computes the 256
  colours at every frame from the time, so the sunrise is a palette animation.
    1-64     sky away from the sun: band 0 (top) - 63 (horizon)
    65-184   sky in the glow: band s (0-11) x glow level g (1-10): 65 + s * 10 + g - 1
    185-232  plain: band b (0-11, from the horizon) x reflection level r (0-3): 185 + b * 4 + r
    233-244  monolith: rim light level 0-11
    245-254  hills: level 0 (inside) - 9 (lit ridge)
  Bands and levels are dithered (ordered 4 x 4 matrix) so that no banding shows.
logo.png, prompt.png (4 bpp): MONOCORE-1 and the prompt, anti-aliased in 15 grey levels.
mote.png: an 8 x 8 speck of light (sprite).
Needs Pillow and numpy; the generated files are in the repository, so building does not.
"""
import numpy as np
from PIL import Image, ImageDraw, ImageFont

# The screen is 360 x 270: on a 1080p television that is exactly 4 times in both directions, so the
# MiSTer scaler shows every pixel sharp. The images are 368 x 272 (16-pixel tiles), shown from (4, 1).
SW, SH, OX, OY = 360, 270, 4, 1
W, H = 368, 272
HOR = 170                                       # horizon line (screen coordinates)
BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16.0
yy, xx = np.mgrid[0:H, 0:W]
yy = yy - OY; xx = xx - OX                      # screen coordinates
dith = BAYER[(yy + 8) % 4, (xx + 8) % 4]

def quant(v, levels):                           # v in 0..1 -> 0..levels-1, dithered
    v = np.nan_to_num(v)
    return np.clip(np.floor(v * (levels - 1) + dith), 0, levels - 1).astype(int)

idx = np.zeros((H, W), int)
SUN_X, SUN_Y = 180, HOR
sky_mask = yy < HOR
# sky: far from the sun, 64 fine bands (1-64); in the glow, 12 bands x 10 glow levels (65-184)
d = np.hypot((xx - SUN_X) / 1.6, yy - SUN_Y)
glow = np.exp(-d / 54.0) * 1.15 + np.exp(-d / 13.0) * 0.6
u = np.clip(yy / HOR, 0, 1) ** 0.85
g = np.clip(np.floor(np.clip(glow, 0, 1) * 10 + dith), 0, 10).astype(int)
fine = 1 + quant(u, 64)
coarse = 65 + quant(u, 12) * 10 + (g - 1)
idx[sky_mask] = np.where(g == 0, fine, coarse)[sky_mask]
# plain: 12 bands x 4 reflection levels (185-232)
with np.errstate(all="ignore"):
    b = quant(np.clip((yy - HOR) / (SH - HOR), 0, 1) ** 0.7, 12)
    refl = np.exp(-np.abs(xx - SUN_X) / (11 + (yy - HOR) * 0.55)) * np.exp(-(yy - HOR) / 80.0)
r = quant(np.clip(refl * 1.1, 0, 1), 4)
idx[~sky_mask] = (185 + b * 4 + r)[~sky_mask]
# hills: two ridges along the horizon (245-254)
rng = np.random.default_rng(7)
def ridge(amp, smooth):
    n = rng.standard_normal(W + 64)
    k = np.ones(smooth) / smooth
    for _ in range(3): n = np.convolve(n, k, 'same')
    n = n[32:32 + W]; n = (n - n.min()) / (n.max() - n.min())
    return (HOR - amp * n).astype(int)
for amp, sm, near_sun in ((29, 27, True), (16, 10, False)):
    top = ridge(amp, sm)
    for px in range(W):
        x = px - OX
        if near_sun:                                    # a soft dip in the ridge where the sun rises
            w = np.exp(-((x - SUN_X) / 26.0) ** 2)
            top[px] = int(round(top[px] + (HOR - 5 - top[px]) * w))
        for y in range(max(top[px], 0), HOR):
            depth = (y - top[px]) / 4.0
            lit = np.exp(-depth) * np.exp(-abs(x - SUN_X) / 130.0)
            idx[y + OY, px] = 245 + int(np.clip(lit * 9 + BAYER[(y + 8) % 4, (x + 8) % 4], 0, 9))
# monolith 1 : 4 : 9, its edges lit by the dawn (233-244)
MX0, MX1, MY0, MY1 = 169, 192, 70, 172
for y in range(MY0, MY1):
    for x in range(MX0, MX1):
        e = min(x - MX0, MX1 - 1 - x)
        lit = max(np.exp(-e / 1.6), 0.7 * np.exp(-(y - MY0) / 1.5)) * (0.55 + 0.45 * (y - MY0) / (MY1 - MY0))
        idx[y + OY, x + OX] = 233 + int(np.clip(lit * 11 + BAYER[(y + 8) % 4, (x + 8) % 4] * 0.8, 0, 11))
pal = [0] * (256 * 3)
img = Image.fromarray(idx.astype(np.uint8), 'P')
for i in range(256):                                                   # a preview palette only
    pal[i * 3:i * 3 + 3] = [i, i, i]
img.putpalette(pal); img.save('scene.png')

# text layers: anti-aliased white in indices 1-15
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
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans-ExtraLight.ttf'
FONTB = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
text_image([('MONOCORE', 30, 190, 11, FONT), ('—  1  —', 12, 227, 2, FONTB)], 'logo.png')
text_image([('INSÉREZ UNE CARTOUCHE', 10, 250, 3, FONTB)], 'prompt.png')

# a speck of light (sprite, 16 x 16 sheet: cell 0 at the top left)
sp = np.zeros((16, 16), np.uint8)
for y in range(8):
    for x in range(8):
        v = np.exp(-((x - 3.5) ** 2 + (y - 3.5) ** 2) / 1.3)
        sp[y, x] = int(np.clip(round(v * 15), 0, 15)) if v > 0.3 else 0   # no dark fringe
m = Image.fromarray(sp, 'P'); p = []
for i in range(16): v = i * 17; p += [v, v, v]
m.putpalette(p + [0] * (768 - len(p))); m.save('mote.png')
print('scene: %d colours used' % len(np.unique(idx)))
