#!/usr/bin/env python3
"""Splits a stream of sound commands into the events of the Z80 driver of the game.

The driver queues every byte the 68000 writes, then reads them one by one: 0A, 0E, 14 and 18-1E
read one more byte (their parameter), 15 reads two (15 a b: sound b of the slot a, used for the
second player), every other command is alone. Used by make_sounds.py
and, written again in C, by neort.c.
"""
TWO = {0x0A, 0x0E, 0x14} | set(range(0x18, 0x1F))
THREE = {0x15}


def split(cmds):
    out, k = [], 0
    while k < len(cmds):
        c = cmds[k]
        if c in THREE and k + 2 < len(cmds): out.append((c, cmds[k + 1], cmds[k + 2])); k += 3
        elif c in TWO and k + 1 < len(cmds): out.append((c, cmds[k + 1])); k += 2
        else: out.append((c,)); k += 1
    return out


if __name__ == '__main__':
    import collections, sys
    seen = collections.Counter()
    for path in sys.argv[1:]:
        cmds = [int(l.split()[-1], 16) for l in open(path) if l.strip()]
        seen.update(split(cmds))
    for e, n in sorted(seen.items()): print(' '.join('%02X' % v for v in e), n)
