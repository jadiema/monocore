#!/usr/bin/env python3
"""Finalise a MONOCORE-1 ROM: patch size, title, origin, rotation and DIP defaults into the header.
usage: mkrom.py in.bin out.mnc "Title" "Origin" [rotation] [dip]"""
import struct, sys

src, dst, title, origin = sys.argv[1:5]
rot = int(sys.argv[5]) if len(sys.argv) > 5 else 0
dip = int(sys.argv[6], 0) if len(sys.argv) > 6 else 0
rom = bytearray(open(src, 'rb').read())
while len(rom) % 4:
    rom.append(0)
assert rom[:4] == b'MNC1', 'missing MNC1 header'
struct.pack_into('<III', rom, 0x0C, len(rom), rot & 3, dip)
for off, text in ((0x20, title), (0x40, origin)):
    b = text.encode('utf-8')[:31]
    rom[off:off + 32] = b + bytes(32 - len(b))
open(dst, 'wb').write(rom)
print('%s: %d bytes, entry 0x%x' % (dst, len(rom), struct.unpack_from('<I', rom, 8)[0]))
