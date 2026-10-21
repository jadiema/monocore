#!/usr/bin/env python3
"""Original sound effects of the port, synthesised with the instruments of make_music.py, in place of
the game's (each event of the Z80 driver gets a sound of ours, chosen from what the game does when
it sends it: tracked against the pad and the screen in game sessions).

usage: python3 tools/sound/make_sfx.py        writes sounds.bin and sounds_tab.bin (out of git)
Same table as make_sounds.py (which rendered the original sounds with the Z80 driver): u32 count,
then per sound eight u32: key (prefix << 8 | code), offset, length in samples, loop (0xFFFFFFFF),
format (1 = 16-bit PCM), rate (22050), playback volume (0-255), cuts (0). Events that make no sound
in the game are kept with length 0, so that the console knows them.
"""
import os, struct, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_music import SR, midi, hz, marimba, brass, bass, bell, lowpass, highpass, decay, attack, KICK, SNARE, HAT, OHAT, CLAP

here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(os.path.dirname(here))
rng = np.random.default_rng(2026)


def mix(parts, length):
    """parts: (signal, start in seconds, gain)"""
    out = np.zeros(int(length * SR))
    for x, t, g in parts:
        i = int(t * SR); j = min(len(x), len(out) - i)
        if j > 0: out[i:i + j] += x[:j] * g
    return out


def fade_out(x, ms=20):
    k = min(len(x), int(ms * SR / 1000)); x[-k:] *= np.linspace(1, 0, k); return x


def sweep(f0, f1, dur, tau=None, shape='sin'):
    n = int(dur * SR); t = np.arange(n) / SR
    f = f0 * (f1 / f0) ** (t / dur)                 # exponential glide
    ph = 2 * np.pi * np.cumsum(f) / SR
    x = np.sin(ph) if shape == 'sin' else np.sin(ph) + 0.3 * np.sin(2 * ph)
    return attack(x * (decay(n, tau) if tau else np.ones(n)), 2)


def noise(dur, lo, hi, tau):
    n = int(dur * SR); x = rng.standard_normal(n)
    return lowpass(highpass(x, lo), hi) * decay(n, tau)


def chord_hit(notes, dur, inst=brass):
    return sum(inst(m, dur) for m in notes)


# ---------------------------------------------------------------- the sounds
def tick():                     # the launcher turns (sent every 4 frames while it moves)
    return fade_out(mix([(marimba(midi('E7'), 0.03), 0, 0.5), (noise(0.03, 2000, 8000, 0.006), 0, 0.15)], 0.06))


def shot():                     # a bubble is shot
    return fade_out(mix([(sweep(260, 980, 0.16, 0.09), 0, 0.7), (noise(0.12, 800, 3000, 0.03), 0, 0.05),
                         (marimba(midi('G5'), 0.1), 0.0, 0.25)], 0.3))


def stick():                    # the bubble hits a wall or sticks
    return fade_out(mix([(marimba(midi('C6'), 0.08), 0, 0.5), (sweep(180, 90, 0.08, 0.03), 0, 0.5)], 0.25))


def pop():                      # bubbles burst
    parts = []
    for k in range(5):
        f = 500 + 220 * k + rng.random() * 120
        parts.append((sweep(f * 1.8, f, 0.07, 0.03), 0.045 * k, 0.45))
        parts.append((bell(midi('C7') + (2, 4, 7, 9, 12)[k], 0.1), 0.045 * k + 0.01, 0.12))
    return fade_out(mix(parts, 0.7))


def cascade(root='C5', up=True):  # bubbles fall, combo
    steps = [0, 4, 7, 12, 16, 19] if up else [19, 16, 12, 7, 4, 0]
    return fade_out(mix([(marimba(midi(root) + s, 0.12), 0.06 * k, 0.45) for k, s in enumerate(steps)]
                        + [(bell(midi(root) + 24, 0.3), 0.36, 0.15)], 1.0))


def fanfare_small(root='G4'):   # a big combo, an attack sent to the other side
    r = midi(root)
    return fade_out(mix([(chord_hit([r, r + 4, r + 7], 0.12), 0, 0.35), (chord_hit([r, r + 4, r + 7], 0.12), 0.16, 0.35),
                         (chord_hit([r + 5, r + 9, r + 12], 0.5), 0.32, 0.4), (CLAP, 0.32, 0.4),
                         (cascade('C6'), 0.32, 0.4)], 1.4))


def confirm(a='C6', b='G6'):    # a choice in the menus
    return fade_out(mix([(bell(midi(a), 0.15), 0, 0.5), (bell(midi(b), 0.3), 0.09, 0.5),
                         (marimba(midi(a) - 12, 0.2), 0, 0.3)], 0.9))


def blip():                     # the cursor moves in a menu
    return fade_out(mix([(sweep(880, 1320, 0.05, 0.04), 0, 0.45), (marimba(midi('A6'), 0.04), 0, 0.2)], 0.09))


def round_start():              # before a round: drum roll and a brass stab
    parts = [(SNARE, 0.05 * k, 0.15 + 0.02 * k) for k in range(16)]
    parts += [(chord_hit([midi('C4'), midi('E4'), midi('G4'), midi('C5')], 0.6), 0.85, 0.35), (KICK, 0.85, 0.8),
              (OHAT, 0.85, 0.6)]
    return fade_out(mix(parts, 1.8))


def ready_go():                 # "READY" ... "GO!"
    return fade_out(mix([(chord_hit([midi('G3'), midi('D4'), midi('G4')], 0.25), 0, 0.35), (KICK, 0, 0.6),
                         (chord_hit([midi('C4'), midi('G4'), midi('C5'), midi('E5')], 0.6), 0.55, 0.4), (KICK, 0.55, 0.8),
                         (CLAP, 0.55, 0.5), (OHAT, 0.55, 0.6), (cascade('C5'), 0.6, 0.3)], 1.6))


def fanfare(root='C4', length=3.0):   # end of a round (won and lost are shown together)
    r = midi(root); parts = []
    for k, (off, iv, d) in enumerate([(0, [0, 4, 7], 0.15), (0.2, [0, 4, 7], 0.15), (0.4, [0, 4, 7], 0.15),
                                      (0.6, [5, 9, 12], 0.35), (1.0, [7, 11, 14], 0.35), (1.45, [12, 16, 19], 1.0)]):
        parts.append((chord_hit([r + i for i in iv], d), off, 0.3))
        parts.append((KICK, off, 0.5))
    parts += [(OHAT, 1.45, 0.7), (CLAP, 1.45, 0.4), (bell(r + 36, 0.8), 1.45, 0.2), (cascade('C6'), 1.5, 0.25)]
    return fade_out(mix(parts, length))


def logo():                     # the NEOGEO animation at start-up
    parts = [(bell(midi('C6') + s, 0.2), 0.07 * k, 0.25) for k, s in enumerate([0, 4, 7, 12, 16, 19, 24, 28])]
    parts += [(chord_hit([midi('C4'), midi('G4'), midi('E5'), midi('C5')], 1.6, marimba), 0.6, 0.3),
              (chord_hit([midi('C4'), midi('G4'), midi('C5')], 1.2), 0.6, 0.18)]
    return fade_out(mix(parts, 3.0), 300)


def beep(f, dur):
    x = sweep(f, f, dur, 0.3); k = int(0.012 * SR)
    x[:k] *= np.linspace(0, 1, k); x[-k:] *= np.linspace(1, 0, k)     # no click at either end
    return x


def alert():                    # rare, a warning
    return fade_out(mix([(beep(990, 0.09), 0.2 * k, 0.35) for k in range(3)]
                        + [(beep(740, 0.09), 0.2 * k + 0.1, 0.35) for k in range(3)], 0.8))


def chime():                    # rare, soft
    return fade_out(mix([(bell(midi('E6') + s, 0.4), 0.12 * k, 0.3) for k, s in enumerate([0, 3, 7, 12])], 1.6), 200)


SOUNDS = [
    (0x0002, logo, 150),
    (0x1921, lambda: fanfare('F4', 3.5), 170),
    (0x1A42, tick, 90),
    (0x1A21, shot, 170),
    (0x1A24, stick, 150),
    (0x1A22, pop, 190),
    (0x1A25, cascade, 170),
    (0x1A2A, lambda: cascade('D5'), 170),
    (0x1A26, fanfare_small, 180),
    (0x1A28, lambda: fanfare_small('A4'), 180),
    (0x1A29, lambda: fanfare_small('F4'), 180),
    (0x1A2B, confirm, 170),
    (0x1A2C, lambda: confirm('D6', 'A6'), 170),
    (0x1A27, blip, 130),
    (0x1A2E, round_start, 190),
    (0x1A2F, ready_go, 200),
    (0x1A30, fanfare, 190),
    (0x1A54, alert, 150),
    (0x1A59, chime, 150),
]
SILENT = [0x1A14, 0x1A19, 0x1C19, 0x1D19]      # no sound in the game


def main():
    data, tab = bytearray(), []
    for key, make, vol in SOUNDS:
        x = make(); x = x / max(1e-9, np.max(np.abs(x)))
        while len(data) % 4: data.append(0)
        off = len(data); data += np.clip(np.round(x * 32000), -32768, 32767).astype('<i2').tobytes()
        tab.append((key, off, len(x), 0xFFFFFFFF, 1, SR, vol, 0))
        print('%04X %-14s %.2f s' % (key, make.__name__ if make.__name__ != '<lambda>' else 'variant', len(x) / SR))
    for key in SILENT: tab.append((key, len(data), 0, 0xFFFFFFFF, 1, SR, 0, 0))
    open(os.path.join(root, 'sounds.bin'), 'wb').write(bytes(data))
    with open(os.path.join(root, 'sounds_tab.bin'), 'wb') as f:
        f.write(struct.pack('<I', len(tab)))
        for t in tab: f.write(struct.pack('<8I', *t))
    print('sounds.bin: %d sounds, %.2f MB' % (len(tab), len(data) / 2 ** 20))


if __name__ == '__main__':
    main()
