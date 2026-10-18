#!/usr/bin/env python3
"""Rough static timing analysis of a Yosys Cyclone V netlist (synth_intel_alm, write_json).

Delay model (Cyclone V -7, slow corner), calibrated on paths measured by Quartus:
  routing into a cell input 0.75 ns (0 on carry-in), LUT 0.35 ns, carry chain: data in -> carry out
  0.87 ns, carry hop 0.03 ns, carry/data -> sum 0.36 ns, DSP multiplier 3.9 ns (+1.2 ns routing),
  M10K clock-to-out 2.8 ns, MLAB read 1.2 ns, register clock-to-out 0.3 ns, setup + skew 0.8 ns.
These are averages: the tool ranks paths and gives the order of magnitude; Quartus has the last word.

usage: sta.py net.json [period_ns] [n]   -> worst path per block, then the n worst endpoints
"""
import json, sys, re
from collections import defaultdict

net = json.load(open(sys.argv[1]))
period = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0
topn = int(sys.argv[3]) if len(sys.argv) > 3 else 30
filt = re.compile(sys.argv[4]) if len(sys.argv) > 4 else None
mod = net['modules']['monocore_trial_top']
cells = mod['cells']

ROUTE, LUT, CARRY_IN, CARRY_HOP, SUM, DSP, DSP_ROUTE, M10K_TCO, MLAB_RD, TCO, SETUP = \
    0.75, 0.35, 0.87, 0.03, 0.36, 3.9, 1.2, 2.8, 1.2, 0.3, 0.8

# bit -> readable name
names = {}
for n, info in mod['netnames'].items():
    if n.startswith('$'):
        continue
    for i, b in enumerate(info['bits']):
        if isinstance(b, int):
            nm = n if len(info['bits']) == 1 else '%s[%d]' % (n, i)
            if 'MISTRAL' in nm or '$' in nm:
                continue
            if b not in names or len(nm) < len(names[b]):
                names[b] = nm

driver = {}          # bit -> (cell name, port)
for cn, c in cells.items():
    for p, d in c['port_directions'].items():
        if d == 'output':
            for b in c['connections'][p]:
                if isinstance(b, int):
                    driver[b] = (cn, p)

arrival = {}         # bit -> (time, predecessor bit or None)
SRC_TYPES = {'MISTRAL_FF': TCO}

def comb_arcs(cn, c, port):
    """inputs (bit, extra delay) feeding output `port` of a combinational cell"""
    t = c['type']
    conn = c['connections']
    if t.startswith('MISTRAL_ALUT') and t != 'MISTRAL_ALUT_ARITH':
        return [(b, ROUTE + LUT) for p in conn if p != 'Q' for b in conn[p]]
    if t == 'MISTRAL_NOT':
        return [(conn['A'][0], ROUTE + LUT)]
    if t == 'MISTRAL_ALUT_ARITH':
        res = []
        for p in ('A', 'B', 'C', 'D0', 'D1'):
            res.append((conn[p][0], ROUTE + (CARRY_IN if port == 'CO' else LUT + SUM)))
        res.append((conn['CI'][0], CARRY_HOP if port == 'CO' else SUM))
        return res
    if t.startswith('MISTRAL_MUL'):
        return [(b, DSP_ROUTE + DSP) for p in ('A', 'B') for b in conn[p]]
    if t == 'MISTRAL_MLAB':
        return [(b, ROUTE + MLAB_RD) for b in conn['B1ADDR']]
    return []

sys.setrecursionlimit(1000000)

def arr(b):
    if not isinstance(b, int):
        return (0.0, None)
    if b in arrival:
        return arrival[b]
    if b not in driver:
        arrival[b] = (0.0, None)          # top input: false path
        return arrival[b]
    cn, port = driver[b]
    c = cells[cn]
    t = c['type']
    if t == 'MISTRAL_FF':
        res = (TCO, None)
    elif t == 'MISTRAL_M10K':
        res = (M10K_TCO, None)
    elif t in ('MISTRAL_IB', 'MISTRAL_CLKBUF'):
        res = (0.0, None)
    else:
        best = (0.0, None)
        # iterative-friendly: compute inputs first
        for ib, d in comb_arcs(cn, c, port):
            a = arr(ib)[0] + d
            if a > best[0]:
                best = (a, ib)
        if t == 'MISTRAL_MLAB':
            best = max(best, (MLAB_RD, None), key=lambda x: x[0])
        res = best
    arrival[b] = res
    return res

# warm up arrivals in topological-ish order to keep recursion shallow
order = sorted(driver.keys())
for b in order:
    try:
        arr(b)
    except RecursionError:
        pass

# endpoints: register inputs and RAM inputs
ends = []
for cn, c in cells.items():
    t = c['type']
    conn = c['connections']
    if t == 'MISTRAL_FF':
        ports = ('DATAIN', 'ENA', 'SCLR', 'SDATA', 'SLOAD')
    elif t == 'MISTRAL_M10K':
        ports = ('A1ADDR', 'A1DATA', 'A1EN', 'B1ADDR', 'B1EN')
    elif t == 'MISTRAL_MLAB':
        ports = ('A1ADDR', 'A1DATA', 'A1EN')
    else:
        continue
    for p in ports:
        for b in conn.get(p, []):
            ends.append((arr(b)[0] + ROUTE + SETUP, cn, b))

def src(cn):
    a = cells[cn].get('attributes', {}).get('src', '')
    for part in a.split('|'):
        if part.startswith('rtl/') or part.startswith('quartus_trial/'):
            f, rng = part.split(':', 1)
            return '%s:%s' % (f.replace('rtl/', ''), rng.split('.')[0])
    return cells[cn]['type']

def path_desc(b):
    """source lines crossed by the path, start to end, generic cells and repeats removed"""
    seq = []
    while b is not None:
        if isinstance(b, int) and b in driver:
            seq.append(src(driver[b][0]))
        b = arrival.get(b, (0, None))[1]
    seq.reverse()
    out = []
    for x in seq:
        if x.startswith('MISTRAL_') and x not in ('MISTRAL_M10K', 'MISTRAL_MUL18X18', 'MISTRAL_MUL27X27', 'MISTRAL_MUL9X9'):
            continue
        if not out or out[-1] != x:
            out.append(x)
    return out

def path_names(b):
    """named nets crossed by the path, start to end"""
    seq = []
    while b is not None:
        if isinstance(b, int) and b in names:
            nm = names[b].split('.')[-1]
            if not seq or seq[-1] != nm:
                seq.append(nm)
        b = arrival.get(b, (0, None))[1]
    seq.reverse()
    return seq

def path_len(b):
    n = 0
    while b is not None:
        n += 1
        b = arrival.get(b, (0, None))[1]
    return n

SCALE = 0.87   # model / Quartus ratio measured on the PSG path (25.5 ns estimated, 22.1 ns measured)
rows = sorted(ends, key=lambda r: -r[0])
worst = {}
for a, cn, b in rows:
    f = src(cn).split(':')[0]
    if f.startswith('MISTRAL_'):
        d = path_desc(b)
        f = 'RAM/' + (d[-1].split(':')[0] if d else '?')
    if f not in worst:
        worst[f] = (a, cn, b)
print('Pire chemin arrivant dans chaque fichier (estimation recalee x%.2f, periode %.1f ns) :' % (SCALE, period))
for f, (a, cn, b) in sorted(worst.items(), key=lambda x: -x[1][0]):
    print('%6.1f ns  %-26s -> %-20s via %s' % (a * SCALE, f, src(cn), ' > '.join(path_desc(b)[-9:])))
if filt:
    print('\nFiltre %s :' % filt.pattern)
    n = 0
    for a, cn, b in rows:
        d = path_desc(b) + ['=> ' + src(cn)]
        if filt.search(' '.join(d)):
            print('%6.1f ns  %s' % (a * SCALE, ' > '.join(d)))
            q = cells[cn]['connections'].get('Q', [None])[0]
            print('          signaux : %s  ==> %s   (%d cellules)' % (' > '.join(path_names(b)), names.get(q, cells[cn]['type']), path_len(b)))
            n += 1
            if n >= topn:
                break
