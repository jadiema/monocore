#!/usr/bin/env python3
"""PCM sample bank of MONOTRACKER (mtbank.bin): original sounds, all synthesised here.

usage: python3 make_bank.py [output]          (default: examples/monotracker/mtbank.bin)

Drums, basses, keys, strings, brass, choir... in 16-bit mono. The sustained sounds have a
seamless loop: their loop holds a whole number of periods of every partial, so the trackers'
envelopes can hold them as long as needed. Each sample is stored with its root note (the note
at which it plays at its own rate); the player transposes it from there.
Format (little-endian): "MTBK", u32 count, then count entries of 32 bytes
    u32 offset (from the start of the bank), u32 length (samples), u32 loop start (0xFFFFFFFF none),
    u32 rate (Hz), u8 root note (1 = C-0, 49 = C-4), u8 format (1 = 16 bits), 2 pad, char name[12]
then the samples. Needs numpy and scipy.
"""
import os, struct, sys
import numpy as np
from scipy.signal import butter, lfilter

R = 32000
rng = np.random.default_rng(1985)

def note_hz(n): return 16.3516 * 2 ** ((n - 1) / 12)   # n = 1: C-0
C2, C3, C4 = 25, 37, 49

def lp(x, f, o=2): b, a = butter(o, min(f / (R / 2), 0.99)); return lfilter(b, a, x)
def hp(x, f, o=2): b, a = butter(o, f / (R / 2), btype='high'); return lfilter(b, a, x)
def bp(x, lo, hi): b, a = butter(2, [lo / (R / 2), hi / (R / 2)], btype='band'); return lfilter(b, a, x)
def t_(d): return np.arange(int(d * R)) / R
def expd(d, tau): return np.exp(-t_(d) / tau)
def noise(d): return rng.uniform(-1, 1, int(d * R))

# ---------------------------------------------------------------- one-shot sounds
def kick():
    t = t_(0.45); f = 45 + 110 * np.exp(-t / 0.03)
    ph = 2 * np.pi * np.cumsum(f) / R
    return np.sin(ph) * np.exp(-t / 0.16) + 0.4 * lp(noise(0.45), 3000) * np.exp(-t / 0.004)
def snare():
    t = t_(0.32)
    tone = np.sin(2 * np.pi * 185 * t) * np.exp(-t / 0.05) + 0.5 * np.sin(2 * np.pi * 330 * t) * np.exp(-t / 0.03)
    return 0.7 * tone + bp(noise(0.32), 1500, 8000) * np.exp(-t / 0.08) * 1.6
def clap():
    t = t_(0.4); env = np.zeros_like(t)
    for k, s in enumerate((0, 0.011, 0.023)): env += (t >= s) * np.exp(-(t - s).clip(0) / 0.006) * (t >= s)
    env += (t >= 0.03) * np.exp(-(t - 0.03).clip(0) / 0.09)
    return bp(noise(0.4), 900, 2600) * env * 2
def metal(d):
    t = t_(d); x = np.zeros_like(t)
    for f in (205.3, 304.4, 369.6, 522.7, 540.0, 800.0):
        x += np.sign(np.sin(2 * np.pi * f * t))
    return hp(x + 2 * noise(d), 6500, 4)
def hh_closed(): return metal(0.09) * expd(0.09, 0.018)
def hh_open(): return metal(0.5) * expd(0.5, 0.16)
def crash(): t = t_(1.8); return hp(metal(1.8) + 3 * noise(1.8), 3500) * np.exp(-t / 0.55)
def tom():
    t = t_(0.55); f = 98 * (1 + 0.6 * np.exp(-t / 0.05))
    return np.sin(2 * np.pi * np.cumsum(f) / R) * np.exp(-t / 0.2) + 0.2 * lp(noise(0.55), 2000) * np.exp(-t / 0.02)
def rim(): t = t_(0.06); return bp(noise(0.06), 1500, 5000) * np.exp(-t / 0.006) + np.sin(2 * np.pi * 1700 * t) * np.exp(-t / 0.01)
def cowbell():
    t = t_(0.35); x = np.sign(np.sin(2 * np.pi * 540 * t)) + np.sign(np.sin(2 * np.pi * 800 * t))
    return bp(x, 400, 3000) * (0.6 * np.exp(-t / 0.02) + 0.4 * np.exp(-t / 0.12))

def epiano(f):
    t = t_(1.8); mod = 1.8 * np.exp(-t / 0.3) * np.sin(2 * np.pi * f * t)
    return np.sin(2 * np.pi * f * t + mod) * np.exp(-t / 0.7) + 0.15 * np.sin(2 * np.pi * 14 * f * t) * np.exp(-t / 0.02)
def marimba(f):
    t = t_(0.8)
    return np.sin(2 * np.pi * f * t) * np.exp(-t / 0.25) + 0.35 * np.sin(2 * np.pi * 3.93 * f * t) * np.exp(-t / 0.06) \
        + 0.1 * np.sin(2 * np.pi * 9.2 * f * t) * np.exp(-t / 0.015)
def bell(f):
    t = t_(2.5); x = np.zeros_like(t)
    for r, a, d in ((0.56, 0.6, 1.2), (1, 1, 1.0), (1.19, 0.5, 0.7), (2, 0.6, 0.6), (2.74, 0.4, 0.4), (3, 0.3, 0.3), (4.07, 0.25, 0.2)):
        x += a * np.sin(2 * np.pi * f * r * t) * np.exp(-t / d)
    return x
def guitar(f, dur=1.6):
    n = int(dur * R); d = int(round(R / f)); buf = lp(rng.uniform(-1, 1, d), 5000)
    y = np.zeros(n); y[:d] = buf
    for i in range(d, n): y[i] = 0.996 * 0.5 * (y[i - d] + y[i - d - 1 if i - d - 1 >= 0 else 0])
    return hp(y, 60)
def orch_hit():
    t = t_(0.7); x = np.zeros_like(t)
    for n in (C3, C3 + 7, C4, C4 + 4, C4 + 7):
        f = note_hz(n); x += (2 * ((t * f) % 1) - 1) + 0.5 * (2 * ((t * f * 1.004) % 1) - 1)
    x = lp(x, 4000) + 0.6 * bp(noise(0.7), 300, 3000) * np.exp(-t / 0.03)
    return x * (0.5 * np.exp(-t / 0.08) + 0.5 * np.exp(-t / 0.3))

# ---------------------------------------------------------------- looped sounds
def looped(root, partials, voices=((0, 1.0),), loop_s=0.5, attack=0.08, bright=None):
    """partials: list of (harmonic, amplitude); voices: (detune in loop cycles, gain).
    Returns (signal, loop start, rate): attack part then one loop, all partials whole in the loop"""
    f = note_hz(root)
    L = int(loop_s * R); k = max(1, round(f * L / R)); fl = k * R / L        # loop frequency
    t = np.arange(2 * L) / R
    x = np.zeros(2 * L)
    for dv, g in voices:
        for h, a in partials:
            cyc = h * k + dv                                                   # whole cycles per loop
            x += g * a * np.sin(2 * np.pi * cyc * R / L * t + rng.uniform(0, 2 * np.pi))
    env = np.ones(2 * L); na = int(attack * R); env[:na] = np.linspace(0, 1, na) ** 1.5
    x *= env
    if bright is not None:
        x[:L] += bright(L)
    return x, L, R * f / fl

def saw_partials(n, roll=1.0): return [(h, (1 / h) ** roll) for h in range(1, n + 1)]
def bass():
    return looped(C2, [(h, (1 / h) * (1 if h < 6 else 0.4 ** (h - 5))) for h in range(1, 20)], attack=0.005, loop_s=0.25)
def brass():
    return looped(C4, [(h, (1 / h) * np.exp(-(h - 1) / 6)) for h in range(1, 18)], voices=((0, 1), (1, 0.5)), attack=0.06)
def strings():
    return looped(C4, saw_partials(14, 1.2), voices=((0, 1), (1, 0.8), (-1, 0.8), (2, 0.5)), attack=0.15, loop_s=0.75)
def organ():
    return looped(C4, [(1, 1), (2, 0.8), (3, 0.6), (4, 0.4), (6, 0.25), (8, 0.3)], attack=0.01, loop_s=0.25)
def choir():
    fo = ((800, 1.0, 80), (1150, 0.5, 90), (2900, 0.2, 120))
    def amp(h):
        f = h * note_hz(C4)
        return sum(a / (1 + ((f - c) / w) ** 2) for c, a, w in fo) + 0.05 / h
    return looped(C4, [(h, amp(h)) for h in range(1, 14)], voices=((0, 1), (1, 0.7), (-1, 0.7)), attack=0.2, loop_s=0.75)
def flute():
    def breath(L): return 0.15 * bp(rng.uniform(-1, 1, L), 1500, 6000) * np.exp(-np.arange(L) / R / 0.05)
    return looped(C4 + 12, [(1, 1), (2, 0.12), (3, 0.05)], attack=0.07, bright=breath, loop_s=0.25)
def pad():
    return looped(C4, saw_partials(8, 1.6), voices=((0, 1), (1, 1), (-1, 1), (3, 0.6), (-2, 0.6)), attack=0.3, loop_s=1.0)

SAMPLES = [
    ('KICK', kick, C4), ('SNARE', snare, C4), ('CLAP', clap, C4), ('HH FERME', hh_closed, C4),
    ('HH OUVERT', hh_open, C4), ('CRASH', crash, C4), ('TOM', tom, 32), ('RIM', rim, C4),
    ('COWBELL', cowbell, C4),
    ('BASSE', bass, None), ('PIANO EL', lambda: epiano(note_hz(C4)), C4),
    ('MARIMBA', lambda: marimba(note_hz(C4 + 12)), C4 + 12), ('CLOCHE', lambda: bell(note_hz(C4 + 12)), C4 + 12),
    ('GUITARE', lambda: guitar(note_hz(C3)), C3), ('CUIVRES', brass, None), ('CORDES', strings, None),
    ('ORGUE', organ, None), ('CHOEUR', choir, None), ('FLUTE', flute, None), ('NAPPE', pad, None),
    ('ORCH HIT', orch_hit, C3),
]
ROOTS = {'BASSE': C2, 'CUIVRES': C4, 'CORDES': C4, 'ORGUE': C4, 'CHOEUR': C4, 'FLUTE': C4 + 12, 'NAPPE': C4}

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '../../examples/monotracker/mtbank.bin')
    entries, blobs = [], b''
    base = 8 + 32 * len(SAMPLES)
    for name, fn, root in SAMPLES:
        r = fn()
        if isinstance(r, tuple): x, loop, rate = r; root = ROOTS[name]
        else: x, loop, rate = r, 0xFFFFFFFF, R
        x = np.asarray(x, dtype=np.float64); x -= x.mean() if loop != 0xFFFFFFFF else 0
        x = x / (np.max(np.abs(x)) + 1e-9) * 0.9
        pcm = np.round(x * 32767).astype('<i2').tobytes()
        entries.append(struct.pack('<IIIIBB2x12s', base + len(blobs), len(x), loop, int(round(rate)), root, 1,
                                   name.encode('ascii')[:12]))
        blobs += pcm
        if len(blobs) % 4: blobs += b'\0' * (4 - len(blobs) % 4)
        print('%2d %-10s %6d samples %6.3f s  loop %s  rate %d  root %d' % (len(entries) - 1, name, len(x), len(x) / R,
              '-' if loop == 0xFFFFFFFF else loop, int(round(rate)), root))
    data = b'MTBK' + struct.pack('<I', len(SAMPLES)) + b''.join(entries) + blobs
    open(out, 'wb').write(data)
    print('%s: %d bytes' % (out, len(data)))

if __name__ == '__main__':
    main()
