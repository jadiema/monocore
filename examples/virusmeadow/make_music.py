#!/usr/bin/env python3
"""Music of VIRUS MEADOW: an original loop (48 s) in the style of mid-80s English post-punk
pastoral rock - ringing arpeggiated guitar, tremolo-picked lead, melodic bass high in the mix,
rolling floor toms, an organ drone and wind over the meadow, all in a large damp reverb.

E minor (aeolian), 120 bpm, 24 bars: intro (4), arpeggios (8), tremolo melody (8), return (4).
The piece is rendered twice and the second pass is kept, so that the reverb and the ringing
notes of the end flow into the beginning: the loop has no seam.
Writes music_l.raw and music_r.raw: signed 16-bit mono at 22050 Hz, one per side.
Needs numpy and scipy (python3 -m pip install numpy scipy).
"""
import numpy as np
from scipy.signal import butter, lfilter, fftconvolve

SR = 22050
BPM = 120
STEP = 60.0 / BPM / 4                          # one sixteenth
BAR = 16 * STEP
BARS = 24
LOOP = int(round(BARS * BAR * SR))
N = 2 * LOOP + SR * 4                          # two passes and room for the tails
rng = np.random.default_rng(1986)

NOTE = {'C': -9, 'C#': -8, 'D': -7, 'D#': -6, 'E': -5, 'F': -4, 'F#': -3, 'G': -2, 'G#': -1,
        'A': 0, 'A#': 1, 'B': 2}
def hz(n):
    return 440.0 * 2 ** ((NOTE[n[:-1]] + 12 * (int(n[-1]) - 4)) / 12)

def lowpass(x, f, order=2):
    b, a = butter(order, min(f / (SR / 2), 0.99)); return lfilter(b, a, x)
def highpass(x, f, order=2):
    b, a = butter(order, f / (SR / 2), btype='high'); return lfilter(b, a, x)
def bandpass(x, lo, hi):
    b, a = butter(2, [lo / (SR / 2), hi / (SR / 2)], btype='band'); return lfilter(b, a, x)

def add(buf, t, x):
    i = int(round(t * SR))
    if i >= len(buf): return
    x = x[:len(buf) - i]; buf[i:i + len(x)] += x

# ---------------------------------------------------------------- instruments
def pluck(f, dur, bright=0.5, t60=2.0, S=0.06):
    """Karplus-Strong string: a burst of filtered noise in a tuned feedback loop, ringing about
    t60 s; the loop filter (1 - S) + S z^-1 is light (small S), for a bright, jangling string"""
    w = 2 * np.pi * f / SR
    h = np.sqrt((1 - S) ** 2 + S ** 2 + 2 * S * (1 - S) * np.cos(w))  # loop filter gain at f
    decay = min(0.9998, 10 ** (-3 / (f * t60)) / h)
    n = int(dur * SR); q = SR / f - S; d = int(q); frac = q - d     # tuned: the filter delays by S
    burst = lowpass(rng.uniform(-1, 1, d + 2), 1500 + 6000 * bright)
    burst -= burst.mean()
    x = np.zeros(n); x[:d + 2] = burst
    taps = np.convolve([1 - S, S], [1 - frac, frac]) * decay       # loop filter and fractional delay
    a = np.zeros(d + 3); a[0] = 1; a[d:d + 3] -= taps
    y = highpass(lfilter([1.0], a, x), 40)                     # no DC build-up in the loop
    y *= np.minimum(1, np.linspace(dur * 40, 0, n))      # short release at the very end
    return y / (np.max(np.abs(y)) + 1e-9)

def bass_note(f, dur):
    n = int(dur * SR); t = np.arange(n) / SR
    saw = 2 * ((t * f) % 1) - 1
    sq = np.sign(np.sin(2 * np.pi * f * t))
    x = 0.7 * saw + 0.3 * sq
    cut = 300 + 1700 * np.exp(-t * 9)                    # pick attack, then round
    y = np.zeros(n); z = 0.0
    # one-pole lowpass with a moving cut-off (sample loop, short notes)
    k = 1 - np.exp(-2 * np.pi * cut / SR)
    for i in range(n):
        z += k[i] * (x[i] - z); y[i] = z
    e = np.exp(-t * 2.2) * np.minimum(1, t * 400) * np.minimum(1, (dur - t) * 60)
    return y * e

def tom(f0=105, f1=72, dur=0.55):
    n = int(dur * SR); t = np.arange(n) / SR
    f = f1 + (f0 - f1) * np.exp(-t * 14)
    ph = 2 * np.pi * np.cumsum(f) / SR
    body = np.sin(ph) * np.exp(-t * 6.5)
    skin = bandpass(rng.uniform(-1, 1, n), 200, 2500) * np.exp(-t * 40) * 0.35
    return body + skin

def kick():
    n = int(0.3 * SR); t = np.arange(n) / SR
    f = 44 + 70 * np.exp(-t * 30)
    return np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 11)

def snare():
    n = int(0.3 * SR); t = np.arange(n) / SR
    noise = bandpass(rng.uniform(-1, 1, n), 900, 7000) * np.exp(-t * 16)
    tone = np.sin(2 * np.pi * 185 * t) * np.exp(-t * 22)
    return 0.8 * noise + 0.5 * tone

def shaker():
    n = int(0.06 * SR); t = np.arange(n) / SR
    return highpass(rng.uniform(-1, 1, n), 6000) * np.exp(-t * 70)

def organ(freqs, dur):
    n = int(dur * SR); t = np.arange(n) / SR
    x = np.zeros(n)
    for f in freqs:
        for det in (-0.12, 0.0, 0.13):
            x += 2 * ((t * f * (1 + det / 100)) % 1) - 1
    x = lowpass(x, 700, 2)
    e = np.minimum(1, t / 0.8) * np.minimum(1, (dur - t) / 0.8)
    return x * e / len(freqs) / 3

# ---------------------------------------------------------------- the score (original)
CHORDS = ['Em', 'Em', 'D', 'Em',                                       # intro
          'Em', 'Em', 'D', 'D', 'C', 'C', 'Bm', 'Bm',                  # arpeggios
          'C', 'C', 'G', 'G', 'Am', 'Am', 'B', 'B',                    # tremolo melody
          'Em', 'D', 'C', 'B']                                         # return
VOICING = {'Em': ['E4', 'G4', 'B4', 'E5'], 'D': ['D4', 'F#4', 'A4', 'D5'], 'C': ['C4', 'E4', 'G4', 'B4'],
           'Bm': ['B3', 'D4', 'F#4', 'B4'], 'G': ['G3', 'B3', 'D4', 'G4'], 'Am': ['A3', 'C4', 'E4', 'A4'],
           'B': ['B3', 'D#4', 'F#4', 'B4']}
ROOT = {'Em': 'E2', 'D': 'D2', 'C': 'C2', 'Bm': 'B1', 'G': 'G2', 'Am': 'A2', 'B': 'B1'}
FIFTH = {'Em': 'B2', 'D': 'A2', 'C': 'G2', 'Bm': 'F#2', 'G': 'D3', 'Am': 'E3', 'B': 'F#2'}
PEDAL = {'B': 'B4'}                                                   # ringing open string, E5 elsewhere
ARP = [0, 2, 4, 2, 1, 3, 4, 3, 0, 2, 4, 2, 1, 3, 4, 1]               # 4 = pedal
MELODY = {12: ['G5', 'E5'], 13: ['D5', 'E5'], 14: ['D5', 'B4'], 15: ['A4', 'B4'],
          16: ['C5', 'E5'], 17: ['A5', 'G5'], 18: ['F#5', 'D#5'], 19: ['F#5', 'F#5']}

def semis(a, b):
    return round(12 * np.log2(hz(b) / hz(a)))

GL = np.zeros(N); GR = np.zeros(N)                                   # guitars (stereo, chorus)
BS = np.zeros(N); DR = np.zeros(N); PD = np.zeros(N)                 # bass, drums, pad
LEAD = np.zeros(N)
cache = {}
def cached(key, fn):
    if key not in cache: cache[key] = fn()
    return cache[key]

for p in range(2):                                                   # two passes (see above)
    for b, ch in enumerate(CHORDS):
        t0 = (p * BARS + b) * BAR
        nxt = CHORDS[(b + 1) % BARS]
        v = VOICING[ch] + [PEDAL.get(ch, 'E5')]
        intro = b < 4
        trem = 12 <= b < 20
        # guitar: ringing arpeggio (quieter under the tremolo melody)
        for s, k in enumerate(ARP):
            if intro and b < 2 and s % 2: continue
            vel = (0.85 if s % 4 == 0 else 0.6) * (0.45 if trem else 1.0) * rng.uniform(0.9, 1.05)
            note = v[k]
            x = cached(('g', note), lambda: pluck(hz(note), 1.8, 0.55, 2.6))
            pan = 0.35 + 0.3 * (k / 4)
            add(GL, t0 + s * STEP + rng.uniform(0, 0.006), x * vel * (1 - pan))
            add(GR, t0 + s * STEP + rng.uniform(0, 0.006), x * vel * pan)
        # lead: tremolo picking, sixteenths, on the melody
        if trem:
            for s in range(16):
                note = MELODY[b][s // 8]
                vel = (1.0 if s % 2 == 0 else 0.7) * (1.1 if s % 8 == 0 else 1.0)
                x = cached(('l', note), lambda: pluck(hz(note), 0.4, 0.8, 0.9))
                add(LEAD, t0 + s * STEP, x * vel)
        if b in (21, 23):                                            # echoes of the melody in the return
            for s, note in enumerate(['B5', 'A5', 'G5', 'F#5'] if b == 21 else ['F#5', 'D#5', 'F#5', 'B5']):
                for r in range(4):
                    x = cached(('l', note), lambda: pluck(hz(note), 0.4, 0.8, 0.9))
                    add(LEAD, t0 + (s * 4 + r) * STEP, x * (0.6 if r % 2 else 0.8) * 0.7)
        # bass: melodic eighths, a passing note into the next chord
        if b >= 2 or p == 1:
            r, f = ROOT[ch], FIFTH[ch]
            up = ROOT[ch][:-1] + str(int(ROOT[ch][-1]) + 1)
            line = [r, r, f, r, up, r, f, r]
            nr = ROOT[nxt]
            if nr != r:                                              # approach from a step above/below
                d = semis(r, nr); line[7] = r if abs(d) > 2 else f
            for e, note in enumerate(line):
                x = cached(('b', note), lambda: bass_note(hz(note), STEP * 2 * 0.95))
                add(BS, t0 + e * 2 * STEP, x * (1.0 if e % 2 == 0 else 0.8))
        # drums: rolling floor toms, kick, a distant snare, a shaker
        for s in range(16):
            t = t0 + s * STEP
            if s in (0, 10) and not (intro and b < 2): add(DR, t, cached('k', kick) * 0.9)
            if s in (2, 6, 14) or (s == 11 and b % 2): add(DR, t, cached('tom', tom) * (0.7 if s != 14 else 0.85))
            if s in (4, 12) and not intro: add(DR, t, cached('sn', snare) * 0.45)
            if s % 2 == 0 and not intro: add(DR, t, cached('sh', shaker) * (0.12 if s % 4 else 0.07))
        if b in (11, 19, 23):                                        # tom fill at the end of a section
            for s in (12, 13, 14, 15):
                f0 = 140 - (s - 12) * 15
                add(DR, t0 + s * STEP, tom(f0, f0 * 0.7, 0.45) * 0.75)
        # organ drone: the chord, low and soft, two bars at a time
        if b % 2 == 0:
            fr = [hz(n) / 2 for n in VOICING[ch][:3]]
            add(PD, t0, organ(fr, BAR * 2 + 0.4))

# wind over the meadow: filtered noise with a slow swell (periodic over the loop)
t = np.arange(N) / SR
wind = lowpass(highpass(rng.uniform(-1, 1, N), 250), 900) * (0.6 + 0.4 * np.sin(2 * np.pi * t / (BARS * BAR / 3)))

# ---------------------------------------------------------------- effects and mix
def reverb_ir(dur, decay, seed):
    r = np.random.default_rng(seed); n = int(dur * SR); tt = np.arange(n) / SR
    ir = r.uniform(-1, 1, n) * np.exp(-tt * decay)
    ir = lowpass(ir, 3500); ir[0] = 0
    return ir / np.sqrt(np.sum(ir ** 2))

def chorus(x, depth=0.004, rate=0.7, phase=0.0):
    n = len(x); tt = np.arange(n) / SR
    delay = (0.012 + depth * (1 + np.sin(2 * np.pi * rate * tt + phase)) / 2) * SR
    idx = np.arange(n) - delay
    i0 = np.clip(idx.astype(int), 0, n - 1); fr = idx - np.floor(idx)
    return x[i0] * (1 - fr) + x[np.clip(i0 + 1, 0, n - 1)] * fr

def delay(x, sec, fb, mix):
    d = int(sec * SR); y = x.copy()
    for k in range(1, 5):
        y[d * k:] += x[:-d * k] * (fb ** k) * mix
    return y

GL = 0.6 * GL + 0.4 * chorus(GL, phase=0.0); GR = 0.6 * GR + 0.4 * chorus(GR, phase=2.0)
dot8 = STEP * 3                                                     # dotted-eighth echo
GL = delay(GL, dot8, 0.45, 0.5); GR = delay(GR, dot8 * 1.02, 0.45, 0.5)
LEAD = lowpass(LEAD, 5000)
LL = delay(LEAD, dot8, 0.5, 0.55); LR = delay(LEAD, dot8 * 2, 0.5, 0.45)
BS = lowpass(BS, 2200)

dryL = 0.30 * GL + 0.30 * LL + 0.42 * BS + 0.50 * DR + 0.22 * PD + 0.05 * wind
dryR = 0.30 * GR + 0.30 * LR + 0.42 * BS + 0.50 * DR + 0.22 * PD + 0.05 * np.roll(wind, SR)
sendL = 0.45 * GL + 0.5 * LL + 0.05 * BS + 0.55 * DR + 0.3 * PD
sendR = 0.45 * GR + 0.5 * LR + 0.05 * BS + 0.55 * DR + 0.3 * PD
irL, irR = reverb_ir(3.2, 1.9, 1), reverb_ir(3.2, 1.9, 2)
wetL = fftconvolve(sendL, irL)[:N]; wetR = fftconvolve(sendR, irR)[:N]
L = dryL + 0.28 * wetL; R = dryR + 0.28 * wetR

L = L[LOOP:2 * LOOP]; R = R[LOOP:2 * LOOP]                           # the second pass: no seam
peak = max(np.max(np.abs(L)), np.max(np.abs(R)))
L = np.tanh(L / peak * 1.2) * 0.85; R = np.tanh(R / peak * 1.2) * 0.85   # gentle tape-like saturation
for name, x in (('music_l.raw', L), ('music_r.raw', R)):
    np.round(x * 32767).astype('<i2').tofile(name)
print('%d samples (%.1f s) per side' % (LOOP, LOOP / SR))
