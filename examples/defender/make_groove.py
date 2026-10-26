#!/usr/bin/env python3
"""Background groove of PIANO DEFENDER: bass, drums and a pad in A minor (Am F C G), 112 BPM.

usage: python3 make_groove.py  -> groove.mts
The PSG tracks 0-7 stay empty: the game plays the player's notes on them. The enemies' notes
are taken from the C major scale, which is the scale of this progression.
"""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '../../tools/mtrack'))
from mtsong import Song, cell, note
from make_demo import instruments

s = Song('DEFENDER GROOVE')
s.bpm, s.speed, s.master = 112, 6, 44
I = instruments(s)
PROG = [('A-1', ['A-3', 'C-4', 'E-4']), ('F-1', ['A-3', 'C-4', 'F-4']), ('C-2', ['G-3', 'C-4', 'E-4']), ('G-1', ['G-3', 'B-3', 'D-4'])]
for p in range(2):
    pat = s.pattern(p)
    for bar, (root, chord) in enumerate(PROG):
        r0 = bar * 16
        for st, k in ((0, 0), (3, 0), (6, 12), (8, 0), (11, 0), (14, 7)):
            pat[r0 + st][11] = cell(note(root) + 12 + k, I['BASSE/P'], 52 if st in (0, 8) else 38)
        for st in (0, 6, 8) if p == 0 else (0, 6, 8, 10):
            pat[r0 + st][8] = cell(note('C-4'), I['KICK/P'], 50)
        for st in (4, 12):
            pat[r0 + st][9] = cell(note('C-4'), I['SNARE/P'] if p == 0 else I['CLAP/P'], 30)
        for st in range(0, 16, 2):
            pat[r0 + st][10] = cell(note('C-4'), I['HH FERME/P'], 26 if st % 4 else 34)
        if p == 1:
            for k, n in enumerate(chord): pat[r0][12 + k] = cell(note(n), I['NAPPE/P'], 26)
s.orders = [0, 0, 1, 1]
s.restart = 0
data = s.encode()
open(os.path.join(HERE, 'groove.mts'), 'wb').write(data)
print('groove.mts: %d octets' % len(data))
