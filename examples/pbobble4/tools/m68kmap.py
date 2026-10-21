#!/usr/bin/env python3
"""Maps the 68000 code of the Neo Geo program by recursive descent (first step of the translator).

usage: python3 tools/m68kmap.py [p1_be.bin] [--strict]
Entry points: interrupt vectors and the cartridge header (USER, PLAYER_START, DEMO_END, COIN_SOUND).
Follows branches and calls, records calls into the BIOS ($C00000-$C1FFFF), resolves the jump tables
of the usual forms:
  movea.l T(pc,Dn.w),Am ... jmp/jsr (Am)        table of 32-bit addresses
  move.w  T(pc,Dn.w),Dn ... jmp/jsr T2(pc,Dn.w)  table of 16-bit offsets from T2
  lea T,Am ... movea.l (d,Am,Dn.w),Ak ... jmp (Ak)
and lists the indirect jumps it cannot resolve (addresses computed or read from RAM).
Unless --strict is given, it also starts from every jsr/jmp abs.l found by scanning the ROM, then
follows the code addresses that appear as constants (#imm.l, lea, pea), which is how the game gives
a routine to its task kernel. A candidate is kept only if it decodes cleanly (Mapper.plausible).
What is still missing is reached through addresses computed at run time: the translator will report
them (see LISEZMOI.md) and they are added here as entry points.
Writes build/check/codemap.txt (report) and build/check/codemap.json (for the translator).
Needs capstone (pip install capstone).
"""
import collections, json, os, re, struct, sys
from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000

here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
BIOS = {0xC00402: 'RESET', 0xC00438: 'SYSTEM_INT1', 0xC0043E: 'SYSTEM_INT2', 0xC00444: 'SYSTEM_RETURN',
        0xC0044A: 'SYSTEM_IO', 0xC00450: 'CREDIT_CHECK', 0xC00456: 'CREDIT_DOWN',
        0xC0045C: 'READ_CALENDAR', 0xC00462: 'SETUP_CALENDAR', 0xC00468: 'SYS_CARD',
        0xC0046E: 'SYS_CARD_ERROR', 0xC00474: 'SYS_HOW_TO_PLAY', 0xC0047A: 'CHECKSUM',
        0xC004C2: 'FIX_CLEAR', 0xC004C8: 'LSP_1ST', 0xC004CE: 'MESS_OUT', 0xC004D4: 'CONTROLLER_SETUP'}
BRANCH = re.compile(r'^(b(ra|sr|hi|ls|cc|cs|ne|eq|vc|vs|pl|mi|ge|lt|gt|le|hs|lo))(\.[bwsl])?$')
DBCC = re.compile(r'^db(ra|t|f|hi|ls|cc|cs|ne|eq|vc|vs|pl|mi|ge|lt|gt|le|hs|lo)(\.w)?$')
ABS = re.compile(r'^\$([0-9a-f]+)(\.[wl])?$')
PCREL = re.compile(r'^\$([0-9a-f]+)\(pc\)$')
PCIDX = re.compile(r'^\$([0-9a-f]+)\(pc, (d\d)\.[wl]\)$')
AREG = re.compile(r'^\((a\d)\)$')
AIDX = re.compile(r'^(-?\$[0-9a-f]+)?\((a\d), (d\d)\.[wl]\)$')


def split_ops(ops):
    """Operands of a capstone operand string; commas inside parentheses do not split."""
    out, depth, cur = [], 0, ''
    for ch in ops or '':
        if ch == ',' and depth == 0: out.append(cur.strip()); cur = ''; continue
        depth += (ch == '(') - (ch == ')'); cur += ch
    if cur.strip(): out.append(cur.strip())
    return out


class Mapper:
    def __init__(self, rom):
        self.rom, self.size = rom, len(rom)
        self.cs = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)
        self.ins = {}                       # address -> (size, mnemonic, operands)
        self.entries = {}                   # address -> why
        self.bios = collections.Counter()
        self.tables = {}                    # table address -> (kind, site, [targets])
        self.unresolved = {}                # site -> text
        self.bad = {}                       # address -> reason
        self.consts = set()                 # code-looking constants (for --pointers)
        self.data = set()                   # bytes known to be tables

    def L(self, a): return struct.unpack('>I', self.rom[a:a + 4])[0]
    def W(self, a): return struct.unpack('>H', self.rom[a:a + 2])[0]
    def code_addr(self, a): return 0x100 <= a < self.size and a % 2 == 0 and a not in self.data

    def decode(self, a):
        try: i = next(self.cs.disasm(self.rom[a:a + 10], a, 1))
        except StopIteration: return None
        return i.size, i.mnemonic, i.op_str

    def add(self, a, why, todo):
        if a >= 0xC00000 and a < 0xC20000: self.bios[(BIOS.get(a, '?'), a)] += 1; return
        if not self.code_addr(a): self.bad.setdefault(a, 'target out of ROM from ' + why); return
        if a not in self.ins and a not in self.entries: todo.append(a)
        self.entries.setdefault(a, why)

    def run(self, todo):
        while todo:
            a = todo.pop()
            hist = []
            while True:
                if a in self.ins: break
                if not self.code_addr(a): self.bad.setdefault(a, 'flow runs out of code'); break
                r = self.decode(a)
                if r is None: self.bad.setdefault(a, 'invalid instruction'); break
                size, mn, ops = r
                self.ins[a] = r; hist.append((a, mn, ops))
                nxt = a + size
                ol = split_ops(ops)
                if mn in ('rts', 'rte', 'rtr', 'stop', 'illegal'): break
                if mn.startswith('trap') or mn == 'reset': pass
                m = BRANCH.match(mn)
                if m:
                    t = int(ol[-1][1:], 16); self.add(t, '%s at %06x' % (mn, a), todo)
                    if m.group(2) == 'ra': break
                    a = nxt; continue
                if DBCC.match(mn):
                    self.add(int(ol[-1][1:], 16), 'dbcc at %06x' % a, todo); a = nxt; continue
                if mn in ('jmp', 'jsr'):
                    op = ops.strip(); m1, m2 = ABS.match(op), PCREL.match(op)
                    if m1 or m2: self.add(int((m1 or m2).group(1), 16), '%s at %06x' % (mn, a), todo)
                    else: self.indirect(a, mn, op, hist, todo)
                    if mn == 'jmp': break
                    a = nxt; continue
                for o in ol:                # remember code-looking constants
                    mm = re.match(r'^#\$([0-9a-f]+)$', o) if mn.endswith('.l') or mn == 'move.l' else None
                    if mm: self.consts.add(int(mm.group(1), 16))
                    if mn.startswith('lea') or mn.startswith('pea'):
                        mm = ABS.match(o) or PCREL.match(o)
                        if mm: self.consts.add(int(mm.group(1), 16))
                a = nxt

    def table(self, kind, tab, base, site, todo):
        """Reads a jump table: stops at the first entry that is not a code address, or when the
        table runs into code already known (or into one of its own targets)."""
        targets, k, limit = [], tab, None
        while k + (4 if kind == 'long' else 2) <= self.size and len(targets) < 512:
            if limit is not None and k >= limit: break
            if k in self.ins: break
            t = self.L(k) if kind == 'long' else (base + struct.unpack('>h', self.rom[k:k + 2])[0]) & 0xFFFFFFFF
            if not self.code_addr(t) and not (0xC00000 <= t < 0xC20000): break
            targets.append(t); k += 4 if kind == 'long' else 2
            if t > tab and (limit is None or t < limit): limit = t
        if not targets: return False
        for b in range(tab, k): self.data.add(b)
        self.tables[tab] = (kind, site, targets)
        for t in targets: self.add(t, 'table %06x' % tab, todo)
        return True

    def bra_table(self, base, site, todo):
        """jmp T(pc,Dn.w) into a run of bra instructions of the same size (a branch table)."""
        k, n, size = base, 0, None
        while n < 256:
            r = self.decode(k) if self.code_addr(k) else None
            if not r or not r[1].startswith('bra') or (size and r[0] != size): break
            size = r[0]; self.add(k, 'branch table %06x' % base, todo); k += size; n += 1
        if n: self.tables[base] = ('bra', site, list(range(base, k, size)))
        return n > 0

    def plausible(self, a):
        """A constant is taken as a code address only if the code from there decodes cleanly up to its
        first rts/rte/bra/jmp (at most 200 instructions), without instructions games never use."""
        for _ in range(200):
            if not self.code_addr(a) or a in self.data: return False
            if a in self.ins: return True
            r = self.decode(a)
            if r is None: return False
            size, mn, ops = r
            if mn.startswith(('dc', 'trap', 'illegal', 'reset', 'stop', 'chk')): return False
            if mn.startswith('ori') and ops.startswith('#$0,') or self.W(a) in (0x0000, 0xFFFF): return False
            if mn in ('rts', 'rte') or mn.startswith(('bra', 'jmp')): return True
            a += size
        return False

    def indirect(self, a, mn, op, hist, todo):
        m = AREG.match(op)
        if m:                               # jmp (Am): find where Am was loaded
            reg = m.group(1)
            for (pa, pmn, pops) in reversed(hist[:-1][-8:]):
                pol = split_ops(pops)
                if not pol or pol[-1] != reg: continue
                mm = PCIDX.match(pol[0]) if pmn == 'movea.l' else None
                if mm and self.table('long', int(mm.group(1), 16), 0, a, todo): return
                mm = AIDX.match(pol[0]) if pmn == 'movea.l' else None
                if mm:                      # movea.l (d,Ak,Dn),Am: look for lea T,Ak before
                    disp = int(mm.group(1).replace('$', ''), 16) if mm.group(1) else 0
                    for (qa, qmn, qops) in reversed(hist[:-1][-12:]):
                        qol = split_ops(qops)
                        if qmn.startswith('lea') and qol and qol[-1] == mm.group(2):
                            mq = ABS.match(qol[0]) or PCREL.match(qol[0])
                            if mq and self.table('long', int(mq.group(1), 16) + disp, 0, a, todo): return
                            break
                if pmn.startswith('lea') and (ABS.match(pol[0]) or PCREL.match(pol[0])):
                    t = int((ABS.match(pol[0]) or PCREL.match(pol[0])).group(1), 16)
                    self.add(t, 'lea+jmp at %06x' % a, todo); return
                break
        m = PCIDX.match(op)
        if m:                               # jmp T2(pc,Dn.w) with move.w T(pc,Dn.w),Dn before
            base = int(m.group(1), 16)
            for (pa, pmn, pops) in reversed(hist[:-1][-6:]):
                pol = split_ops(pops)
                mm = PCIDX.match(pol[0]) if pmn == 'move.w' and pol else None
                if mm and pol[-1] == m.group(2):
                    if self.table('word', int(mm.group(1), 16), base, a, todo): return
            if self.bra_table(base, a, todo): return
            self.unresolved[a] = '%s %s (no offset table found)' % (mn, op); return
        prev = ' / '.join('%s %s' % (x[1], x[2]) for x in hist[-4:-1])
        self.unresolved[a] = '%s %s   <- %s' % (mn, op, prev)


def main():
    path = next((x for x in sys.argv[1:] if not x.startswith('--')), os.path.join(root, 'p1_be.bin'))
    rom = open(path, 'rb').read()
    assert rom[0x100:0x107] == b'NEO-GEO'
    mp = Mapper(rom); todo = []
    names = {0x64: 'VBLANK (level 1)', 0x68: 'TIMER (level 2)', 0x6C: 'COLD BOOT (level 3)'}
    for v in range(2, 64):                  # exception and interrupt vectors that point into the game
        t = mp.L(v * 4)
        if 0x100 <= t < len(rom): mp.add(t, 'vector %d %s' % (v, names.get(v * 4, '')), todo)
    for off, name in ((0x122, 'USER'), (0x128, 'PLAYER_START'), (0x12E, 'DEMO_END'), (0x134, 'COIN_SOUND')):
        assert mp.W(off) == 0x4EF9
        mp.add(mp.L(off + 2), name, todo)
    mp.run(todo)
    rounds, seeded = 0, 0
    if '--strict' not in sys.argv:
        # 1. every jsr/jmp abs.l of the ROM whose target decodes cleanly (calls from code not yet
        #    reached); 2. then, until nothing changes, the code-looking constants of the code found
        #    (routine addresses given to the task kernel or stored in RAM)
        for a in range(0, len(rom) - 6, 2):
            if mp.W(a) in (0x4EB9, 0x4EF9):
                t = mp.L(a + 2)
                if t not in mp.ins and mp.plausible(t): mp.add(t, 'call target (scan)', todo); seeded += 1
        mp.run(todo)
        while True:
            new = [c for c in mp.consts if mp.code_addr(c) and c not in mp.ins and c not in mp.entries
                   and mp.plausible(c)]
            if not new: break
            rounds += 1
            for c in new: mp.add(c, 'constant', todo)
            mp.run(todo)
    code = sum(r[0] for r in mp.ins.values())
    hist = collections.Counter(r[1].split('.')[0] for r in mp.ins.values())
    out = []
    p = out.append
    p('MAP OF THE 68000 CODE  (%s, %d bytes)' % (os.path.basename(path), len(rom)))
    p('instructions %d, code %d bytes (%.1f %% of the ROM), entry points %d, jump tables %d (%d targets)'
      % (len(mp.ins), code, 100 * code / len(rom), len(mp.entries), len(mp.tables),
         sum(len(t[2]) for t in mp.tables.values())))
    if seeded: p('seeded from %d call sites found by scanning, constants followed in %d rounds' % (seeded, rounds))
    starts = set(mp.ins)
    overl = [a for a, r in mp.ins.items() if any((a + k) in starts for k in range(2, r[0], 2))]
    p('overlapping decodes (to check by hand): %d' % len(overl) + ('  ' + ' '.join('%06X' % a for a in sorted(overl)[:16]) if overl else ''))
    traps = collections.Counter(r[2] for r in mp.ins.values() if r[1].startswith('trap'))
    p('traps used: ' + ', '.join('%s x%d -> %06X' % (t, c, mp.L(0x80 + 4 * int(t[2:], 16))) for t, c in sorted(traps.items())))
    p('\nBIOS CALLS'); [p('  %06X %-16s x%d' % (a, n, c)) for (n, a), c in sorted(mp.bios.items(), key=lambda x: x[0][1])]
    p('\nUNRESOLVED INDIRECT JUMPS (%d)' % len(mp.unresolved))
    [p('  %06X  %s' % (a, t)) for a, t in sorted(mp.unresolved.items())]
    p('\nJUMP TABLES'); [p('  %06X %-4s from %06X: %d entries' % (t, k, s, len(v))) for t, (k, s, v) in sorted(mp.tables.items())]
    p('\nPROBLEMS (%d)' % len(mp.bad)); [p('  %06X  %s' % (a, w)) for a, w in sorted(mp.bad.items())[:60]]
    p('\nMNEMONICS'); p('  ' + '  '.join('%s %d' % x for x in hist.most_common()))
    # code ranges
    rng, cur = [], None
    for a in sorted(mp.ins):
        if cur and a == cur[1]: cur[1] = a + mp.ins[a][0]
        else:
            cur = [a, a + mp.ins[a][0]]; rng.append(cur)
    p('\nCODE RANGES (%d)' % len(rng)); [p('  %06X-%06X' % (s, e)) for s, e in rng]
    d = os.path.join(root, 'build', 'check'); os.makedirs(d, exist_ok=True)
    open(os.path.join(d, 'codemap.txt'), 'w').write('\n'.join(out) + '\n')
    json.dump({'entries': {'%06x' % a: w for a, w in mp.entries.items()},
               'tables': {'%06x' % t: {'kind': k, 'site': '%06x' % s, 'targets': ['%06x' % x for x in v]}
                          for t, (k, s, v) in mp.tables.items()},
               'unresolved': {'%06x' % a: t for a, t in mp.unresolved.items()},
               'bios': {'%06x' % a: n for (n, a) in mp.bios}, 'ranges': [['%06x' % s, '%06x' % e] for s, e in rng]},
              open(os.path.join(d, 'codemap.json'), 'w'), indent=1)
    print('\n'.join(out[:2])); print('report:', os.path.join(d, 'codemap.txt'))


if __name__ == '__main__':
    main()
