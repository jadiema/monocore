#!/usr/bin/env python3
"""Convert a PNG into MONOCORE-1 graphics.

usage: png2chr.py image.png out_prefix [--tile 8|16] [--bpp 4|8] [--map]

Writes:
  out_prefix.chr  graphics: for sprites (default) 8x8 cells in row-major order over the whole
                  image, matching the sprite layout (cell = tile + cy * width + cx);
                  with --tile 16, 16x16 tiles for a tile layer.
  out_prefix.pal  palette, RGB555 little-endian (16 colours in 4 bpp, 256 in 8 bpp).
  out_prefix.map  (with --map) one u16 tile number per tile, duplicates removed, tile 0 = empty.

Colour 0 is transparent: fully transparent pixels (alpha < 128) map to it. An indexed PNG keeps
its palette order (index 0 = transparent); an RGB(A) PNG gets its colours in order of appearance.
"""
import struct, sys
from PIL import Image

args = [a for a in sys.argv[1:] if not a.startswith('--')]
opts = sys.argv[1:]
def opt(name, default):
    return int(opts[opts.index(name) + 1]) if name in opts else default
src, prefix = args[0], args[1]
tile = opt('--tile', 8)
bpp = opt('--bpp', 4)
make_map = '--map' in opts
maxcol = 16 if bpp == 4 else 256

im = Image.open(src)
if im.mode == 'P' and 'transparency' not in im.info:
    w, h = im.size
    idx = [[im.getpixel((x, y)) for x in range(w)] for y in range(h)]
    pal = im.getpalette()[:3 * maxcol]
    colors = [tuple(pal[i * 3:i * 3 + 3]) for i in range(len(pal) // 3)]
else:
    im = im.convert('RGBA')
    w, h = im.size
    colors = [(0, 0, 0)]
    lookup = {}
    idx = []
    for y in range(h):
        row = []
        for x in range(w):
            r, g, b, a = im.getpixel((x, y))
            if a < 128:
                row.append(0); continue
            key = (r >> 3, g >> 3, b >> 3)
            if key not in lookup:
                lookup[key] = len(colors); colors.append((r, g, b))
            row.append(lookup[key])
        idx.append(row)
    if len(colors) > maxcol:
        sys.exit('%s: %d colours, %d maximum in %d bpp' % (src, len(colors), maxcol, bpp))
if w % tile or h % tile:
    sys.exit('%s: size %dx%d is not a multiple of %d' % (src, w, h, tile))

def pack(rows):
    out = bytearray()
    for r in rows:
        if bpp == 8:
            out += bytes(r)
        else:
            for x in range(0, len(r), 2):
                out.append((r[x] & 15) | ((r[x + 1] & 15) << 4))
    return bytes(out)

blocks = []
for ty in range(h // tile):
    for tx in range(w // tile):
        blocks.append(pack([idx[ty * tile + y][tx * tile:(tx + 1) * tile] for y in range(tile)]))

if make_map:
    empty = bytes(len(blocks[0]))
    uniq, out_map = {empty: 0}, []
    for b in blocks:
        if b not in uniq:
            uniq[b] = len(uniq)
        out_map.append(uniq[b])
    data = b''.join(sorted(uniq, key=uniq.get))
    open(prefix + '.map', 'wb').write(b''.join(struct.pack('<H', v) for v in out_map))
else:
    data = b''.join(blocks)
open(prefix + '.chr', 'wb').write(data)
colors += [(0, 0, 0)] * (maxcol - len(colors))
open(prefix + '.pal', 'wb').write(b''.join(struct.pack('<H', ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)) for r, g, b in colors[:maxcol]))
print('%s: %dx%d, %d %s, %d bytes' % (src, w, h, len(data) // len(blocks[0]), 'tiles' if make_map else 'cells', len(data)))
