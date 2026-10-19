#!/usr/bin/env python3
"""Builds the game-data files of the port from your own copy of the game.

usage: python3 tools/prepare.py pbobbl2n.zip [--sheets]
  - 248-p1.p1 (68000 program, MAME word-swapped layout): kept as is in p1.bin, which is already
    the memory layout used by the runtime (16-bit words little-endian, byte access at addr ^ 1);
    p1_be.bin is the big-endian image for the disassembler and the translator
  - 248-c1..c4 (sprite tiles 16x16, 4 bpp, 4 bitplanes over two ROMs): converted to the MONOCORE
    4 bpp format (SPEC 4.3), identical tiles merged: spr.bin (tile 0 = empty tile) and spr_map.bin
    (uint16 per original tile number -> tile index in spr.bin)
    spr_cells.bin holds the same tiles in the layout of MONOCORE sprites: four 8x8 cells per tile
    (top left, top right, bottom left, bottom right), for the sprite tile cache of main.c
  - 248-s1.s1 (fix layer tiles 8x8, 4 bpp): converted to fix.bin
  - --sheets also writes contact sheets in build/check/ (fixed grey palette) to check the decoding
These files come from the original game: keep them out of any public repository.
"""
import os, sys, zipfile
import numpy as np

here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
ROMS = {'p1': '248-p1.p1', 's1': '248-s1.s1', 'm1': '248-m1.m1',
        'c1': '248-c1.c1', 'c2': '248-c2.c2', 'c3': '248-c3.c3', 'c4': '248-c4.c4',
        'v1': '248-v1.v1', 'v2': '248-v2.v2'}


def load(path):
    z = zipfile.ZipFile(path)
    names = {os.path.basename(n).lower(): n for n in z.namelist()}
    missing = [f for f in ROMS.values() if f not in names]
    if missing: sys.exit('missing in %s: %s' % (path, ' '.join(missing)))
    return {k: z.read(names[f]) for k, f in ROMS.items()}


def sprite_pixels(odd, even):
    """Neo Geo sprite tiles -> array (n, 16, 16) of 4-bit pixels.
    In each C ROM a tile is 64 bytes: 32 for the right half (x 8-15) then 32 for the left half
    (x 0-7), 2 bytes per line (bitplanes 0 and 1 in the odd ROM, 2 and 3 in the even one),
    bit 0 = leftmost pixel of the half."""
    a = np.frombuffer(odd, np.uint8).reshape(-1, 2, 16, 2)     # tile, half (R, L), line, plane
    b = np.frombuffer(even, np.uint8).reshape(-1, 2, 16, 2)
    bit = np.arange(8, dtype=np.uint8)
    px = np.zeros((a.shape[0], 16, 16), np.uint8)
    for half, x0 in ((1, 0), (0, 8)):
        for plane, src, k in ((0, a, 0), (1, a, 1), (2, b, 0), (3, b, 1)):
            v = (src[:, half, :, k, None] >> bit) & 1               # (n, 16 lines, 8 pixels)
            px[:, :, x0:x0 + 8] |= v << plane
    return px


def fix_pixels(s1):
    """Neo Geo fix tiles -> array (n, 8, 8). A tile is 32 bytes stored by columns of two pixels:
    bytes 0x10-0x17 hold x 0-1, 0x18-0x1F x 2-3, 0x00-0x07 x 4-5, 0x08-0x0F x 6-7 (one byte per
    line, low nibble = left pixel)."""
    t = np.frombuffer(s1, np.uint8).reshape(-1, 4, 8)             # tile, column pair, line
    px = np.zeros((t.shape[0], 8, 8), np.uint8)
    for pair, col in ((2, 0), (3, 2), (0, 4), (1, 6)):
        px[:, :, col] = t[:, pair, :] & 15
        px[:, :, col + 1] = t[:, pair, :] >> 4
    return px


def pack4(px):
    """(n, h, w) 4-bit pixels -> MONOCORE 4 bpp bytes (low nibble = left pixel), SPEC 4.3."""
    return (px[:, :, 0::2] | (px[:, :, 1::2] << 4)).astype(np.uint8).reshape(px.shape[0], -1)


def sheet(px, cols, path, scale=1):
    from PIL import Image
    n, h, w = px.shape
    rows = (n + cols - 1) // cols
    img = np.zeros((rows * (h + 1), cols * (w + 1)), np.uint8) + 40
    for i in range(n):
        r, c = divmod(i, cols)
        img[r * (h + 1):r * (h + 1) + h, c * (w + 1):c * (w + 1) + w] = px[i] * 15 + (px[i] > 0) * 20
    im = Image.fromarray(img, 'L')
    if scale > 1: im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
    im.save(path)


def main():
    if len(sys.argv) < 2: sys.exit(__doc__)
    r = load(sys.argv[1])
    p1 = r['p1']
    be = bytes(p1[i ^ 1] for i in range(len(p1)))
    assert be[0x100:0x107] == b'NEO-GEO', 'not a Neo Geo program ROM'
    out = lambda name, data: open(os.path.join(root, name), 'wb').write(data)
    out('p1.bin', p1); out('p1_be.bin', be)

    spr = np.concatenate([sprite_pixels(r['c1'], r['c2']), sprite_pixels(r['c3'], r['c4'])])
    packed = pack4(spr)                                            # (n, 128)
    keys = packed.view(np.dtype((np.void, 128))).ravel()
    empty = np.zeros(128, np.uint8).view(np.dtype((np.void, 128)))[0]
    uniq, first, inv = np.unique(keys, return_index=True, return_inverse=True)
    # keep the original order of first use, with the empty tile at index 0
    order = sorted(range(len(uniq)), key=lambda u: (uniq[u] != empty, first[u]))
    rank = np.empty(len(uniq), np.int64); rank[order] = np.arange(len(uniq))
    if uniq[order[0]] != empty: rank += 1; order = [None] + order  # no empty tile in the game
    assert len(order) < 65536
    tiles = np.zeros((len(order), 128), np.uint8)
    for i, u in enumerate(order):
        if u is not None: tiles[i] = packed[first[u]]
    out('spr.bin', tiles.tobytes())
    t16 = tiles.reshape(-1, 16, 8)                                  # 16 lines of 8 bytes
    cells = np.concatenate([t16[:, 0:8, 0:4], t16[:, 0:8, 4:8], t16[:, 8:16, 0:4], t16[:, 8:16, 4:8]], axis=1)
    out('spr_cells.bin', cells.reshape(-1, 128).tobytes())
    out('spr_map.bin', rank[inv.ravel()].astype('<u2').tobytes())

    fix = fix_pixels(r['s1'])
    out('fix.bin', pack4(fix).tobytes())

    print('p1.bin %d bytes; spr.bin %d tiles (%d original, %.1f MB); fix.bin %d tiles'
          % (len(p1), len(tiles), len(spr), tiles.nbytes / 2**20, len(fix)))
    if '--sheets' in sys.argv:
        d = os.path.join(root, 'build', 'check'); os.makedirs(d, exist_ok=True)
        sheet(fix[:1024], 32, os.path.join(d, 'fix_0000.png'), 2)
        used = tiles.reshape(-1, 16, 8)                            # back to pixels for the sheet
        px = np.zeros((len(tiles), 16, 16), np.uint8)
        px[:, :, 0::2] = used & 15; px[:, :, 1::2] = used >> 4
        for start in (0, 4096, 16384):
            sheet(px[start:start + 2048], 64, os.path.join(d, 'spr_%05d.png' % start))
        print('contact sheets in', d)


if __name__ == '__main__':
    main()
