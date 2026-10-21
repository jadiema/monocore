#!/usr/bin/env python3
"""Reads the samples of tools/prof.js and tells where the time goes: by function of the MONOCORE
program, and, inside the translated program, by routine of the 68000 (through the debug lines:
build with -g, then the comment // ADDRESS above each translated instruction).
usage: python3 tools/prof.py samples.txt [build/pbobble4.elf]"""
import bisect, json, os, re, subprocess, sys, collections
here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
samples = [(int(a, 16), int(n)) for a, n in (l.split() for l in open(sys.argv[1]) if l.strip())]
elf = sys.argv[2] if len(sys.argv) > 2 else os.path.join(root, 'build', 'pbobble4.elf')
total = sum(n for _, n in samples)
# the whole chain of inlined functions of every address (innermost first), as JSON
raw = subprocess.run(['llvm-symbolizer', '--obj=' + elf, '--inlining', '--output-style=JSON'] +
                     ['0x%x' % a for a, _ in samples], capture_output=True, text=True).stdout
chains = [[(f.get('FunctionName', '?'), f.get('FileName', ''), f.get('Line', 0)) for f in e['Symbol']]
          for e in json.loads(raw)]
src = os.path.join(root, 'build', 'tr', 'pb2_code.c')
lines = open(src).read().split('\n') if os.path.exists(src) else []
addr_of_line = []; cur = None
for l in lines:
    m = re.match(r'\s*// ([0-9A-F]{6}) ', l)
    if m: cur = int(m.group(1), 16)
    addr_of_line.append(cur)
cmap = json.load(open(os.path.join(root, 'build', 'check', 'codemap.json')))
entries = sorted(set(int(x, 16) for x in cmap['entries']) | {0xF0000, 0xF0100, 0xF0400})
by_fn, by_routine = collections.Counter(), collections.Counter()
for k, (a, n) in enumerate(samples):
    ch = chains[k] if k < len(chains) else [('?', '', 0)]
    by_fn[ch[-1][0]] += n                                # the outermost (real) function
    tr = [c for c in ch if c[1].endswith('pb2_code.c')]
    if tr:
        ln = tr[0][2] - 1
        a68 = addr_of_line[ln] if 0 <= ln < len(addr_of_line) else None
        if a68 is not None:
            r = entries[bisect.bisect_right(entries, a68) - 1]
            by_routine['%06X' % r] += n
print('%d samples' % total)
print('by function:')
for f, n in by_fn.most_common(12): print('  %5.1f%%  %s' % (100 * n / total, f))
print('translated 68000 code, by routine:')
for r, n in by_routine.most_common(20): print('  %5.1f%%  %s' % (100 * n / total, r))
