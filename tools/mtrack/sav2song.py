#!/usr/bin/env python3
"""Exports the songs of MONOTRACKER from its NVRAM save, for the games.

usage:
    python3 sav2song.py monotracker.sav                  lists the 8 slots
    python3 sav2song.py monotracker.sav 3 -o song.mts    slot 3 -> song.mts (data = song.mts)
    python3 sav2song.py monotracker.sav 3 -o song.h      slot 3 -> C array (const uint8_t song[])
    python3 sav2song.py song.mts --dump                  shows a song as text (patterns, instruments)
    python3 sav2song.py song.mts --tenues                sustained notes still sounding when a position starts
On MiSTer, the save is saves/MONOCORE/<name of the ROM>.sav on the SD card; it is written when the
OSD is opened after a save in the tracker. In the simulator, NVRAM=file.sav node tools/run.js ...
"""
import argparse, os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mtsong import Song, read_nvram, note_name, NOTE_OFF

def dump(s):
    print('%s - BPM %d, vitesse %d, volume %d, reprise %d' % (s.name, s.bpm, s.speed, s.master, s.restart))
    print('ordre:', ' '.join('%02X' % o for o in s.orders))
    for i, ins in enumerate(s.ins):
        if ins.pack() != bytes(32):
            print('ins %02X %-10s %s' % (i, ins.name, 'PCM' if ins.type else 'PSG'))
    for n, p in enumerate(s.patterns):
        used = [t for t in range(16) if any(p[r][t] for r in range(64))]
        if not used: continue
        print('motif %02X, pistes %s' % (n, ' '.join(str(t) for t in used)))
        for r in range(64):
            cells = []
            for t in used:
                c = p[r][t]
                nt, ins, vol, fx, par = c & 0x7F, (c >> 7) & 0x1F, (c >> 12) & 0x7F, (c >> 19) & 0x1F, c >> 24
                cells.append('%s %s %s %s' % (note_name(nt), '%02X' % ins if ins else '..', '%02X' % (vol - 1) if vol else '..',
                                              '%X%02X' % (fx - 1, par) if fx else '...'))
            print('  %02X  %s' % (r, ' | '.join(cells)))

def held_notes(s):
    """notes still sounding when a position starts, on a track that has nothing on its first row:
    the usual cause of pads piling up. Only sustained sounds count (sustain > 0); one walk through the
    positions, then the jump back to the restart position."""
    out, held = [], {}
    loops = set()                      # samples of the bank that loop: only they can hold a PCM note
    bank = os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../examples/tracker/mtbank.bin')
    if os.path.exists(bank):
        bd = open(bank, 'rb').read()
        for k in range(struct.unpack_from('<I', bd, 4)[0]):
            if struct.unpack_from('<I', bd, 8 + 32 * k + 8)[0] != 0xFFFFFFFF: loops.add(k)
    def sustained(i):
        ins = s.ins[i]
        if ins.sustain == 0: return False
        return ins.type == 0 or (ins.wave in loops and ins.duty != 1)
    TRACKS = ['PSG %d' % (t + 1) for t in range(8)] + ['PCM %d' % (t + 1) for t in range(8)]
    seq = list(range(len(s.orders))) + ([s.restart] if s.restart < len(s.orders) else [])
    for k, pos in enumerate(seq):
        rows = s.patterns[s.orders[pos]]
        for t in range(16):
            c = rows[0][t]
            if t in held and not (c & 0x7F):
                n, ins, p0, r0 = held[t]
                out.append((pos, t, n, ins, p0, r0, k == len(seq) - 1))
        if k == len(seq) - 1: break
        for r in range(64):
            for t in range(16):
                c = rows[r][t]; n, ins = c & 0x7F, (c >> 7) & 0x1F
                if n == 97: held.pop(t, None)
                elif 1 <= n <= 96:
                    i = ins or (held[t][1] if t in held else 0)
                    if i and i < len(s.ins) and sustained(i): held[t] = (n, i, pos, r)
                    else: held.pop(t, None)
    for pos, t, n, ins, p0, r0, wrap in out:
        print('position %02d (motif %02d) : la piste %s tient encore %s (%02X %s), jouee en position %02d ligne %02X%s'
              % (pos, s.orders[pos], TRACKS[t], note_name(n), ins, s.ins[ins].name, p0, r0,
                 ', au retour a la reprise' if wrap else ''))
    if not out: print('aucune note tenue d\'une position a l\'autre')
    return out

def main():
    ap = argparse.ArgumentParser(description='MONOTRACKER songs: from the NVRAM save to the games')
    ap.add_argument('file', help='monotracker.sav, or a song .mts with --dump')
    ap.add_argument('slot', nargs='?', type=int, help='slot 1-8')
    ap.add_argument('-o', '--output', help='.mts (binary) or .h (C array)')
    ap.add_argument('--dump', action='store_true', help='print the song as text')
    ap.add_argument('--tenues', action='store_true', help='list the sustained notes still sounding when a position starts (pads piling up)')
    a = ap.parse_args()
    data = open(a.file, 'rb').read()
    if data[:4] == b'MTS1':
        song = data
    else:
        slots = read_nvram(data)
        if a.slot is None:
            for k, sd in enumerate(slots):
                print('%d  %s' % (k + 1, '(vide)' if not sd else '%-16s %5d octets' % (Song.decode(sd).name, len(sd))))
            return
        song = slots[a.slot - 1]
        if not song: sys.exit('emplacement %d vide' % a.slot)
    s = Song.decode(song)
    if a.dump: dump(s)
    if a.tenues: held_notes(s)
    if a.output:
        if a.output.endswith('.h'):
            name = re.sub(r'\W', '_', os.path.splitext(os.path.basename(a.output))[0])
            lines = ['// %s: song "%s" for sdk/mtrack.h (made by tools/mtrack/sav2song.py)' % (os.path.basename(a.output), s.name),
                     '#pragma once', '#include <stdint.h>',
                     'static const uint8_t %s[%d] __attribute__((aligned(4))) = {' % (name, len(song))]
            for i in range(0, len(song), 16):
                lines.append('  ' + ', '.join('0x%02x' % b for b in song[i:i + 16]) + ',')
            lines.append('};')
            open(a.output, 'w').write('\n'.join(lines) + '\n')
        else:
            open(a.output, 'wb').write(song)
        print('%s: "%s", %d octets' % (a.output, s.name, len(song)))

if __name__ == '__main__':
    main()
