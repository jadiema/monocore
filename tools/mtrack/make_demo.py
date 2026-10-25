#!/usr/bin/env python3
"""Demo song of MONOTRACKER (examples/tracker/demo.mts) and its set of instruments.

The instruments of this song are also those of a new song in the tracker (it copies them from
demo.mts), so this file is the one place where the default instruments are defined.
An original arcade-style tune in A minor (Am F C G), 132 BPM: PSG lead, arpeggios and saw lead,
PCM drums, bass, strings and brass from the sample bank (make_bank.py).
usage: python3 make_demo.py [output]
"""
import os, sys
from mtsong import Song, Instrument, cell, note, NOTE_OFF, PSG, PCM, SQUARE, TRIANGLE, SAW, NOISE, BOUNCE, FIXED

def instruments(s):
    I = Instrument
    psg = [
        I('LEAD', wave=SQUARE, duty=64, volume=46, attack=1, decay=12, sustain=40, release=6, vib_speed=6, vib_depth=3, vib_delay=14),
        I('ARPEGE', wave=SQUARE, duty=96, volume=34, decay=8, sustain=26, release=3),
        I('BASSE', wave=TRIANGLE, volume=64, decay=10, sustain=48, release=3),
        I('KICK', wave=TRIANGLE, volume=64, decay=6, sustain=0, pitch_sweep=-60, flags=FIXED, transpose=-12),
        I('CAISSE', wave=NOISE, volume=50, decay=7, sustain=0, flags=FIXED, transpose=20),
        I('CHAPEAU', wave=NOISE, volume=34, decay=2, sustain=0, flags=FIXED, transpose=36),
        I('PWM NAPPE', wave=SQUARE, duty=128, duty_sweep=3, volume=40, attack=8, decay=20, sustain=40, release=12,
          vib_speed=4, vib_depth=2, vib_delay=20, flags=BOUNCE),
        I('DENT LEAD', wave=SAW, volume=40, attack=2, decay=10, sustain=44, release=8, vib_speed=5, vib_depth=3, vib_delay=10),
        I('BIP', wave=SQUARE, duty=128, volume=48, decay=4, sustain=0),
    ]
    one = dict(type=PCM, volume=60, sustain=64, release=2)
    pcm = [
        I('KICK', wave=0, **one), I('SNARE', wave=1, **one), I('CLAP', wave=2, **one),
        I('HH FERME', wave=3, **one), I('HH OUVERT', wave=4, **one), I('CRASH', wave=5, **one),
        I('TOM', wave=6, **one), I('RIM', wave=7, **one), I('COWBELL', wave=8, **one),
        I('BASSE', type=PCM, wave=9, volume=60, decay=14, sustain=40, release=4),
        I('PIANO EL', wave=10, **one), I('MARIMBA', wave=11, **one), I('CLOCHE', wave=12, **one),
        I('GUITARE', wave=13, **one),
        I('CUIVRES', type=PCM, wave=14, volume=52, attack=2, decay=10, sustain=44, release=5),
        I('CORDES', type=PCM, wave=15, volume=46, attack=10, sustain=64, release=14),
        I('ORGUE', type=PCM, wave=16, volume=50, sustain=64, release=3),
        I('CHOEUR', type=PCM, wave=17, volume=48, attack=12, sustain=64, release=16),
        I('FLUTE', type=PCM, wave=18, volume=52, attack=3, sustain=60, release=6, vib_speed=5, vib_depth=2, vib_delay=16),
        I('NAPPE', type=PCM, wave=19, volume=46, attack=20, sustain=64, release=24),
        I('ORCH HIT', wave=20, **one),
    ]
    for k, ins in enumerate(psg + pcm): s.ins[k + 1] = ins
    return {i.name + ('/P' if i.type == PCM else ''): k + 1 for k, i in enumerate(psg + pcm)}

# tracks: 0 lead, 1 arpeggio, 2 saw lead, 3 PSG hats; 8 kick, 9 snare/clap, 10 hats/crash,
# 11 bass, 12-14 chords, 15 percussion and hits
CHORDS = [('A-', 0x37, ['A-3', 'C-4', 'E-4'], 'A-1'), ('F-', 0x47, ['F-3', 'A-3', 'C-4'], 'F-1'),
          ('C-', 0x47, ['E-3', 'G-3', 'C-4'], 'C-2'), ('G-', 0x47, ['D-3', 'G-3', 'B-3'], 'G-1')]
MEL_A = [[(0, 'E-5', 3), (3, 'D-5', 1), (4, 'C-5', 2), (6, 'A-4', 2), (8, 'C-5', 2), (10, 'E-5', 2), (12, 'A-5', 4)],
         [(0, 'G-5', 2), (2, 'F-5', 2), (4, 'E-5', 2), (6, 'C-5', 2), (8, 'F-5', 4), (12, 'E-5', 2), (14, 'C-5', 2)],
         [(0, 'E-5', 3), (3, 'G-5', 1), (4, 'C-6', 4), (8, 'B-5', 2), (10, 'G-5', 2), (12, 'E-5', 4)],
         [(0, 'D-5', 2), (2, 'E-5', 2), (4, 'G-5', 2), (6, 'B-5', 2), (8, 'A-5', 6), (14, 'G-5', 2)]]
MEL_B = [[(0, 'A-5', 2), (2, 'G-5', 1), (3, 'E-5', 1), (4, 'A-5', 2), (6, 'C-6', 2), (8, 'B-5', 2), (10, 'A-5', 2), (12, 'E-5', 4)],
         [(0, 'F-5', 2), (2, 'A-5', 2), (4, 'C-6', 4), (8, 'A-5', 2), (10, 'F-5', 2), (12, 'G-5', 4)],
         [(0, 'G-5', 2), (2, 'E-5', 2), (4, 'G-5', 2), (6, 'C-6', 2), (8, 'E-6', 4), (12, 'D-6', 2), (14, 'C-6', 2)],
         [(0, 'B-5', 4), (4, 'D-6', 4), (8, 'G-5', 4), (12, 'B-5', 2), (14, 'D-6', 2)]]

def shift(n, semis): return note(n) + semis

def put(p, row, t, c):
    p[row][t] = c

def melody(p, t, inst, mel, transpose=0, vol=None):
    """notes (step, note, length) bar by bar; a note off ends the notes followed by a rest"""
    starts = {bar * 16 + step for bar, notes in enumerate(mel) for step, _, _ in notes}
    for bar, notes in enumerate(mel):
        for step, n, ln in notes:
            r = bar * 16 + step
            put(p, r, t, cell(shift(n, transpose), inst, vol))
            if ln >= 2 and r + ln < 64 and r + ln not in starts: put(p, r + ln, t, cell(NOTE_OFF))

def arpeggio(p, t, inst):
    for bar, (root, fx, _, _) in enumerate(CHORDS):
        for step in range(16):
            r = bar * 16 + step
            n = note(root + '4') if step % 4 == 0 else 0
            p[r][t] = cell(n, inst if n else 0, None, 0, fx)

def bass(p, t, inst):
    for bar, (_, _, _, b) in enumerate(CHORDS):
        root = note(b)
        for step, k in ((0, 0), (2, 0), (4, 12), (6, 0), (8, 0), (10, 0), (12, 12), (14, 7)):
            p[bar * 16 + step][t] = cell(root + 12 + k, inst, 48 if step % 4 else 60)

def chords(p, inst, stabs=None, vol=None):
    for bar, (_, _, tones, _) in enumerate(CHORDS):
        for k, n in enumerate(tones):
            if stabs:
                for st in stabs: p[bar * 16 + st][12 + k] = cell(note(n) + 12, inst, vol)
            else:
                p[bar * 16][12 + k] = cell(note(n), inst, vol)

def drums(p, I, style):
    for bar in range(4):
        b = bar * 16
        kicks = (0, 8) if style == 'a' else (0, 6, 8, 11) if style == 'b' else (0, 10)
        for st in kicks: p[b + st][8] = cell(note('C-4'), I['KICK/P'])
        for st in (4, 12):
            p[b + st][9] = cell(note('C-4'), I['SNARE/P'] if style != 'b' else I['CLAP/P'])
        hats = range(0, 16, 2) if style != 'b' else range(16)
        for st in hats:
            p[b + st][10] = cell(note('C-4'), I['HH FERME/P'], 56 if st % 4 == 0 else 32 if st % 2 == 0 else 20)
        if style != 'b': p[b + 14][10] = cell(note('C-4'), I['HH OUVERT/P'], 40)

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '../../examples/tracker/demo.mts')
    s = Song('DEMO MONOCORE')
    s.bpm, s.speed, s.master = 132, 6, 48
    I = instruments(s)
    # 0: intro - drums, bass, PWM pad
    p = s.pattern(0)
    drums(p, I, 'a'); bass(p, 11, I['BASSE/P'])
    for bar, (_, _, tones, _) in enumerate(CHORDS):
        p[bar * 16][2] = cell(note(tones[0]) + 12, I['PWM NAPPE'])
    p[0][15] = cell(note('C-3'), I['ORCH HIT/P'])
    for r in range(56, 64): p[r][9] = cell(note('C-4'), I['SNARE/P'], 20 + 5 * (r - 56))
    # 1: theme A - lead, arpeggios
    p = s.pattern(1)
    drums(p, I, 'a'); bass(p, 11, I['BASSE/P']); arpeggio(p, 1, I['ARPEGE'])
    melody(p, 0, I['LEAD'], MEL_A)
    p[0][10] = cell(note('C-4'), I['CRASH/P'], 40)
    for r in range(2, 64, 4): p[r][3] = cell(note('C-4'), I['CHAPEAU'])
    # 2: theme B - strings, claps, saw lead an octave under the lead
    p = s.pattern(2)
    drums(p, I, 'b'); bass(p, 11, I['BASSE/P']); arpeggio(p, 1, I['ARPEGE'])
    melody(p, 0, I['LEAD'], MEL_B); melody(p, 2, I['DENT LEAD'], MEL_B, -12, 0x24)
    chords(p, I['CORDES/P'])
    p[0][10] = cell(note('C-4'), I['CRASH/P'], 48)
    # 3: break - brass stabs, toms, snare roll and orchestra hits
    p = s.pattern(3)
    bass(p, 11, I['BASSE/P'])
    chords(p, I['CUIVRES/P'], stabs=(0, 3, 6, 10), vol=48)
    for bar in range(4):
        p[bar * 16][8] = cell(note('C-4'), I['KICK/P'])
        p[bar * 16 + 8][8] = cell(note('C-4'), I['KICK/P'])
    for k, r in enumerate((8, 10, 12, 14, 24, 26, 28, 30)): p[r][15] = cell(note('G-2') - (k % 4) * 2, I['TOM/P'])
    for r in list(range(32, 48, 2)) + list(range(48, 64)):
        p[r][9] = cell(note('C-4'), I['SNARE/P'], 16 + (r - 32))
    p[32][15] = cell(note('C-3'), I['ORCH HIT/P']); p[40][15] = cell(note('A#2'), I['ORCH HIT/P'])
    p[0][10] = cell(note('C-4'), I['CRASH/P'], 40)
    s.orders = [0, 1, 2, 1, 3, 2]
    s.restart = 1
    data = s.encode()
    open(out, 'wb').write(data)
    print('%s: %d bytes, %d patterns, %d positions' % (out, len(data), len(s.patterns), len(s.orders)))

if __name__ == '__main__':
    main()
