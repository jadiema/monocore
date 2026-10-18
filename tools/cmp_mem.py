#!/usr/bin/env python3
"""Compare two memory dumps (tools/run.js vs rtl testbench).
usage: cmp_mem.py a.mem b.mem [--ignore-hud]
Layout: palette 8K, sprites 8K, tilemaps 64K, line tables 8K, CHR RAM 128K, sound registers 8K.
--ignore-hud skips text-layer cells of row 0 (the CPU load readout differs by design: the cycle
model of the simulator is an estimate)."""
import struct, sys

REGIONS = [('palette', 0x2000), ('sprites', 0x2000), ('tilemaps', 0x10000), ('linetab', 0x2000), ('chrram', 0x20000), ('apu', 0x2000)]
a, b = open(sys.argv[1], 'rb').read(), open(sys.argv[2], 'rb').read()
ignore_hud = '--ignore-hud' in sys.argv
off, total = 0, 0
for name, size in REGIONS:
    ra, rb = a[off:off + size], b[off:off + size]
    diffs = []
    for w in range(0, size, 4):
        if ra[w:w + 4] != rb[w:w + 4]:
            if ignore_hud and name == 'tilemaps' and w < 0x4000 and (w // 4) // 64 == 0:
                continue
            diffs.append(w)
    total += len(diffs)
    if diffs:
        print('%-9s %5d mots differents' % (name, len(diffs)), end='')
        for w in diffs[:6]:
            va, vb = struct.unpack_from('<I', ra, w)[0], struct.unpack_from('<I', rb, w)[0]
            extra = ''
            if name == 'tilemaps':
                l, cell = divmod(w // 4, 4096)
                extra = ' plan%d (%d,%d)' % (l, cell % 64, cell // 64)
            print('\n    +0x%05x%s: %08x / %08x' % (w, extra, va, vb), end='')
        print()
    else:
        print('%-9s identique' % name)
    off += size
print('TOTAL', total)
sys.exit(1 if total else 0)
