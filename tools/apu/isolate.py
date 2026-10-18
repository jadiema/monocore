#!/usr/bin/env python3
"""Split a sound log by component (8 PCM voices, 8 PSG channels) and compare each one alone.
usage: isolate.py rom log samples"""
import subprocess, sys
rom, log, nsmp = sys.argv[1], sys.argv[2], int(sys.argv[3])
lines = [l for l in open(log) if l.strip() and not l.startswith('#')]
def comp(o):
    o = int(o)
    if o < 0x200: return 'voix%d' % (o >> 5)
    if 0x400 <= o < 0x480: return 'psg%d' % ((o - 0x400) >> 4)
    return 'master'
for name in ['voix%d' % v for v in range(8)] + ['psg%d' % c for c in range(8)]:
    sel = [l for l in lines if comp(l.split()[1]) in (name, 'master') and int(l.split()[0]) < nsmp]
    if not any(comp(l.split()[1]) == name for l in sel): continue
    p = '/tmp/claude-0/apu/iso'
    open(p + '.log', 'w').write(''.join(sel) + '#samples %d\n' % nsmp)
    subprocess.run(['node', 'tools/apu/apu_ref.js', rom, p + '.log', p + '_js.raw'], capture_output=True)
    subprocess.run(['rtl/obj_apu/Vmc_apu', rom, p + '.log', p + '_rtl.raw'], capture_output=True)
    r = subprocess.run(['python3', 'tools/apu/cmp_raw.py', p + '_js.raw', p + '_rtl.raw'], capture_output=True, text=True)
    first = r.stdout.splitlines()
    print('%-7s %s' % (name, ' | '.join(first[:2])))
