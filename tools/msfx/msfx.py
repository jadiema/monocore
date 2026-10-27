#!/usr/bin/env python3
"""The sound effect banks of MONOSFX, between the NVRAM save, the games and WAV files.

usage:
    python3 msfx.py monosfx.sav                     lists the 8 slots
    python3 msfx.py monosfx.sav 1 -o bruits.mfx     slot 1 -> bruits.mfx (data = bruits.mfx, module msfx)
    python3 msfx.py bruits.mfx --dump               the parameters of each effect
    python3 msfx.py bruits.mfx --wav dossier/       each effect as a WAV file (an approximation of the PSG)
    python3 msfx.py bruits.mfx --to-sav monosfx.sav 2    into slot 2 of a save (created if missing)
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from msbank import Bank, read_nvram, write_nvram, WAVE_NAMES


def main():
    ap = argparse.ArgumentParser(description='MONOSFX sound effect banks')
    ap.add_argument('file'); ap.add_argument('slot', nargs='?', type=int)
    ap.add_argument('-o', '--output'); ap.add_argument('--dump', action='store_true')
    ap.add_argument('--wav', metavar='DIR'); ap.add_argument('--to-sav', nargs=2, metavar=('SAV', 'SLOT'))
    a = ap.parse_args()
    data = open(a.file, 'rb').read()
    if data[:4] == b'MFX1':
        bb = data
    else:
        banks = dict(read_nvram(data))
        if a.slot is None:
            for k, b in sorted(banks.items()):
                bk = Bank.from_bytes(b)
                print('%d: %-16s %2d bruitage(s)' % (k + 1, bk.name, len(bk.effects)))
            if not banks:
                print('aucune banque dans cette sauvegarde')
            return
        if a.slot - 1 not in banks:
            sys.exit('emplacement %d vide' % a.slot)
        bb = banks[a.slot - 1]
    b = Bank.from_bytes(bb)
    if a.dump:
        print('%s - %d bruitage(s)' % (b.name, len(b.effects)))
        for i, e in enumerate(b.effects):
            print('%2d %-8s %-8s %5d Hz glisse %+6d accel %+4d  A%3d T%3d C%3d vol %3d  rapport %3d%+d  vib %d/%d  arp %+d@%d  rep %d  (%d images)'
                  % (i, e.name, WAVE_NAMES[e.wave & 3], e.freq, e.slide, e.accel, e.attack, e.sustain, e.decay, e.volume,
                     e.duty, e.duty_sweep, e.vib_depth, e.vib_speed, e.arp, e.arp_time, e.repeat, e.length()))
    if a.wav:
        os.makedirs(a.wav, exist_ok=True)
        for i, e in enumerate(b.effects):
            e.write_wav(os.path.join(a.wav, '%02d_%s.wav' % (i, (e.name or 'bruit').lower())))
        print('WAV dans', a.wav)
    if a.to_sav:
        old = open(a.to_sav[0], 'rb').read() if os.path.exists(a.to_sav[0]) else None
        open(a.to_sav[0], 'wb').write(write_nvram(old, int(a.to_sav[1]) - 1, bb))
        print('%s : banque %s dans l\'emplacement %s' % (a.to_sav[0], b.name, a.to_sav[1]))
    if a.output:
        open(a.output, 'wb').write(bb)
        print('%s : %d octets' % (a.output, len(bb)))


if __name__ == '__main__':
    main()
