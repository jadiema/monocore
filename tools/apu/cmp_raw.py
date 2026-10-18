#!/usr/bin/env python3
"""Compare two raw int16 stereo streams: first difference, count, and the log writes around it.
usage: cmp_raw.py a.raw b.raw [log]"""
import sys
from array import array
a, b = array('h'), array('h')
a.frombytes(open(sys.argv[1], 'rb').read()); b.frombytes(open(sys.argv[2], 'rb').read())
n = min(len(a), len(b)) // 2
diff = [i for i in range(n) if a[2 * i] != b[2 * i] or a[2 * i + 1] != b[2 * i + 1]]
print('%d echantillons, %d differents' % (n, len(diff)))
if diff:
    i = diff[0]
    print('premier ecart a l\'echantillon %d : %s / %s' % (i, (a[2*i], a[2*i+1]), (b[2*i], b[2*i+1])))
    print('suivants :', [(j, a[2*j] - b[2*j]) for j in diff[:8]])
    if len(sys.argv) > 3:
        shown = 0
    for line in open(sys.argv[3]):
            if line.startswith('#'): continue
            k = int(line.split()[0])
            if i - 400 <= k <= i and shown < 12: print("   ecriture", line.strip()); shown += 1
sys.exit(1 if diff else 0)
