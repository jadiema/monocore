#!/usr/bin/env python3
"""Prepares a .mnc file for a MONOCORE-1 cartridge: one image per SPI Flash chip (W25Q64, 8 MB),
to burn with a programmer (MiniPro TL866: chip W25Q64, file of 8 MB).

usage: python3 cart_split.py game.mnc [outdir]
  -> game_puce0.bin (first 8 MB), and game_puce1.bin when the ROM is larger than 8 MB.
Each image is padded with 0xFF (erased Flash) to the size of the chip.
"""
import os, struct, sys

CHIP = 8 * 1024 * 1024
if len(sys.argv) < 2: sys.exit(__doc__)
data = open(sys.argv[1], 'rb').read()
if data[:4] != b'MNC1': sys.exit('ERROR: %s is not a .mnc file (no MNC1 signature)' % sys.argv[1])
size = struct.unpack_from('<I', data, 0x0C)[0]
if size != len(data): print('note: header size %d, file size %d' % (size, len(data)))
if len(data) > 2 * CHIP: sys.exit('ERROR: %d bytes, the cartridge holds 16 MB (two chips) at most' % len(data))
out = sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(os.path.abspath(sys.argv[1]))
base = os.path.splitext(os.path.basename(sys.argv[1]))[0]
for c in range((len(data) + CHIP - 1) // CHIP):
    part = data[c * CHIP:(c + 1) * CHIP]
    name = os.path.join(out, '%s_puce%d.bin' % (base, c))
    open(name, 'wb').write(part + b'\xff' * (CHIP - len(part)))
    print('%s: %d bytes of ROM, chip %d (/CS%d)' % (name, len(part), c, c))
