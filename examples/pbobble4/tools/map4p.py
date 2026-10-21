#!/usr/bin/env python3
"""Map of what the program does per player: the places to change for 4 players.

usage: python3 tools/map4p.py      (after prepare.py and m68kmap.py) -> build/check/map4p.txt
Lists, with the routine they are in (the nearest entry point of the code map before them):
  - the instructions that read the number of the running task (-19260(a5)): the field of a player
    is a task, number 5 for player 1 and 6 for player 2, all running the routine $74F16;
  - the absolute references to the data of player 1 and of player 2 (structure of 150 bytes at
    $10D5D2 / $10D668, and the other per-player data found by comparing game sessions that differ
    only by one player's input, see DESIGN_4P.md);
  - the uses of the two-player flag (-10200(a5)).
The report quotes the game's code: it stays in build/, out of the repository.
"""
import json, os, re, struct, sys
here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(root)), 'tools', 'recomp68k'))
import m68k

A5 = 0x108000
TASK_NUMBER = -19260
TWO_PLAYERS = -10200
# per-player data: (player 1, player 2, what)
PAIRS = [(0x10D5D2, 0x10D668, 'player structure (150 bytes; launcher angle at +8)'),
         (0x100FF4, 0x100FF8, 'two words per player'),
         (0x100248, 0x10041C, 'launcher object'),
         (0x10263D, 0x10243D, 'a byte per player (player 2 below player 1)')]


def main():
    rom = open(os.path.join(root, 'p1_be.bin'), 'rb').read()
    mem = lambda a: struct.unpack('>H', rom[a:a + 2])[0] if 0 <= a < len(rom) - 1 and a % 2 == 0 else None
    cmap = json.load(open(os.path.join(root, 'build', 'check', 'codemap.json')))
    ranges = [(int(s, 16), int(e, 16)) for s, e in cmap['ranges']]
    entries = sorted(int(a, 16) for a in cmap['entries'])
    in_code = lambda a: any(s <= a < e for s, e in ranges)

    def routine(a):
        k = max((e for e in entries if e <= a), default=0); return k

    def decode_at(a):          # the instruction that contains the operand word at a
        for back in (2, 4, 6):
            i = m68k.decode(mem, a - back)
            if i and i.len >= back + 2: yield i

    out = []; p = out.append

    def section(title, pattern, match):
        p('\n== ' + title)
        n = 0
        for m in re.finditer(re.escape(pattern), rom):
            a = m.start()
            if a % 2: continue
            for i in decode_at(a):
                if match(i) and in_code(i.addr):
                    p('  %06X  %-40s in routine %06X' % (i.addr, m68k.text(i), routine(i.addr))); n += 1; break
        p('  (%d)' % n)

    a5d = lambda d: lambda i: ('disp', 5, d) in (i.src, i.dst)
    section('number of the running task, %d(a5)' % TASK_NUMBER, struct.pack('>h', TASK_NUMBER), a5d(TASK_NUMBER))
    section('two-player flag, %d(a5)' % TWO_PLAYERS, struct.pack('>h', TWO_PLAYERS), a5d(TWO_PLAYERS))
    for p1, p2, what in PAIRS:
        for who, v in (('player 1', p1), ('player 2', p2)):
            ab = lambda i, v=v: v in (i.src[1] if i.src and i.src[0] in ('absl', 'imm') else None,
                                      i.dst[1] if i.dst and i.dst[0] == 'absl' else None)
            section('%s, %s: $%06X absolute' % (what, who, v), struct.pack('>I', v), ab)
            rel = lambda i, d=v - A5: ('disp', 5, d) in (i.src, i.dst)
            if -32768 <= v - A5 < 32768:
                section('%s, %s: %d(a5)' % (what, who, v - A5), struct.pack('>h', v - A5), rel)
    path = os.path.join(root, 'build', 'check', 'map4p.txt')
    open(path, 'w').write('\n'.join(out) + '\n')
    for line in out:
        if line.lstrip('\n').startswith('==') or line.startswith('  ('): print(line.strip())
    print('report:', path)


if __name__ == '__main__':
    main()
