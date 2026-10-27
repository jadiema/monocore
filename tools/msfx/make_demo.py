#!/usr/bin/env python3
"""The demonstration bank of MONOSFX: the usual sounds of a small game.
usage: python3 make_demo.py   -> examples/monosfx/demo.mfx and examples/plateformes/bruits.mfx
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from msbank import Bank, Effect, SQUARE, TRIANGLE, SAW, NOISE

HERE = os.path.dirname(os.path.abspath(__file__))
b = Bank('DEMO', [
    Effect('SAUT', wave=SQUARE, freq=260, slide=480, accel=-8, attack=0, sustain=4, decay=10, volume=110, duty=96),
    Effect('PIECE', wave=SQUARE, freq=988, attack=0, sustain=3, decay=14, volume=100, duty=64, arp=5, arp_time=4),
    Effect('TIR', wave=SAW, freq=1800, slide=-1600, attack=0, sustain=2, decay=10, volume=120),
    Effect('BOUM', wave=NOISE, freq=1400, slide=-160, attack=0, sustain=6, decay=40, volume=200),
    Effect('BONUS', wave=SQUARE, freq=523, attack=0, sustain=18, decay=12, volume=100, duty=128, repeat=6, slide=640, duty_sweep=-2),
    Effect('AIE', wave=SQUARE, freq=420, slide=-200, attack=0, sustain=10, decay=20, volume=140, duty=32, vib_depth=30, vib_speed=4),
    Effect('ECRASE', wave=NOISE, freq=3000, slide=-900, attack=0, sustain=2, decay=10, volume=150),
    Effect('BRAVO', wave=TRIANGLE, freq=523, attack=0, sustain=28, decay=30, volume=200, arp=7, arp_time=12, vib_depth=6, vib_speed=8),
    Effect('PERDU', wave=TRIANGLE, freq=400, slide=-96, attack=2, sustain=30, decay=30, volume=200, vib_depth=10, vib_speed=10),
    Effect('MENU', wave=SQUARE, freq=1320, attack=0, sustain=1, decay=4, volume=80, duty=128),
])
data = b.to_bytes()
for out in ('../../examples/monosfx/demo.mfx', '../../examples/plateformes/bruits.mfx'):
    p = os.path.normpath(os.path.join(HERE, out))
    os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, 'wb').write(data)
    print('%s : %d octets' % (p, len(data)))
assert Bank.from_bytes(data).to_bytes() == data
