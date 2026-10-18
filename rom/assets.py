#!/usr/bin/env python3
"""Generate the test ROM assets: graphics (MONOCORE-1 formats), palettes and sound samples.
Writes build/assets.bin (included in the ROM) and build/assets.h (offsets)."""
import math, os, random, struct, sys
from PIL import Image, ImageDraw, ImageFont

OUT = sys.argv[1] if len(sys.argv) > 1 else 'build'
os.makedirs(OUT, exist_ok=True)
random.seed(42)

blob = bytearray()
defs = []

def add(name, data, align=4):
    while len(blob) % align:
        blob.append(0)
    defs.append((name, len(blob), len(data)))
    blob.extend(data)

def rgb555(r, g, b):
    return ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)

def pal_bytes(cols):
    return b''.join(struct.pack('<H', rgb555(*c)) for c in cols)

def pack4(pixels, w, h):
    """pixels[y][x] -> 4bpp rows, low nibble = left pixel"""
    out = bytearray()
    for y in range(h):
        for x in range(0, w, 2):
            out.append((pixels[y][x] & 15) | ((pixels[y][x + 1] & 15) << 4))
    return bytes(out)

def cells(img_idx, cw, ch, bpp=4):
    """cut an index image (list of rows) into 8x8 cells, row-major (sprite layout)"""
    out = bytearray()
    for cy in range(ch):
        for cx in range(cw):
            sub = [row[cx * 8:cx * 8 + 8] for row in img_idx[cy * 8:cy * 8 + 8]]
            out += pack4(sub, 8, 8) if bpp == 4 else bytes(v for r in sub for v in r)
    return bytes(out)

# ------------------------------------------------------------------ font (96 chars, 8x8, 4bpp)
font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf', 9)
fdata = bytearray()
for c in range(32, 128):
    im = Image.new('L', (10, 12), 0)
    d = ImageDraw.Draw(im); d.fontmode = '1'
    d.text((1, -2), chr(c), font=font, fill=255)
    px = [[1 if im.getpixel((x, y)) > 127 else 0 for x in range(8)] for y in range(8)]
    # drop shadow (value 2) down-right
    sh = [[0] * 8 for _ in range(8)]
    for y in range(8):
        for x in range(8):
            if px[y][x]:
                sh[y][x] = 1
            elif (y > 0 and x > 0 and px[y - 1][x - 1]):
                sh[y][x] = 2
    fdata += pack4(sh, 8, 8)
add('FONT', fdata)

# ------------------------------------------------------------------ solid tiles: tile k = colour k
solid = bytearray()
for k in range(16):
    solid += pack4([[k] * 8 for _ in range(8)], 8, 8)
add('SOLID', solid)

# ------------------------------------------------------------------ tileset builder for maps
def build_map(img, mw, mh, ts=16):
    """img: list of rows of indices (mw*ts x mh*ts). returns (tiles bytes, map u16 list)"""
    tiles = {}
    tdata = bytearray()
    blank = tuple([0] * (ts * ts))
    tiles[blank] = 0
    tdata += bytes(ts * ts // 2)
    m = []
    for ty in range(mh):
        for tx in range(mw):
            t = tuple(img[ty * ts + y][tx * ts + x] for y in range(ts) for x in range(ts))
            if t not in tiles:
                tiles[t] = len(tiles)
                rows = [list(t[y * ts:(y + 1) * ts]) for y in range(ts)]
                tdata += pack4(rows, ts, ts)
            m.append(tiles[t])
    return bytes(tdata), m

# far layer: stars, 8x8 tiles, 64x32 map (512x256)
W, H = 512, 256
stars = [[0] * W for _ in range(H)]
for _ in range(260):
    x, y = random.randrange(W), random.randrange(H)
    b = random.choice([1, 1, 1, 2, 2, 3])
    stars[y][x] = b
    if b == 3 and 0 < x < W - 1 and 0 < y < H - 1:
        stars[y][x - 1] = stars[y][x + 1] = stars[y - 1][x] = stars[y + 1][x] = 1
t, m = build_map(stars, 64, 32, ts=8)
add('STARS_CHR', t); add('STARS_MAP', b''.join(struct.pack('<H', v) for v in m))

# mid layer: mountains, 16x16 tiles, 32x16 map (512x256)
mount = [[0] * W for _ in range(H)]
for x in range(W):
    a = 2 * math.pi * x / W
    top = 150 - 40 * math.sin(a * 2) - 18 * math.sin(a * 5 + 1) - 8 * math.sin(a * 11)
    for y in range(int(top), H):
        d = y - top
        v = 1 if d < 3 else (2 if (d < 40 or ((x + y) // 3) % 7) else 3)
        if d > 60: v = 3 if ((x // 4 + y // 4) % 2) else 2
        mount[y][x] = v
    snow = int(top) + 6
    for y in range(int(top), min(H, snow)):
        if top < 125: mount[y][x] = 4
t, m = build_map(mount, 32, 16)
add('MOUNT_CHR', t); add('MOUNT_MAP', b''.join(struct.pack('<H', v) for v in m))

# near layer: ground band with checker (used with per-line scroll), 16x16 tiles, 32x16 map
ground = [[0] * W for _ in range(H)]
for y in range(192, H):
    for x in range(W):
        stripe = ((x // 32) + (y // 16)) % 2
        ground[y][x] = 1 + stripe if y > 194 else 3
for x in range(W):           # bushes on top of the ground
    h = int(6 + 5 * math.sin(x / 9.0) + 3 * math.sin(x / 3.1))
    for y in range(192 - h, 192):
        ground[y][x] = 4 if (x + y) % 5 else 5
t, m = build_map(ground, 32, 16)
add('GROUND_CHR', t); add('GROUND_MAP', b''.join(struct.pack('<H', v) for v in m))

# ------------------------------------------------------------------ sprites
spr = bytearray(64)   # cell 0 (and 4bpp cells 0-1) left blank: tile 0 is transparent everywhere
spr_defs = []
def add_sprite(name, idx, cw, ch, bpp=4):
    global spr
    cell_bytes = 32 if bpp == 4 else 64
    while len(spr) % cell_bytes:
        spr.append(0)
    spr_defs.append((name, len(spr) // cell_bytes))
    spr += cells(idx, cw, ch, bpp)

# ball 16x16, shaded sphere 4bpp (values 1..7)
ball = [[0] * 16 for _ in range(16)]
for y in range(16):
    for x in range(16):
        dx, dy = x - 7.5, y - 7.5
        r = math.hypot(dx, dy)
        if r < 7.6:
            l = 1 - math.hypot(dx + 3, dy + 3) / 12
            v = 1 + int(max(0, min(0.999, l)) * 6)
            if r > 6.8: v = 1
            ball[y][x] = v
        if math.hypot(dx + 3, dy + 3) < 1.6: ball[y][x] = 8
add_sprite('BALL', ball, 2, 2)

# ship 32x32
ship = [[0] * 32 for _ in range(32)]
for y in range(32):
    for x in range(32):
        cx = abs(x - 15.5)
        if y > 4 and cx < (y - 4) * 0.55 and y < 26:
            ship[y][x] = 2 if cx < 3 else 3
            if cx < 1.5 and 10 < y < 18: ship[y][x] = 5
        if 18 < y < 28 and 8 < cx < 14 - (27 - y) * 0.2: ship[y][x] = 4
        if 25 < y < 31 and cx < 3 - (y - 26) * 0.5: ship[y][x] = 6 if y < 28 else 7
add_sprite('SHIP', ship, 4, 4)

# logo 128x32, 8bpp gradient text
logo_img = Image.new('L', (128, 32), 0)
d = ImageDraw.Draw(logo_img); d.fontmode = '1'
for size in range(24, 10, -1):
    lf = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', size)
    bb = d.textbbox((0, 0), 'MONOCORE', font=lf)
    if bb[2] - bb[0] <= 122: break
d.text(((128 - (bb[2] - bb[0])) // 2 - bb[0], (32 - (bb[3] - bb[1])) // 2 - bb[1]), 'MONOCORE', font=lf, fill=255)
logo = [[0] * 128 for _ in range(32)]
for y in range(32):
    for x in range(128):
        if logo_img.getpixel((x, y)) > 127:
            logo[y][x] = 16 + int((x / 128) * 200 + y * 1.2) % 224
        elif any(logo_img.getpixel((xx, yy)) > 127 for xx, yy in ((x - 1, y - 1), (x - 2, y - 2)) if 0 <= xx < 128 and 0 <= yy < 32):
            logo[y][x] = 1
add_sprite('LOGO', logo, 16, 4, bpp=8)
add('SPR_CHR', spr, align=64)

# ------------------------------------------------------------------ palettes (all 16-colour, except logo 256)
pals = {}
pals['FONT'] = [(0, 0, 0), (255, 255, 255), (20, 20, 40)] + [(0, 0, 0)] * 13
pals['FONT_HI'] = [(0, 0, 0), (255, 214, 64), (60, 30, 0)] + [(0, 0, 0)] * 13
pals['FONT_DIM'] = [(0, 0, 0), (120, 130, 160), (10, 10, 20)] + [(0, 0, 0)] * 13
pals['FONT_OK'] = [(0, 0, 0), (90, 255, 140), (0, 40, 10)] + [(0, 0, 0)] * 13
pals['STARS'] = [(0, 0, 0), (90, 100, 140), (180, 190, 230), (255, 255, 255)] + [(0, 0, 0)] * 12
pals['MOUNT'] = [(0, 0, 0), (120, 110, 190), (70, 60, 130), (50, 40, 100), (230, 235, 255)] + [(0, 0, 0)] * 11
pals['GROUND'] = [(0, 0, 0), (60, 170, 70), (40, 130, 50), (150, 230, 120), (30, 110, 60), (50, 150, 80)] + [(0, 0, 0)] * 10
pals['SHIP'] = [(0, 0, 0), (0, 0, 0), (230, 230, 240), (140, 150, 170), (200, 60, 60), (80, 200, 255), (255, 220, 80), (255, 110, 30)] + [(0, 0, 0)] * 8
ball_cols = [(255, 80, 80), (80, 220, 100), (90, 140, 255), (255, 210, 60), (220, 90, 255), (60, 220, 230), (255, 140, 40), (230, 230, 230)]
for i, (r, g, b) in enumerate(ball_cols):
    p = [(0, 0, 0)]
    for k in range(1, 8):
        f = 0.25 + 0.75 * (k - 1) / 6
        p.append((int(r * f), int(g * f), int(b * f)))
    p.append((255, 255, 255))
    pals['BALL%d' % i] = p + [(0, 0, 0)] * (16 - len(p))
# test-card palette: 16 palettes of 16 colours = hue x brightness sweep (256 colours)
import colorsys
card = []
for p in range(16):
    for k in range(16):
        if p == 0:
            v = k * 17; card.append((v, v, v))
        else:
            r, g, b = colorsys.hsv_to_rgb((p - 1) / 15, 0.85, 0.2 + 0.8 * k / 15)
            card.append((int(r * 255), int(g * 255), int(b * 255)))
logo_pal = [(0, 0, 0), (10, 10, 30)] + [(0, 0, 0)] * 14
for i in range(16, 256):
    r, g, b = colorsys.hsv_to_rgb(((i - 16) / 224 * 1.0) % 1, 0.75, 1.0)
    logo_pal.append((int(r * 255), int(g * 255), int(b * 255)))

pal_order = list(pals.keys())
palblob = b''.join(pal_bytes(pals[k]) for k in pal_order)
add('PALS', palblob)
add('PAL_CARD', pal_bytes(card))
add('PAL_LOGO', pal_bytes(logo_pal))

# ------------------------------------------------------------------ sound samples
def s8(vals): return bytes((max(-128, min(127, int(v * 127))) & 0xff) for v in vals)
def s16(vals): return b''.join(struct.pack('<h', max(-32768, min(32767, int(v * 32767)))) for v in vals)

# kick: 24 kHz, signed 8-bit
sr = 24000
kick = []
ph = 0
for i in range(int(sr * 0.35)):
    t = i / sr
    f = 45 + 140 * math.exp(-t * 28)
    ph += 2 * math.pi * f / sr
    kick.append(math.sin(ph) * math.exp(-t * 9))
add('SMP_KICK', s8(kick))
# snare: 48 kHz, signed 16-bit
sr = 48000
snare = []
for i in range(int(sr * 0.25)):
    t = i / sr
    n = random.uniform(-1, 1) * math.exp(-t * 18)
    tone = math.sin(2 * math.pi * 190 * t) * math.exp(-t * 30) * 0.6
    snare.append((n * 0.8 + tone) * 0.9)
add('SMP_SNARE', s16(snare))
# hi-hat: 48 kHz s8
hat = [random.uniform(-1, 1) * math.exp(-i / 48000 * 60) * 0.6 for i in range(int(48000 * 0.08))]
add('SMP_HAT', s8(hat))

# chime: OKI ADPCM, 16 kHz
def oki_encode(vals):
    steps = [int(16.0 * (1.1 ** i)) for i in range(49)]
    adj = [-1, -1, -1, -1, 2, 4, 6, 8]
    sig, idx = 0, 0
    nibs = []
    for v in vals:
        target = int(v * 2047)
        step = steps[idx]
        diff = target - sig
        n = 0
        if diff < 0: n = 8; diff = -diff
        q = min(7, int(diff * 4 / step) // 1)
        q = min(7, max(0, int(round((diff * 8 / step - 1) / 2))))
        n |= q
        d = ((2 * q + 1) * step) >> 3
        sig = max(-2048, min(2047, sig - d if n & 8 else sig + d))
        idx = max(0, min(48, idx + adj[q]))
        nibs.append(n)
    if len(nibs) % 2: nibs.append(0)
    return bytes((nibs[i] << 4) | nibs[i + 1] for i in range(0, len(nibs), 2)), len(nibs)
sr = 16000
chime = []
for i in range(int(sr * 1.2)):
    t = i / sr
    v = sum(a * math.sin(2 * math.pi * f * t) * math.exp(-t * d) for f, a, d in ((880, 0.5, 3), (1320, 0.25, 4), (2640, 0.15, 6), (3520, 0.08, 8)))
    chime.append(v * min(1, t * 400))
cdata, cn = oki_encode(chime)
add('SMP_CHIME', cdata)
defs.append(('SMP_CHIME_LEN_', 0, cn))
# pad: looping 16-bit, one cycle of a rich wave repeated (48 kHz, 220 Hz -> ~218 samples per cycle, 1 s attack part)
sr = 48000
pad = []
period = 48000 / 220.0
cyc = int(round(period * 4))  # 4 cycles in the loop
for i in range(cyc):
    t = i / sr
    v = sum(math.sin(2 * math.pi * 220 * h * t * (period * 4 / cyc)) / h for h in (1, 2, 3, 4, 5, 6)) * 0.45
    pad.append(v)
add('SMP_PAD', s16(pad))

# ------------------------------------------------------------------ tables
add('SINE', b''.join(struct.pack('<h', int(round(math.sin(2 * math.pi * i / 256) * 16384))) for i in range(256)))
add('NOTES', b''.join(struct.pack('<I', int(round(440 * 2 ** ((n - 69) / 12) * 16))) for n in range(128)))

# ------------------------------------------------------------------ write
with open(os.path.join(OUT, 'assets.bin'), 'wb') as f:
    f.write(blob)
with open(os.path.join(OUT, 'assets.h'), 'w') as f:
    f.write('// generated by assets.py\n#pragma once\n')
    for name, off, ln in defs:
        if name.endswith('_LEN_'):
            f.write('#define %sNIBBLES %d\n' % (name[:-4], ln))
            continue
        f.write('#define A_%s 0x%06x\n#define A_%s_SIZE %d\n' % (name, off, name, ln))
    for name, cell in spr_defs:
        f.write('#define SPR_%s %d\n' % (name, cell))
    for i, k in enumerate(pal_order):
        f.write('#define PAL_%s %d\n' % (k, i))
    f.write('#define PAL_COUNT %d\n' % len(pal_order))
    f.write('#define KICK_LEN %d\n#define SNARE_LEN %d\n#define HAT_LEN %d\n#define PAD_LEN %d\n' % (len(kick), len(snare), len(hat), len(pad)))
print('assets: %d bytes, %d palettes' % (len(blob), len(pal_order)))
