#!/usr/bin/env python3
"""Prepares the sounds of the port from your copy of the game (m1.bin, v.bin from prepare.py).

usage: python3 tools/sound/make_sounds.py        (after tools/sound/build.sh)
Every event of tools/sound/sounds.txt is rendered by the game's own Z80 driver and a YM2610
(build/snd/render), then:
  - a sound that falls silent is kept up to there;
  - a sound still playing after 140 s is a music: its loop comes from the YM2610 writes of the
    driver (period.py, exact step for step); a piece that ends by itself is kept 30 s at most (the
    game stops those within 20 s);
  - stored as 16-bit PCM, mono: short sounds (12 s at most) at 22050 Hz, musics and long pieces at
    16000 Hz (the ADPCM samples of the Neo Geo are 18.5 kHz) so that everything fits in 32 MB; each
    sound normalised to its own peak, its level in the game given back by its playback volume.
    Tried first: OKI ADPCM (about 36 dB for musics, 13-20 dB for sound effects, which crackle) and
    8-bit PCM for musics (31-37 dB, an audible hiss).
Writes sounds.bin (the samples) and sounds_tab.bin (the table read by main.c). Both come from the
game: they stay out of the repository.

sounds_tab.bin: u32 count, then per sound eight u32: key (prefix << 8 | code, prefix 0 for a command
alone), offset in sounds.bin, length in samples, loop start in samples (0xFFFFFFFF = none), format
(0 = 8-bit PCM, 1 = 16-bit PCM), rate in Hz, playback volume (0-255), and the mask of the
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
RATE_IN, RATE, RATE_LONG = 55555, 22050, 16000
OKI_STEP = [int(16.0 * (11.0 / 10.0) ** i) for i in range(49)]
OKI_ADJ = [-1, -1, -1, -1, 2, 4, 6, 8]


SILENT, MUSIC = set(), set()


def events():
    out = []
    for raw in open(os.path.join(here, 'sounds.txt')):
        line = raw.split('#')[0].split()
        if not line: continue
        ev = [int(v, 16) for v in line]; out.append(ev)
        comment = raw.split('#', 1)[1] if '#' in raw else ''
        if 'music:' in comment: MUSIC.add(tuple(ev)); out.pop(); continue      # made by make_music.py
        if 'silent' in comment: SILENT.add(tuple(ev))
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
                n = min(kind[1], 30 * RATE)
                x = render(ev, n / RATE + 1) if n > len(x) else x
                clips.append((ev, x[:n], None))
                print('%-6s long piece, ends at %.0f s, kept %.0f s' % (name, kind[1] / RATE, n / RATE))
        else:
            end = min(len(x), loud[-1] + RATE // 10)
            clips.append((ev, x[:end], None)); print('%-6s sound %.2f s' % (name, end / RATE))
    top = max(np.max(np.abs(c[1])) for c in clips if len(c[1]))     # the loudest sound plays at 255
    rng = np.random.default_rng(1)
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
        while len(data) % 4: data.append(0)
        off = len(data)
        cut = cuts.get(names[len(tab)], 0)
        if not len(x): tab.append((key, off, 0, 0xFFFFFFFF, 1, RATE, 0, cut)); continue
        peak = np.max(np.abs(x)); vol = max(1, int(round(255 * peak / top)))
        y = x / peak                                            # this sound at full scale
        if loop is None and len(y) <= 12 * RATE:                # short: 22050 Hz
            rate, lp = RATE, 0xFFFFFFFF
        else:                                                   # long: 16000 Hz
            n = len(y)
            y = resample_poly(y, 320, 441)[:int(round(n * RATE_LONG / RATE))]
            y = y / max(1e-9, np.max(np.abs(y)))
            rate, lp = RATE_LONG, 0xFFFFFFFF if loop is None else int(round(loop * RATE_LONG / RATE))
        pcm = np.clip(np.round(y * 32000), -32768, 32767).astype('<i2')
        data += pcm.tobytes(); tab.append((key, off, len(pcm), lp, 1, rate, vol, cut))
    open(os.path.join(root, 'sounds.bin'), 'wb').write(bytes(data))
    with open(os.path.join(root, 'sounds_tab.bin'), 'wb') as f:
        f.write(struct.pack('<I', len(tab)))
        for t in tab: f.write(struct.pack('<8I', *t))
    print('sounds.bin: %d sounds, %.1f MB' % (len(tab), len(data) / 2 ** 20))


if __name__ == '__main__':
    main()
