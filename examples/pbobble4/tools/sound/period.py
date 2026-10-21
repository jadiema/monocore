#!/usr/bin/env python3
"""Finds the loop of a music from the YM2610 writes of the driver (render ... -writes): at that
level the loop repeats step for step, even where the sound does not repeat to the bit.

usage: python3 tools/sound/period.py FILE.writes...
prints  name  intro  loop  (in samples at 55555 Hz, and seconds)  or  "no loop"
The log has one line per step of the driver (a timer interrupt): the sample number, then the
writes, each as two tokens: port (0 address A, 1 data A, 2 address B, 3 data B) and value. Writes
are read as (port, register, value); the timer registers (24-27 on port A), written every step,
are left out. The period is the smallest shift that makes the last steps equal to earlier ones
(found from windows of 64 steps), the intro is where that equality starts.
"""
import sys

K = 64          # steps of the window that proposes a period
CHECK = 2000    # steps at the end that a period must explain


def steps(path):
    out = []
    for line in open(path):
        t = line.split()
        if not t: continue
        sample, w, reg = int(t[0]), [], [None, None]
        for tok in t[1:]:
            port, val = int(tok[0], 16), int(tok[1:], 16)
            if port in (0, 2): reg[port >> 1] = val
            else:
                r = reg[port >> 1]
                if not (port == 1 and r is not None and 0x24 <= r <= 0x27): w.append((port >> 1, r, val))
        out.append((sample, hash(tuple(w))))
    return out


QUIET = hash(())
MIN_P = 300     # steps (1.8 s): shorter periods are notes held after the end of a piece


def find(st):
    h = [x[1] for x in st]
    n = len(h)
    k = n
    while k > 0 and h[k - 1] == QUIET: k -= 1
    if n - k > 3000: return ('end', st[k - 1][0] if k else 0)   # no write for 18 s: the piece is over
    if n < CHECK + K + 10: return None
    grams = {}
    for i in range(0, n - K):
        grams.setdefault(hash(tuple(h[i:i + K])), []).append(i)
    tail = n - K
    cands = sorted({tail - i for i in grams.get(hash(tuple(h[tail:tail + K])), []) if tail - i > 0})
    for p in cands:                                   # smallest period that explains the end
        if p < MIN_P: continue
        if p > n - CHECK: break
        bad = sum(h[i] != h[i - p] for i in range(n - CHECK, n))
        if bad > CHECK // 100: continue
        i, miss = n - 1, 0                            # walk back while it keeps repeating
        while i - p >= 0:
            if h[i] != h[i - p]:
                miss += 1
                if miss > 8: break
            else: miss = 0
            i -= 1
        start = min(n - 1, i + 1 + 8)                 # first step of the repeated part
        return st[start - p][0] if start - p >= 0 else st[0][0], st[start][0] - st[start - p][0]
    return None


def main():
    for path in sys.argv[1:]:
        r = find(steps(path))
        name = path.split('/')[-1]
        if r is None: print('%s no loop' % name)
        elif r[0] == 'end': print('%s ends %d  (%.2f s)' % (name, r[1], r[1] / 55555))
        else: print('%s intro %d loop %d  (%.2f s, %.2f s)' % (name, r[0], r[1], r[0] / 55555, r[1] / 55555))
        sys.stdout.flush()


if __name__ == '__main__':
    main()
