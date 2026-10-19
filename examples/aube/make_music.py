#!/usr/bin/env python3
"""Music of the AUBE start-up screen: a short orchestral piece (about 28 s), synthesised here.

Strings (detuned saw waves, filtered), choir (formant filters on a saw), horns, celesta, harp,
timpani and a cymbal swell, in a stereo hall (convolution with a synthetic reverb).
D minor, slow, rising to D major when the logo appears (12 s), then a long fading chord.
Writes music_l.raw and music_r.raw: signed 16-bit mono at 22050 Hz, one per side.
Needs numpy and scipy (python3 -m pip install numpy scipy, or apt install python3-numpy python3-scipy).
"""
import numpy as np
from scipy.signal import butter, lfilter, fftconvolve

SR = 22050
DUR = 30.0
N = int(SR * DUR)
rng = np.random.default_rng(1985)
L = np.zeros(N); R = np.zeros(N)

NOTE = {'C': -9, 'C#': -8, 'Db': -8, 'D': -7, 'D#': -6, 'Eb': -6, 'E': -5, 'F': -4, 'F#': -3, 'Gb': -3,
        'G': -2, 'G#': -1, 'Ab': -1, 'A': 0, 'A#': 1, 'Bb': 1, 'B': 2}
def hz(n):
    name, octv = n[:-1], int(n[-1])
    return 440.0 * 2 ** ((NOTE[name] + 12 * (octv - 4)) / 12)

def env(n, a, r, sustain=1.0):
    """attack / release envelope over n samples (raised cosine edges)"""
    e = np.full(n, sustain)
    na, nr = max(1, int(a * SR)), max(1, int(r * SR))
    na = min(na, n); nr = min(nr, n)
    e[:na] *= 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, na))
    e[n - nr:] *= 0.5 + 0.5 * np.cos(np.linspace(0, np.pi, nr))
    return e

def lowpass(x, f, order=2):
    b, a = butter(order, min(f / (SR / 2), 0.99))
    return lfilter(b, a, x)

def bandpass(x, f, q):
    b, a = butter(2, [max(f / q, 20) / (SR / 2), min(f * q, SR / 2 * 0.95) / (SR / 2)], btype='band')
    return lfilter(b, a, x)

def saw(f, n, phase=None):
    t = np.arange(n) / SR
    ph = (f * t + (rng.random() if phase is None else phase)) % 1.0
    return 2 * ph - 1

def add(sig, start, pan=0.0, gain=1.0):
    i = int(start * SR)
    if i >= N: return
    sig = sig[:N - i] * gain
    L[i:i + len(sig)] += sig * np.cos((pan + 1) * np.pi / 4)
    R[i:i + len(sig)] += sig * np.sin((pan + 1) * np.pi / 4)

# ---------------------------------------------------------------- instruments
def strings(note, start, length, gain=0.12, pan=0.0, a=1.5, r=2.0, bright=1.0):
    n = int((length + r) * SR); f = hz(note)
    t = np.arange(n) / SR
    vib = 1 + 0.003 * np.sin(2 * np.pi * 5.2 * t + rng.random() * 6) * np.clip(t / 1.5, 0, 1)
    x = np.zeros(n)
    for d in (-0.012, -0.005, 0.0, 0.006, 0.013):          # an ensemble: detuned voices
        ph = np.cumsum(f * (1 + d) * vib / SR) + rng.random()
        x += 2 * (ph % 1.0) - 1
    x = lowpass(x, min(900 + f * 3.0 * bright, 6000), 2)
    add(x * env(n, a, r), start, pan, gain / 5)

def choir(note, start, length, gain=0.10, pan=0.0, a=2.0, r=2.5):
    n = int((length + r) * SR); f = hz(note)
    t = np.arange(n) / SR
    x = np.zeros(n)
    for d in (-0.008, 0.0, 0.009):
        ph = np.cumsum(f * (1 + d) * (1 + 0.004 * np.sin(2 * np.pi * 4.6 * t + rng.random() * 6)) / SR) + rng.random()
        x += 2 * (ph % 1.0) - 1
    y = bandpass(x, 730, 1.35) * 1.0 + bandpass(x, 1090, 1.3) * 0.5 + bandpass(x, 2440, 1.25) * 0.25   # "aah"
    add(y * env(n, a, r), start, pan, gain)

def horn(note, start, length, gain=0.10, pan=0.0, a=0.35, r=1.2):
    n = int((length + r) * SR); f = hz(note)
    t = np.arange(n) / SR
    ph = np.cumsum(f * (1 + 0.002 * np.sin(2 * np.pi * 4.8 * t)) / SR)
    x = 2 * (ph % 1.0) - 1 + 0.6 * (2 * ((ph + 0.5) % 1.0) - 1) * 0
    e = env(n, a, r)
    x = lowpass(x * e, 400 + 1400 * float(np.mean(e[:int(a * SR) + 1])) + f, 2)
    add(lowpass(x, 2200, 1) * e, start, pan, gain)

def celesta(note, start, gain=0.07, pan=0.0):
    n = int(3.5 * SR); f = hz(note); t = np.arange(n) / SR
    x = (np.sin(2 * np.pi * f * t) * np.exp(-t * 1.6) + 0.5 * np.sin(2 * np.pi * f * 2.0 * t) * np.exp(-t * 3.0)
         + 0.25 * np.sin(2 * np.pi * f * 4.07 * t) * np.exp(-t * 6.0))
    x *= np.minimum(1, t * 400)
    add(x, start, pan, gain)

def harp(note, start, gain=0.09, pan=0.0):
    f = hz(note); n = int(3.0 * SR); p = int(SR / f)
    buf = rng.uniform(-1, 1, p); out = np.zeros(n)
    for i in range(n):                                       # Karplus-Strong plucked string
        v = buf[i % p]; out[i] = v
        buf[i % p] = 0.4985 * (v + buf[(i + 1) % p])
    add(lowpass(out, 3500, 1), start, pan, gain)

def timpani(note, start, gain=0.35, pan=0.0, decay=1.6):
    n = int(3.0 * SR); f = hz(note); t = np.arange(n) / SR
    ph = np.cumsum(f * (1 + 0.15 * np.exp(-t * 25)) / SR)
    x = np.sin(2 * np.pi * ph) * np.exp(-t * decay) + 0.4 * np.sin(2 * np.pi * ph * 1.5) * np.exp(-t * 3)
    x += lowpass(rng.uniform(-1, 1, n), 600, 2) * np.exp(-t * 18) * 0.6
    add(x, start, pan, gain)

def roll(note, start, length, g0, g1):
    k = 0.0
    while k < length:
        g = g0 + (g1 - g0) * (k / length) ** 1.5
        timpani(note, start + k, g * (0.85 + 0.3 * rng.random()), 0.0, 3.0)
        k += 0.075 + 0.02 * rng.random()

def cymbal_swell(start, length, gain=0.06):
    n = int((length + 3) * SR); t = np.arange(n) / SR
    x = rng.uniform(-1, 1, n)
    x = lfilter(*butter(2, [3000 / (SR / 2), 9000 / (SR / 2)], btype='band'), x)
    e = np.where(t < length, (t / length) ** 2.5, np.exp(-(t - length) * 1.2))
    add(x * e, start, -0.3, gain); add(x[::-1][:n] * e * 0.5, start, 0.3, gain)

# ---------------------------------------------------------------- the piece (seconds)
# 0-4 s: D minor, low strings and choir
for n, p in (('D2', -0.3), ('A2', 0.3), ('D3', -0.1), ('F3', 0.2)): strings(n, 0.2, 4.0, 0.13, p, a=2.5)
choir('A3', 1.5, 3.0, 0.07, -0.2); choir('D4', 2.0, 2.5, 0.06, 0.2)
# 4-8 s: B flat, horn call A3 -> D4
for n, p in (('Bb1', -0.3), ('F2', 0.3), ('Bb2', -0.1), ('D3', 0.15), ('F3', 0.3)): strings(n, 4.0, 4.0, 0.13, p, a=1.2)
choir('F4', 4.2, 3.6, 0.06, 0.1); choir('D4', 4.2, 3.6, 0.05, -0.2)
horn('A3', 4.5, 1.4, 0.10, -0.2); horn('D4', 6.0, 2.0, 0.11, -0.2)
for k, n in enumerate(('D5', 'F5', 'A5', 'D6')): celesta(n, 5.0 + 0.45 * k, 0.05, 0.4)
# 8-12 s: G minor, then A (sus4 -> major), timpani roll, cymbal swell
for n, p in (('G1', -0.3), ('D2', 0.3), ('G2', -0.1), ('Bb2', 0.2), ('D3', 0.3)): strings(n, 8.0, 2.0, 0.14, p, a=0.8, r=1.0)
for n, p in (('A1', -0.3), ('E2', 0.3), ('A2', -0.1), ('D3', 0.2), ('E3', 0.3)): strings(n, 10.0, 1.0, 0.15, p, a=0.5, r=0.6, bright=1.3)
for n, p in (('A1', -0.3), ('E2', 0.3), ('A2', -0.1), ('C#3', 0.2), ('E3', 0.3)): strings(n, 11.0, 1.0, 0.16, p, a=0.3, r=0.4, bright=1.5)
choir('Bb4', 8.0, 2.0, 0.06); choir('A4', 10.0, 2.0, 0.07)
horn('G3', 8.2, 1.6, 0.09, -0.2); horn('A3', 10.0, 2.0, 0.11, -0.2); horn('E4', 10.0, 2.0, 0.08, 0.2)
roll('A1', 9.5, 2.45, 0.04, 0.26)
cymbal_swell(9.0, 3.0, 0.03)
# 12 s: D major, the whole orchestra (the logo appears)
timpani('D2', 12.0, 0.45); timpani('A1', 12.0, 0.25)
for n, p in (('D1', 0.0), ('D2', -0.3), ('A2', 0.3), ('D3', -0.2), ('F#3', 0.25), ('A3', -0.1), ('D4', 0.15), ('F#4', 0.3), ('A4', -0.3)):
    strings(n, 12.0, 6.5, 0.16, p, a=0.15, r=3.5, bright=1.4)
choir('A4', 12.0, 7.0, 0.09, -0.2); choir('D5', 12.0, 7.0, 0.07, 0.2); choir('F#4', 12.2, 6.8, 0.06, 0.0)
horn('D4', 12.0, 1.5, 0.12, -0.25); horn('F#4', 13.4, 1.3, 0.11, -0.25); horn('A4', 14.6, 3.0, 0.12, -0.25)
horn('A3', 12.0, 6.0, 0.08, 0.25); horn('D3', 12.0, 6.0, 0.08, 0.1)
for k, n in enumerate(('D3', 'F#3', 'A3', 'D4', 'F#4', 'A4', 'D5', 'F#5', 'A5', 'D6')): harp(n, 12.05 + 0.07 * k, 0.07, -0.5 + 0.1 * k)
# 18-30 s: G/D then D add9, slowly fading, celesta glints
for n, p in (('D2', -0.3), ('G2', 0.3), ('B2', -0.1), ('D3', 0.2), ('G3', 0.3), ('B3', -0.2)): strings(n, 18.0, 3.5, 0.11, p, a=1.2, r=2.0)
choir('B4', 18.0, 3.5, 0.06); choir('G4', 18.0, 3.5, 0.05, 0.3)
for n, p in (('D2', -0.3), ('A2', 0.3), ('D3', -0.1), ('E3', 0.2), ('F#3', 0.3), ('A3', -0.2)): strings(n, 21.5, 4.0, 0.10, p, a=1.5, r=4.0)
choir('A4', 21.5, 4.0, 0.05, -0.1); choir('E4', 21.5, 4.0, 0.04, 0.2)
horn('F#4', 18.5, 2.5, 0.07, -0.2); horn('E4', 21.5, 3.0, 0.06, -0.2)
for k, (n, t) in enumerate((('A5', 19.0), ('F#5', 19.6), ('D6', 20.4), ('E6', 22.5), ('A5', 23.3), ('F#6', 24.4), ('D6', 25.6))):
    celesta(n, t, 0.045, 0.5 if k % 2 else -0.4)
timpani('D2', 21.5, 0.15)

# ---------------------------------------------------------------- hall
def impulse(seconds, seed):
    r = np.random.default_rng(seed); n = int(seconds * SR); t = np.arange(n) / SR
    ir = r.standard_normal(n) * np.exp(-t * 6.9 / seconds)          # RT60 = seconds
    ir = lowpass(ir, 5000, 1)
    ir[:int(0.02 * SR)] *= np.linspace(0, 1, int(0.02 * SR))
    return ir / np.sqrt(np.sum(ir ** 2))
wl = fftconvolve(L, impulse(3.2, 1))[:N]; wr = fftconvolve(R, impulse(3.2, 2))[:N]
L = 0.72 * L + 0.55 * wl; R = 0.72 * R + 0.55 * wr
fade = np.ones(N); nf = int(4.0 * SR); fade[N - nf:] = np.linspace(1, 0, nf) ** 2
L *= fade; R *= fade
peak = max(np.max(np.abs(L)), np.max(np.abs(R)))
L = np.tanh(L / peak * 2.2) / np.tanh(2.2) * 0.89; R = np.tanh(R / peak * 2.2) / np.tanh(2.2) * 0.89
(np.round(L * 32767).astype('<i2')).tofile('music_l.raw')
(np.round(R * 32767).astype('<i2')).tofile('music_r.raw')
print('music: %.1f s at %d Hz, peak %.3f' % (DUR, SR, peak))
