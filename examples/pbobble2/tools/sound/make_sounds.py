#!/usr/bin/env python3
"""Prepares the sounds of the port from your copy of the game (m1.bin, v.bin from prepare.py).

usage: python3 tools/sound/make_sounds.py        (after tools/sound/build.sh)
Every event of tools/sound/sounds.txt is rendered by the game's own Z80 driver and a YM2610
(build/snd/render), then:
  - a sound that falls silent is kept up to there;
  - a sound still playing after 140 s is a music: its loop comes from the YM2610 writes of the
    driver (period.py, exact step for step); a piece that ends by itself is kept 60 s at most (the
    game stops those within 20 s);
  - all in OKI ADPCM (4 bits) at 22050 Hz, mono, with one gain for every sound so that their levels
    stay as in the game. A loop is encoded apart from its intro (the decoder starts again from
    zero there), since the console loops ADPCM in software.
Writes sounds.bin (the samples) and sounds_tab.bin (the table read by main.c). Both come from the
game: they stay out of the repository.

sounds_tab.bin: u32 count, then per sound seven u32: key (prefix << 8 | code, prefix 0 for a command
alone), offset in sounds.bin, length in samples, loop start in samples (0xFFFFFFFF = none; even, and
the loop part starts on a byte of its own), format (2 = ADPCM), rate in Hz, and the mask of the
sounds of the table it cuts when it starts (build/snd/cuts.txt from cuts.py: the driver gives it
their YM2610 channels). A sound silent alone is kept with length 0, so that it is known.
"""
import os, struct, subprocess, sys, wave
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import period
import numpy as np
from scipy.signal import resample_poly

here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(os.path.dirname(here))
SND = os.path.join(root, 'build', 'snd'); RENDER = os.path.join(SND, 'render')
RATE_IN, RATE = 55555, 22050
OKI_STEP = [int(16.0 * (11.0 / 10.0) ** i) for i in range(49)]
OKI_ADJ = [-1, -1, -1, -1, 2, 4, 6, 8]


SILENT = set()


def events():
    out = []
    for raw in open(os.path.join(here, 'sounds.txt')):
        line = raw.split('#')[0].split()
        if not line: continue
        ev = [int(v, 16) for v in line]; out.append(ev)
        if '#' in raw and 'silent' in raw.split('#', 1)[1]: SILENT.add(tuple(ev))
    return out


def render(ev, seconds):
    base = os.path.join(SND, 'clip')
    subprocess.check_call([RENDER, os.path.join(root, 'm1.bin'), os.path.join(root, 'v.bin'), base, str(seconds),
                           '-clip'] + ['%02X' % v for v in ev], stdout=subprocess.DEVNULL)
    w = wave.open(base + '.wav'); n = w.getnframes()
    x = np.frombuffer(w.readframes(n), np.int16).reshape(-1, 2)[:, 0].astype(np.float64)
    return resample_poly(x, 441, 1111)                          # 55555 Hz -> 22050 Hz


def music_loop(ev):
    """('loop', intro, period) or ('end', length) in samples at 22050 Hz, from the driver's writes."""
    base = os.path.join(SND, 'mloop')
    with open(base + '.txt', 'w') as f:
        f.write('0 03\n6 07\n' + ''.join('%d %02X\n' % (18 + k, v) for k, v in enumerate(ev)))
    subprocess.check_call([RENDER, os.path.join(root, 'm1.bin'), os.path.join(root, 'v.bin'), base, '500',
                           '-writes', base + '.txt'], stdout=subprocess.DEVNULL)
    r = period.find(period.steps(base + '.writes'))
    lead = 18 * RATE_IN // 60                                  # the event is sent at frame 18
    conv = lambda n: max(0, (n - lead)) * RATE // RATE_IN
    if r is None: return ('end', 120 * RATE)
    if r[0] == 'end': return ('end', conv(r[1]) + RATE)
    return ('loop', conv(r[0]), r[1] * RATE // RATE_IN)


def find_loop(x):
    """(loop start, period) in samples, or None if x does not repeat."""
    blk = RATE // 20                                            # envelope in 50 ms blocks
    env = np.abs(x[:len(x) // blk * blk]).reshape(-1, blk).mean(axis=1)
    win = 30 * 20                                               # compare the last 30 s
    best = None
    for p in range(8 * 20, len(env) - win - 20):
        a, b = env[-win:], env[-win - p:-p]
        c = np.corrcoef(a, b)[0, 1]
        if c > 0.995: best = p; break
    if best is None: return None
    # period at the sample level, around the envelope estimate
    seg = x[-5 * RATE:]
    lo, hi = best * blk - 2 * blk, best * blk + 2 * blk
    errs = [(np.mean((seg - x[-5 * RATE - p:len(x) - p]) ** 2), p) for p in range(lo, hi)]
    period = min(errs)[1]
    # loop start: first point from which the signal repeats with that period
    tol = 0.02 * np.mean(x[-10 * RATE:] ** 2) + 1.0
    start = None
    for s in range(0, len(x) - period - RATE, RATE // 4):
        if np.mean((x[s:s + RATE] - x[s + period:s + period + RATE]) ** 2) < tol: start = s; break
    if start is None: return None
    while start > 0 and abs(x[start - 1] - x[start - 1 + period]) < 64: start -= 1
    return start, period


def adpcm(x):
    """OKI MSM6295 ADPCM of a 12-bit signal, by following the decoder of SPEC 5.1 step by step."""
    sig, idx, out = 0, 0, bytearray()
    nib = []
    for v in x:
        step = OKI_STEP[idx]; d = int(v) - sig; n = 0
        if d < 0: n = 8; d = -d
        m = int(round((8.0 * d / step - 1) / 2)); m = 0 if m < 0 else 7 if m > 7 else m
        n |= m
        diff = ((2 * m + 1) * step) >> 3
        sig = max(-2048, min(2047, sig - diff if n & 8 else sig + diff))
        idx = max(0, min(48, idx + OKI_ADJ[m]))
        nib.append(n)
    if len(nib) & 1: nib.append(0)
    for k in range(0, len(nib), 2): out.append((nib[k] << 4) | nib[k + 1])   # high nibble first
    return bytes(out)


def main():
    if not os.path.exists(RENDER): sys.exit('build the renderer first: sh tools/sound/build.sh')
    clips = []
    for ev in events():
        name = ' '.join('%02X' % v for v in ev)
        if tuple(ev) in SILENT: print('%-6s silent (sounds.txt)' % name); clips.append((ev, np.zeros(0), None)); continue
        x = render(ev, 200)
        loud = np.nonzero(np.abs(x) > 150)[0]
        if not len(loud): print('%-6s silent' % name); clips.append((ev, np.zeros(0), None)); continue
        if loud[-1] > len(x) - 60 * RATE:                       # still playing after 140 s: a music
            kind = music_loop(ev)
            if kind[0] == 'loop':
                _, intro, p = kind
                x = render(ev, (intro + p) / RATE + 2)
                clips.append((ev, x[:intro + p], intro & ~1))
                print('%-6s music: intro %.2f s, loop %.2f s' % (name, intro / RATE, p / RATE))
            else:
                n = min(kind[1], 60 * RATE)
                x = render(ev, n / RATE + 1) if n > len(x) else x
                clips.append((ev, x[:n], None))
                print('%-6s long piece, ends at %.0f s, kept %.0f s' % (name, kind[1] / RATE, n / RATE))
        else:
            end = min(len(x), loud[-1] + RATE // 10)
            clips.append((ev, x[:end], None)); print('%-6s sound %.2f s' % (name, end / RATE))
    gain = 30000.0 / max(np.max(np.abs(c[1])) for c in clips if len(c[1]))
    names = [' '.join('%02X' % v for v in c[0]) for c in clips]
    cuts = {}
    cpath = os.path.join(SND, 'cuts.txt')
    if os.path.exists(cpath):
        for line in open(cpath):
            if ':' not in line: continue
            b, a = line.split(':')
            cuts[b.strip()] = sum(1 << names.index(x.strip()) for x in a.split(',') if x.strip() in names)
    else: print('no build/snd/cuts.txt (tools/sound/cuts.py): sounds will not cut each other')
    data, tab = bytearray(), []
    for ev, x, loop in clips:
        key = (ev[0] << 8 | ev[1]) if len(ev) == 2 else ev[0]
        y = x * gain
        while len(data) % 4: data.append(0)
        off = len(data)
        q = np.clip(np.round(y / 16), -2048, 2047)               # 12-bit signal of the decoder
        if loop is not None:                                    # intro and loop encoded apart
            data += adpcm(q[:loop]); data += adpcm(q[loop:])
            tab.append((key, off, len(q), loop, 2, RATE, cuts.get(names[len(tab)], 0)))
        else:
            data += adpcm(q); tab.append((key, off, len(q), 0xFFFFFFFF, 2, RATE, cuts.get(names[len(tab)], 0)))
    open(os.path.join(root, 'sounds.bin'), 'wb').write(bytes(data))
    with open(os.path.join(root, 'sounds_tab.bin'), 'wb') as f:
        f.write(struct.pack('<I', len(tab)))
        for t in tab: f.write(struct.pack('<7I', *t))
    print('sounds.bin: %d sounds, %.1f MB' % (len(tab), len(data) / 2 ** 20))


if __name__ == '__main__':
    main()
