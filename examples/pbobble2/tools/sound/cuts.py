#!/usr/bin/env python3
"""Which sound cuts which: the driver gives a new sound the YM2610 channels of an older one.

usage: python3 tools/sound/cuts.py   (after make_sounds.py; writes build/snd/cuts.txt)
For every pair (a, b) of sounds of tools/sound/sounds.txt (musics left out), renders a alone, b
alone 0.3 s later, and a then b: if a then b is not a + b, b took channels of a, and the console
stops a when b starts. Output lines: "b: a1 a2 ..." (the sounds b cuts).
"""
import os, subprocess, sys, wave
import numpy as np
here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(os.path.dirname(here))
SND = os.path.join(root, 'build', 'snd'); RENDER = os.path.join(SND, 'render')
sys.path.insert(0, here); import make_sounds
LEN, AT = 3.0, 18 + 18                         # seconds rendered, frame of b (0.3 s after a)


def run(events, name):
    path = os.path.join(SND, 'cut_' + name)
    with open(path + '.txt', 'w') as f:
        f.write('0 03\n6 07\n')
        for frame, ev in events:
            for k, v in enumerate(ev): f.write('%d %02X\n' % (frame + k, v))
    subprocess.check_call([RENDER, os.path.join(root, 'm1.bin'), os.path.join(root, 'v.bin'), path, str(LEN + 1),
                           '-trace', path + '.txt'], stdout=subprocess.DEVNULL)
    w = wave.open(path + '.wav')
    return np.frombuffer(w.readframes(w.getnframes()), np.int16).reshape(-1, 2).astype(np.float64).mean(axis=1)


def main():
    evs = [e for e in make_sounds.events() if not (len(e) == 1 and 0x21 <= e[0] < 0x60)]
    name = lambda e: ''.join('%02X' % v for v in e)
    alone = {name(e): run([(18, e)], 'a' + name(e)) for e in evs}
    later = {name(e): run([(AT, e)], 'b' + name(e)) for e in evs}
    t0 = AT * 55555 // 60
    out = []
    for b in evs:
        cut = []
        for a in evs:
            if a == b: continue
            ab = run([(18, a), (AT, b)], 'ab')
            n = min(len(ab), len(alone[name(a)]), len(later[name(b)]))
            res = ab[t0:n] - alone[name(a)][t0:n] - later[name(b)][t0:n]
            ea = np.sum(alone[name(a)][t0:n] ** 2)
            if ea > 1e6 and np.sum(res ** 2) > 0.3 * ea: cut.append(' '.join('%02X' % v for v in a))
        out.append('%s: %s' % (' '.join('%02X' % v for v in b), ', '.join(cut)))
        print(out[-1]); sys.stdout.flush()
    open(os.path.join(SND, 'cuts.txt'), 'w').write('\n'.join(out) + '\n')


if __name__ == '__main__':
    main()
