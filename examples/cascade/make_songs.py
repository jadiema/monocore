#!/usr/bin/env python3
"""Songs of CASCADE: three public-domain tunes, easy to play, with an accompaniment.

usage: python3 make_songs.py      -> au_clair.mts, frere_jacques.mts, ode_joie.mts
The melody, the part the player plays, is on track 0 (PSG lead); bass, strings and drums
accompany it on the PCM tracks. The instruments are those of MONOTRACKER's demo song.
"""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '../../tools/mtrack'))
from mtsong import Song, cell, note, NOTE_OFF
from make_demo import instruments

Q, H, W, E = 4, 8, 16, 2           # quarter, half, whole, eighth (in rows, 16 rows a bar)
CHORDS = {'C': ['C-3', 'E-3', 'G-3'], 'G': ['B-2', 'D-3', 'G-3'], 'F': ['C-3', 'F-3', 'A-3'],
          'Dm': ['D-3', 'F-3', 'A-3'], 'Am': ['C-3', 'E-3', 'A-3'], 'G7': ['B-2', 'F-3', 'G-3']}
ROOT = {'C': 'C-2', 'G': 'G-1', 'F': 'F-1', 'Dm': 'D-2', 'Am': 'A-1', 'G7': 'G-1'}

def build(name, melody, chords, bpm, out):
    """melody: list of (note or None for a rest, length in rows); chords: one per bar"""
    s = Song(name)
    s.bpm, s.speed, s.master = bpm, 6, 48
    I = instruments(s)
    lead = 1 + 0                       # 01 LEAD
    rows = []
    t = 0
    for n, ln in melody:
        rows.append((t, n, ln)); t += ln
    bars = (t + 15) // 16
    n_pat = (bars + 3) // 4
    s.orders = list(range(n_pat)); s.restart = 0
    for p in range(n_pat): s.pattern(p)
    def put(row, track, c):
        s.patterns[row // 64][row % 64][track] = c
    starts = {r for r, n, _ in rows if n}
    for r, n, ln in rows:
        if n:
            put(r, 0, cell(note(n), lead))
            if ln >= 4 and r + ln - 1 not in starts and r + ln < bars * 16:
                put(r + ln - 1, 0, cell(NOTE_OFF))     # a short gap before the next note
    for b in range(bars):
        ch = chords[b % len(chords)]
        r = b * 16
        put(r, 11, cell(note(ROOT[ch]) + 12, I['BASSE/P'], 56))
        put(r + 8, 11, cell(note(ROOT[ch]) + 12, I['BASSE/P'], 44))
        for k, cn in enumerate(CHORDS[ch]): put(r, 12 + k, cell(note(cn) + 12, I['CORDES/P'], 30))
        for st in (0, 8): put(r + st, 8, cell(note('C-4'), I['KICK/P'], 44))
        for st in (4, 12): put(r + st, 9, cell(note('C-4'), I['SNARE/P'], 22))
        for st in range(0, 16, 2): put(r + st, 10, cell(note('C-4'), I['HH FERME/P'], 24 if st % 4 else 34))
    data = s.encode()
    open(os.path.join(HERE, out), 'wb').write(data)
    print('%-14s %-22s %3d BPM, %2d mesures, %d octets' % (out, name, bpm, bars, len(data)))

# Au clair de la lune (traditional)
p1 = [('C-4', Q), ('C-4', Q), ('C-4', Q), ('D-4', Q), ('E-4', H), ('D-4', H), ('C-4', Q), ('E-4', Q), ('D-4', Q), ('D-4', Q), ('C-4', W)]
p3 = [('D-4', Q), ('D-4', Q), ('D-4', Q), ('D-4', Q), ('A-3', H), ('A-3', H), ('D-4', Q), ('C-4', Q), ('B-3', Q), ('A-3', Q), ('G-3', W)]
build('AU CLAIR DE LA LUNE', p1 + p1 + p3 + p1, ['C', 'G', 'G', 'C', 'C', 'G', 'G', 'C', 'G', 'Dm', 'G', 'G', 'C', 'G', 'G', 'C'], 100, 'au_clair.mts')

# Frere Jacques (traditional)
a = [('C-4', Q), ('D-4', Q), ('E-4', Q), ('C-4', Q)]
b = [('E-4', Q), ('F-4', Q), ('G-4', H)]
c = [('G-4', E), ('A-4', E), ('G-4', E), ('F-4', E), ('E-4', Q), ('C-4', Q)]
d = [('C-4', Q), ('G-3', Q), ('C-4', H)]
build('FRERE JACQUES', a + a + b + b + c + c + d + d, ['C'], 104, 'frere_jacques.mts')

# Ode to joy (Beethoven, 9th symphony)
l1 = [('E-4', Q), ('E-4', Q), ('F-4', Q), ('G-4', Q), ('G-4', Q), ('F-4', Q), ('E-4', Q), ('D-4', Q),
      ('C-4', Q), ('C-4', Q), ('D-4', Q), ('E-4', Q)]
end1 = [('E-4', 6), ('D-4', E), ('D-4', H)]
end2 = [('D-4', 6), ('C-4', E), ('C-4', H)]
mid = [('D-4', Q), ('D-4', Q), ('E-4', Q), ('C-4', Q), ('D-4', Q), ('E-4', E), ('F-4', E), ('E-4', Q), ('C-4', Q),
       ('D-4', Q), ('E-4', E), ('F-4', E), ('E-4', Q), ('D-4', Q), ('C-4', Q), ('D-4', Q), ('G-3', H)]
build('ODE A LA JOIE', l1 + end1 + l1 + end2 + mid + l1 + end2,
      ['C', 'G', 'C', 'G', 'C', 'G', 'C', 'C', 'G', 'C', 'G', 'C', 'C', 'G', 'C', 'C'], 108, 'ode_joie.mts')
