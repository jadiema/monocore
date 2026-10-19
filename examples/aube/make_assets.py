#!/usr/bin/env python3
"""Pictures of the AUBE start-up screen (indexed PNGs; the colours are computed by main.c).

scene.png (8 bpp, 320 x 240): a dawn sky over a dark plain, a range of hills and a black monolith.
  Each pixel index says WHERE it is, not its colour: main.c computes the 256 colours at every frame
  from the time, so the sunrise is a palette animation.
    1-192    sky: band s (0 top - 15 horizon) x glow level g (0-11): 1 + s * 12 + g
    193-224  plain: band b (0-7, from the horizon) x reflection level r (0-3): 193 + b * 4 + r
    225-240  monolith: rim light level 0-15
    241-252  hills: level 0 (inside) - 11 (lit ridge)
  Bands and levels are dithered (ordered 4 x 4 matrix) so that no banding shows.
logo.png, prompt.png (4 bpp): MONOCORE-1 and the prompt, anti-aliased in 15 grey levels.
mote.png: an 8 x 8 speck of light (sprite).
Needs Pillow and numpy; the generated files are in the repository, so building does not.
"""
import numpy as np
from PIL import Image, ImageDraw, ImageFont

W, H, HOR = 320, 240, 150                      # horizon line
BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16.0
yy, xx = np.mgrid[0:H, 0:W]
dith = BAYER[yy % 4, xx % 4]

def quant(v, levels):                           # v in 0..1 -> 0..levels-1, dithered
    return np.clip(np.floor(v * (levels - 1) + dith), 0, levels - 1).astype(int)

idx = np.zeros((H, W), int)
SUN_X, SUN_Y = 160, HOR
# sky
d = np.hypot((xx - SUN_X) / 1.6, yy - SUN_Y)
glow = np.exp(-d / 48.0) * 1.15 + np.exp(-d / 12.0) * 0.6
with np.errstate(all="ignore"): s = quant(np.clip(yy / HOR, 0, 1) ** 0.85, 16)
g = quant(np.clip(glow, 0, 1), 12)
sky = 1 + s * 12 + g
idx[:HOR] = sky[:HOR]
# plain, with the reflection of the dawn under the monolith
with np.errstate(all="ignore"): b = quant(np.clip((yy - HOR) / (H - HOR), 0, 1) ** 0.7, 8)
with np.errstate(all="ignore"): refl = np.exp(-np.abs(xx - SUN_X) / (10 + (yy - HOR) * 0.5)) * np.exp(-(yy - HOR) / 70.0)
r = quant(np.clip(refl * 1.1, 0, 1), 4)
idx[HOR:] = (193 + b * 4 + r)[HOR:]
# hills: two noisy ridges along the horizon
rng = np.random.default_rng(7)
def ridge(base, amp, smooth):
    n = rng.standard_normal(W + 64)
    k = np.ones(smooth) / smooth
    for _ in range(3): n = np.convolve(n, k, 'same')
    n = n[32:32 + W]; n = (n - n.min()) / (n.max() - n.min())
    return (base - amp * n).astype(int)
for base, amp, sm in ((HOR, 26, 24), (HOR, 14, 9)):
    top = ridge(base, amp, sm)
    for x in range(W):
        if abs(x - SUN_X) < 30 and base == HOR and amp == 26: top[x] = max(top[x], HOR - 6 + abs(x - SUN_X) // 6)
        for y in range(top[x], HOR):
            depth = (y - top[x]) / 4.0
            lit = np.exp(-depth) * np.exp(-abs(x - SUN_X) / 120.0)
            idx[y, x] = 241 + int(np.clip(lit * 11 + dith[y, x], 0, 11))
# monolith: 1 : 4 : 9 (seen from the front), standing on the plain, its edges lit by the dawn
MX0, MX1, MY0, MY1 = 150, 170, 62, 152
for y in range(MY0, MY1):
    for x in range(MX0, MX1):
        e = min(x - MX0, MX1 - 1 - x)
        top = y - MY0
        lit = max(np.exp(-e / 2.2), 0.7 * np.exp(-top / 1.5)) * (0.55 + 0.45 * (y - MY0) / (MY1 - MY0))
        idx[y, x] = 225 + int(np.clip(lit * 15 + dith[y, x] * 0.8, 0, 15))
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
        f = ImageFont.truetype(font, size)
        widths = [dr.textlength(c, font=f) for c in text]
        total = sum(widths) + spacing * (len(text) - 1)
        x = (W - total) / 2
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
text_image([('MONOCORE', 26, 172, 9, FONT), ('—  1  —', 11, 203, 2, FONTB)], 'logo.png')
text_image([('INSÉREZ UNE CARTOUCHE', 9, 222, 3, FONTB)], 'prompt.png')

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
