#!/usr/bin/env python3
"""Original musics of the port, in an arcade style (marimba, brass, bass, lively drums), synthesised
here like the music of AUBE. They replace the musics of the game, which its Z80 driver plays from a
state that pre-rendered clips cannot follow; a 4-player version will change the game anyway.

usage: python3 tools/sound/make_music.py      writes music.bin and music_tab.bin (out of git)
Each music loops: it is rendered twice in a row and the second pass is kept, so that its end joins
its beginning, reverb included, and the console loops it in hardware (LOOP = 0).
Signed 16-bit, 22050 Hz, mono (the left and right offsets of the table are the same: stereo would
double the size, and the ROM must stay under 32 MB).

music_tab.bin: u32 count, then per music four u32: key (the sound event it replaces: prefix << 8 |
code, prefix 0 for a command alone), offset of the left channel in music.bin, offset of the right
channel, length in samples. Needs numpy and scipy.
"""
import os, struct
import numpy as np
from scipy.signal import butter, lfilter, fftconvolve

here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(os.path.dirname(here))
SR = 22050
NOTE = {'C': 0, 'C#': 1, 'Db': 1, 'D': 2, 'D#': 3, 'Eb': 3, 'E': 4, 'F': 5, 'F#': 6, 'Gb': 6,
        'G': 7, 'G#': 8, 'Ab': 8, 'A': 9, 'A#': 10, 'Bb': 10, 'B': 11}


def midi(n):                                    # 'C4' -> 60
    return NOTE[n[:-1]] + 12 * (int(n[-1]) + 1)


def hz(m): return 440.0 * 2 ** ((m - 69) / 12)


def lowpass(x, f):
    b, a = butter(2, min(f / (SR / 2), 0.99)); return lfilter(b, a, x)


def highpass(x, f):
    b, a = butter(2, f / (SR / 2), btype='high'); return lfilter(b, a, x)


def decay(n, tau):
    return np.exp(-np.arange(n) / (tau * SR))


def attack(x, ms=3):
    k = min(len(x), int(ms * SR / 1000)); x[:k] *= np.linspace(0, 1, k); return x


# ---------------------------------------------------------------- instruments (mono sample arrays)
def marimba(m, dur):
    n = int((dur + 0.6) * SR); t = np.arange(n) / SR; f = hz(m)
    x = (np.sin(2 * np.pi * f * t) * decay(n, 0.42) + 0.30 * np.sin(2 * np.pi * 3.93 * f * t) * decay(n, 0.07)
         + 0.10 * np.sin(2 * np.pi * 9.24 * f * t) * decay(n, 0.02))
    return attack(x, 2)


def brass(m, dur):
    n = int((dur + 0.15) * SR); t = np.arange(n) / SR; f = hz(m)
    vib = 1 + 0.004 * np.sin(2 * np.pi * 5.5 * t) * np.minimum(1, t / 0.25)
    ph = 2 * np.pi * f * np.cumsum(vib) / SR
    x = sum(np.sin(k * ph) / k for k in range(1, int(9000 / f) + 1))
    env = np.minimum(1, t / 0.03) * (0.75 + 0.25 * np.exp(-t / 0.12))
    env[int(dur * SR):] *= np.exp(-np.arange(n - int(dur * SR)) / (0.05 * SR))
    return lowpass(x * env, 2600) * 0.55


def bass(m, dur):
    n = int((dur + 0.1) * SR); t = np.arange(n) / SR; f = hz(m)
    x = np.sin(2 * np.pi * f * t) + 0.35 * np.sin(4 * np.pi * f * t) + 0.12 * np.sin(6 * np.pi * f * t)
    env = decay(n, 0.5); env[int(dur * SR):] *= np.exp(-np.arange(n - int(dur * SR)) / (0.02 * SR))
    return attack(x * env, 4)


def bell(m, dur):
    n = int((dur + 1.0) * SR); t = np.arange(n) / SR; f = hz(m)
    return attack(np.sin(2 * np.pi * f * t) * decay(n, 0.6) + 0.4 * np.sin(2 * np.pi * 2.76 * f * t) * decay(n, 0.25), 1)


rng = np.random.default_rng(1995)
KICK = (np.sin(2 * np.pi * np.cumsum(45 + 90 * decay(int(0.3 * SR), 0.03)) / SR) * decay(int(0.3 * SR), 0.12))
_nz = rng.standard_normal(int(0.25 * SR))
SNARE = (lowpass(highpass(_nz, 1200), 7000) * decay(len(_nz), 0.06) * 0.8
         + np.sin(2 * np.pi * 185 * np.arange(len(_nz)) / SR) * decay(len(_nz), 0.04) * 0.5)
_nh = rng.standard_normal(int(0.08 * SR))
HAT = highpass(_nh, 7000) * decay(len(_nh), 0.018) * 0.5
OHAT = highpass(rng.standard_normal(int(0.3 * SR)), 6500) * decay(int(0.3 * SR), 0.09) * 0.35
CLAP = sum(np.roll(highpass(rng.standard_normal(int(0.2 * SR)), 900) * decay(int(0.2 * SR), 0.05), int(k * 0.011 * SR))
           for k in range(3)) * 0.35

# ---------------------------------------------------------------- arrangement
CHORDS = {'': [0, 4, 7], 'm': [0, 3, 7], '7': [0, 4, 7, 10], 'm7': [0, 3, 7, 10], 'maj7': [0, 4, 7, 11],
          'sus': [0, 5, 7]}


def chord(name):
    """'G' 'Em' 'D7' -> (root midi in octave 3, intervals)"""
    root = name[:2] if len(name) > 1 and name[1] in '#b' else name[:1]
    return midi(root + '3'), CHORDS[name[len(root):]]


class Track:
    def __init__(self, bpm, bars):
        self.beat = 60.0 / bpm; self.bars = bars
        self.n = int(bars * 4 * self.beat * SR)
        self.L = np.zeros(self.n + 3 * SR); self.R = np.zeros(self.n + 3 * SR)

    def put(self, x, beat, gain, pan):
        i = int(round(beat * self.beat * SR)) % self.n
        seg = x * gain; j = min(len(seg), len(self.L) - i)
        self.L[i:i + j] += seg[:j] * np.cos(pan * np.pi / 2); self.R[i:i + j] += seg[:j] * np.sin(pan * np.pi / 2)
        if len(seg) > j:                      # wraps round to the start: the loop joins
            k = len(seg) - j; self.L[:k] += seg[j:] * np.cos(pan * np.pi / 2); self.R[:k] += seg[j:] * np.sin(pan * np.pi / 2)

    def render(self):
        L, R = self.L[:self.n].copy(), self.R[:self.n].copy()
        tail = len(self.L) - self.n                       # what rang past the end comes back at the start
        L[:tail] += self.L[self.n:]; R[:tail] += self.R[self.n:]
        ir_n = int(1.2 * SR); t = np.arange(ir_n) / SR
        irl = rng.standard_normal(ir_n) * np.exp(-t / 0.28); irr = rng.standard_normal(ir_n) * np.exp(-t / 0.28)
        irl[:int(0.012 * SR)] = 0; irr[:int(0.017 * SR)] = 0
        irl /= np.sqrt(np.sum(irl ** 2)); irr /= np.sqrt(np.sum(irr ** 2))   # unit energy: the room
        irl, irr = lowpass(irl, 5000), lowpass(irr, 5000)                   # adds no level of its own
        wl = fftconvolve(np.concatenate([L, L]), irl)[self.n:2 * self.n] * 0.22
        wr = fftconvolve(np.concatenate([R, R]), irr)[self.n:2 * self.n] * 0.22
        return L + wl, R + wr


def melody(rng, key, prog, motif, low, high):
    """A melody on chord tones (strong beats) and scale steps (weak beats), built from a rhythmic
    motif repeated with variations; each 4-bar phrase ends on the root of its last chord."""
    scale = [key + s for s in (0, 2, 4, 5, 7, 9, 11)]
    pitches = sorted({p + 12 * o for p in scale for o in range(-1, 4) if low <= p + 12 * o <= high})
    out, cur = [], pitches[len(pitches) // 2]
    for b, name in enumerate(prog):
        r, iv = chord(name); tones = [p for p in pitches if (p - r) % 12 in [i % 12 for i in iv]]
        t = 0.0
        rhythm = motif if b % 4 != 3 else [2.0, 1.0, 1.0]
        for k, d in enumerate(rhythm):
            if b % 4 == 3 and k == len(rhythm) - 1: break           # breath at the end of a phrase
            strong = (t % 1.0 == 0)
            pool = tones if strong else pitches
            near = sorted(pool, key=lambda p: abs(p - cur) + rng.random() * 2.5)[:3]
            cur = near[0] if rng.random() < 0.6 else near[rng.integers(len(near))]
            if b % 4 == 3 and k == len(rhythm) - 2:
                cur = min((p for p in pitches if (p - r) % 12 == 0), key=lambda p: abs(p - cur))
            out.append((b * 4 + t, cur, d * 0.92)); t += d
    return out


def song(bpm, key, prog, motif, lead_a, lead_b, seed, drums='full', bars_per_pass=None):
    rng = np.random.default_rng(seed)
    tr = Track(bpm, len(prog) * 2)
    full = prog + prog
    mel = melody(rng, midi(key + '4'), prog, motif, midi(key + '4') - 3, midi(key + '4') + 14)
    for b, name in enumerate(full):
        r, iv = chord(name)
        # bass: root, fifth, octave on eighths
        for k, (off, step) in enumerate([(0, 0), (1, 0), (1.5, 7), (2, 0), (3, 12), (3.5, 7)]):
            tr.put(bass(r - 12 + step, 0.4), b * 4 + off, 0.42 if off in (0, 2) else 0.3, 0.5)
        # marimba arpeggio, soft, on eighths
        arp = [r + 12 + i for i in iv] + [r + 24]
        for k in range(8):
            tr.put(marimba(arp[k % len(arp)], 0.3), b * 4 + k * 0.5, 0.13, 0.25)
        # brass stabs on the offbeats in the second pass
        if b >= len(prog):
            for off in (0.5, 1.5, 2.5, 3.5):
                for i in iv[:3]: tr.put(brass(r + 12 + i, 0.18), b * 4 + off, 0.07, 0.72)
        # drums
        for k in range(8):
            beat = b * 4 + k * 0.5
            if drums != 'soft' or k % 2 == 0: tr.put(HAT if k % 4 != 3 else OHAT, beat, 0.55, 0.65)
        if drums == 'soft':
            tr.put(KICK, b * 4, 0.6, 0.5); tr.put(CLAP, b * 4 + 3, 0.35, 0.45)
        else:
            for off in (0, 2, 2.5) if b % 2 else (0, 1.5, 2): tr.put(KICK, b * 4 + off, 0.85, 0.5)
            for off in (1, 3): tr.put(SNARE, b * 4 + off, 0.55, 0.45)
            if b % 4 == 3: tr.put(CLAP, b * 4 + 3.5, 0.35, 0.55)
        if b % 4 == 0: tr.put(bell(r + 36, 0.3), b * 4, 0.08, 0.8)
    # lead: marimba on the first pass, brass on the second (same tune, an octave apart in places)
    for beat, m, d in mel:
        if lead_a == 'marimba': tr.put(marimba(m + 12, d), beat, 0.33, 0.4)
        else: tr.put(brass(m, d), beat, 0.3, 0.45)
        if lead_b == 'brass': tr.put(brass(m, d), beat + len(prog) * 4, 0.3, 0.55)
        else: tr.put(marimba(m + 12, d), beat + len(prog) * 4, 0.33, 0.4)
        tr.put(bell(m + 24, d), beat + len(prog) * 4, 0.05, 0.85)
    return tr.render()


# key, the event of the game it replaces, and the song
MUSICS = [
    ('menus', 0x191A, lambda: song(132, 'C', ['C', 'Am', 'F', 'G', 'C', 'Am', 'Dm', 'G7'],
                                   [1, 0.5, 0.5, 1, 1], 'marimba', 'brass', 11)),
    ('map', 0x191C, lambda: song(108, 'Bb', ['Bb', 'Gm', 'Eb', 'F', 'Bb', 'Gm', 'Cm', 'F7'],
                                 [1.5, 0.5, 1, 1], 'marimba', 'marimba', 23, drums='soft')),
    ('game', 0x002E, lambda: song(150, 'G', ['G', 'Em', 'C', 'D', 'G', 'Em', 'Am', 'D7',
                                             'C', 'D', 'Bm', 'Em', 'C', 'D', 'G', 'D7'],
                                  [0.5, 0.5, 1, 0.5, 0.5, 1], 'marimba', 'brass', 37)),
    ('puzzle', 0x1910, lambda: song(138, 'F', ['F', 'Dm', 'Bb', 'C', 'F', 'Am', 'Bb', 'C7',
                                               'Dm', 'Bb', 'F', 'C', 'Bb', 'C', 'F', 'C7'],
                                    [1, 0.5, 0.5, 0.5, 0.5, 1], 'brass', 'marimba', 41)),
    ('gameover', 0x0021, lambda: song(96, 'D', ['Dm', 'Bb', 'Gm', 'A', 'Dm', 'Bb', 'Gm', 'A7'],
                                      [2, 1, 1], 'marimba', 'marimba', 53, drums='soft')),
    ('gears', 0x1913, lambda: song(140, 'A', ['Am', 'F', 'G', 'Em', 'Am', 'F', 'Dm', 'E7',
                                              'F', 'G', 'Em', 'Am', 'Dm', 'G', 'Am', 'E7'],
                                   [0.5, 0.5, 0.5, 0.5, 1, 1], 'brass', 'marimba', 67)),
]
# other events of the game that start a music: they play one of the pieces above (a music already
# playing is not started again)
ALIASES = [(0x0024, 'map'),          # the map of the paths A-E (vs computer)
           (0x0023, 'gears'),        # Puzzle Game, second world
           (0x1915, 'gears'),        # two players, gears stage
           (0x1916, 'game')]         # two players, beach stage


def main():
    data, tab = bytearray(), []
    peaks = []
    songs = []
    for name, key, make in MUSICS:
        L, R = make(); songs.append((name, key, L, R)); peaks.append(max(np.max(np.abs(L)), np.max(np.abs(R))))
        print('%-9s %.1f s' % (name, len(L) / SR))
    peaks = [np.max(np.abs((L + R) * 0.5)) for _, _, L, R in songs]
    g = 30000.0 / max(peaks)                                   # one gain: the musics keep their balance
    for name, key, L, R in songs:
        while len(data) % 4: data.append(0)
        off = len(data); data += np.clip(np.round((L + R) * 0.5 * g), -32768, 32767).astype('<i2').tobytes()
        tab.append((key, off, off, len(L)))
        for akey, aname in ALIASES:
            if aname == name: tab.append((akey, off, off, len(L)))
    open(os.path.join(root, 'music.bin'), 'wb').write(bytes(data))
    with open(os.path.join(root, 'music_tab.bin'), 'wb') as f:
        f.write(struct.pack('<I', len(tab)))
        for t in tab: f.write(struct.pack('<4I', *t))
    print('music.bin: %d musics, %.1f MB' % (len(tab), len(data) / 2 ** 20))


if __name__ == '__main__':
    main()
