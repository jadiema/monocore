"""68000 instruction decoder for the static translator (tools/recomp68k/recomp68k.py).

decode(mem, addr) -> Insn or None. mem(addr) returns the 16-bit big-endian word at addr (or None
outside the image). Effective addresses are tuples:
  ('D', n) ('A', n) ('ind', n) ('post', n) ('pre', n) ('disp', n, d16) ('idx', n, d8, (kind, r), long)
  ('absw', addr) ('absl', addr) ('pcd', addr) ('pci', base, (kind, r), long) ('imm', value)
PC-relative modes are resolved: 'pcd' holds the address, 'pci' the address without the index.
"""

CC = ['t', 'f', 'hi', 'ls', 'cc', 'cs', 'ne', 'eq', 'vc', 'vs', 'pl', 'mi', 'ge', 'lt', 'gt', 'le']
SZ = {1: 'b', 2: 'w', 4: 'l'}


class Insn:
    __slots__ = ('addr', 'len', 'op', 'size', 'src', 'dst', 'cond', 'target', 'data', 'mask', 'kind')

    def __init__(self, addr, op, size=0):
        self.addr, self.op, self.size = addr, op, size
        self.src = self.dst = self.cond = self.target = self.data = self.mask = self.kind = None
        self.len = 0

    def __repr__(self): return '%06X %s' % (self.addr, text(self))


def s8(v): return v - 0x100 if v & 0x80 else v
def s16(v): return v - 0x10000 if v & 0x8000 else v


class _Cur:
    def __init__(self, mem, addr):
        self.mem, self.pos = mem, addr

    def w(self):
        v = self.mem(self.pos)
        if v is None: raise ValueError('out of image')
        self.pos += 2
        return v

    def l(self):
        hi = self.w(); return (hi << 16) | self.w()


def _ea(cur, mode, reg, size):
    if mode == 0: return ('D', reg)
    if mode == 1: return ('A', reg)
    if mode == 2: return ('ind', reg)
    if mode == 3: return ('post', reg)
    if mode == 4: return ('pre', reg)
    if mode == 5: return ('disp', reg, s16(cur.w()))
    if mode == 6:
        e = cur.w()                                     # brief extension word; the 68000 ignores bits 10-8
        return ('idx', reg, s8(e & 0xFF), ('A' if e & 0x8000 else 'D', (e >> 12) & 7), bool(e & 0x800))
    if reg == 0: return ('absw', s16(cur.w()) & 0xFFFFFFFF)
    if reg == 1: return ('absl', cur.l())
    if reg == 2:
        pc = cur.pos; return ('pcd', (pc + s16(cur.w())) & 0xFFFFFFFF)
    if reg == 3:
        pc = cur.pos; e = cur.w()
        return ('pci', (pc + s8(e & 0xFF)) & 0xFFFFFFFF, ('A' if e & 0x8000 else 'D', (e >> 12) & 7), bool(e & 0x800))
    if reg == 4:
        if size == 1: return ('imm', cur.w() & 0xFF)
        if size == 2: return ('imm', cur.w())
        if size == 4: return ('imm', cur.l())
    return None


# addressing-mode classes (68000 manual): which modes an operand may use
def is_data(e): return e[0] != 'A'
def is_mem(e): return e[0] not in ('D', 'A')
def is_alt(e): return e[0] not in ('pcd', 'pci', 'imm')
def is_dalt(e): return is_data(e) and is_alt(e)
def is_malt(e): return is_mem(e) and is_alt(e)
def is_ctl(e): return e[0] in ('ind', 'disp', 'idx', 'absw', 'absl', 'pcd', 'pci')


def decode(mem, addr):
    try: return _decode(mem, addr)
    except ValueError: return None


def _decode(mem, addr):
    cur = _Cur(mem, addr)
    w = cur.w()
    top, r9, m6, m3, r0 = w >> 12, (w >> 9) & 7, (w >> 6) & 7, (w >> 3) & 7, w & 7
    szf = (w >> 6) & 3
    size = {0: 1, 1: 2, 2: 4}.get(szf)
    i = None

    def mk(op, sz=0): return Insn(addr, op, sz)

    def ea(mode, reg, sz): return _ea(cur, mode, reg, sz)

    if top == 0:
        if w & 0x0100:                                  # dynamic bit ops, movep
            if m3 == 1:
                i = mk('movep', 4 if w & 0x40 else 2)
                d = s16(cur.w())
                if w & 0x80: i.src, i.dst = ('D', r9), ('disp', r0, d)
                else: i.src, i.dst = ('disp', r0, d), ('D', r9)
            else:
                op = ['btst', 'bchg', 'bclr', 'bset'][szf]
                e = ea(m3, r0, 1)
                if e is None or (op == 'btst' and not is_data(e)) or (op != 'btst' and not is_dalt(e)): return None
                if op == 'btst' and e[0] == 'imm': return None
                i = mk(op, 4 if e[0] == 'D' else 1); i.src, i.dst = ('D', r9), e
        elif r9 == 4:                                   # static bit ops
            op = ['btst', 'bchg', 'bclr', 'bset'][szf]
            n = cur.w() & 0xFF
            e = ea(m3, r0, 1)
            if e is None or e[0] == 'imm' or (op == 'btst' and not is_data(e)) or (op != 'btst' and not is_dalt(e)): return None
            i = mk(op, 4 if e[0] == 'D' else 1); i.src, i.dst = ('imm', n), e
        else:
            op = {0: 'ori', 1: 'andi', 2: 'subi', 3: 'addi', 5: 'eori', 6: 'cmpi'}.get(r9)
            if op is None or size is None: return None
            if m3 == 7 and r0 == 4 and op in ('ori', 'andi', 'eori'):
                if size == 1: i = mk(op + '_ccr', 1); i.src = ('imm', cur.w() & 0xFF)
                elif size == 2: i = mk(op + '_sr', 2); i.src = ('imm', cur.w())
                else: return None
            else:
                imm = ('imm', cur.w() & 0xFF if size == 1 else cur.w() if size == 2 else cur.l())
                e = ea(m3, r0, size)
                if e is None or not is_dalt(e): return None
                i = mk(op, size); i.src, i.dst = imm, e
    elif top in (1, 2, 3):                              # move
        sz = {1: 1, 3: 2, 2: 4}[top]
        s = ea(m3, r0, sz)
        if s is None or (sz == 1 and s[0] == 'A'): return None
        if m6 == 1:
            if sz == 1: return None
            i = mk('movea', sz); i.src, i.dst = s, ('A', r9)
        else:
            d = ea(m6, r9, sz)
            if d is None or not is_dalt(d): return None
            i = mk('move', sz); i.src, i.dst = s, d
    elif top == 4:
        i = _decode4(w, cur, mk, ea, r9, m6, m3, r0, szf, size)
        if i is None: return None
    elif top == 5:
        if szf == 3:
            cond = (w >> 8) & 15
            if m3 == 1:
                i = mk('dbcc', 2); i.cond = cond; i.dst = ('D', r0)
                pc = cur.pos; i.target = (pc + s16(cur.w())) & 0xFFFFFFFF
            else:
                e = ea(m3, r0, 1)
                if e is None or not is_dalt(e): return None
                i = mk('scc', 1); i.cond = cond; i.dst = e
        else:
            e = ea(m3, r0, size)
            if e is None or not is_alt(e) or (size == 1 and e[0] == 'A'): return None
            i = mk('subq' if w & 0x100 else 'addq', size); i.src = ('imm', r9 or 8); i.dst = e
    elif top == 6:
        cond = (w >> 8) & 15
        d = w & 0xFF
        pc = cur.pos
        if d == 0: d = s16(cur.w())
        elif d == 0xFF: return None                     # 32-bit displacement: 68020
        else: d = s8(d)
        i = mk({0: 'bra', 1: 'bsr'}.get(cond, 'bcc')); i.cond = cond
        i.target = (pc + d) & 0xFFFFFFFF
    elif top == 7:
        if w & 0x100: return None
        i = mk('moveq', 4); i.src = ('imm', s8(w & 0xFF) & 0xFFFFFFFF); i.dst = ('D', r9)
    elif top in (8, 9, 11, 12, 13):
        i = _decode_arith(w, top, cur, mk, ea, r9, m6, m3, r0, szf, size)
        if i is None: return None
    elif top == 14:
        kinds = ['as', 'ls', 'rox', 'ro']
        if szf == 3:                                    # memory shift, word, by one
            if r9 & 4: return None
            e = ea(m3, r0, 2)
            if e is None or not is_malt(e): return None
            i = mk(kinds[r9 & 3] + ('l' if w & 0x100 else 'r'), 2); i.src = ('imm', 1); i.dst = e
        else:
            i = mk(kinds[m3 & 3] + ('l' if w & 0x100 else 'r'), size)
            i.src = ('D', r9) if m3 & 4 else ('imm', r9 or 8)
            i.dst = ('D', r0)
    else:                                               # line A / line F
        i = mk('linea' if top == 10 else 'linef')
    i.len = cur.pos - addr
    return i


def _decode4(w, cur, mk, ea, r9, m6, m3, r0, szf, size):
    if w == 0x4AFC: return mk('illegal')
    if w == 0x4E70: return mk('reset')
    if w == 0x4E71: return mk('nop')
    if w == 0x4E72: i = mk('stop'); i.src = ('imm', cur.w()); return i
    if w == 0x4E73: return mk('rte')
    if w == 0x4E75: return mk('rts')
    if w == 0x4E76: return mk('trapv')
    if w == 0x4E77: return mk('rtr')
    if w & 0xFFF0 == 0x4E40: i = mk('trap'); i.data = w & 15; return i
    if w & 0xFFF8 == 0x4E50: i = mk('link'); i.dst = ('A', r0); i.src = ('imm', s16(cur.w()) & 0xFFFFFFFF); return i
    if w & 0xFFF8 == 0x4E58: i = mk('unlk'); i.dst = ('A', r0); return i
    if w & 0xFFF8 == 0x4E60: i = mk('move_to_usp'); i.src = ('A', r0); return i
    if w & 0xFFF8 == 0x4E68: i = mk('move_from_usp'); i.dst = ('A', r0); return i
    if w & 0xFFC0 in (0x4E80, 0x4EC0):
        e = ea(m3, r0, 4)
        if e is None or not is_ctl(e): return None
        i = mk('jsr' if w & 0xFFC0 == 0x4E80 else 'jmp'); i.dst = e
        if e[0] in ('absw', 'absl', 'pcd'): i.target = e[1]
        return i
    if w & 0xF1C0 == 0x41C0:
        e = ea(m3, r0, 4)
        if e is None or not is_ctl(e): return None
        i = mk('lea', 4); i.src, i.dst = e, ('A', r9); return i
    if w & 0xF1C0 == 0x4180:
        e = ea(m3, r0, 2)
        if e is None or not is_data(e): return None
        i = mk('chk', 2); i.src, i.dst = e, ('D', r9); return i
    if w & 0xFFF8 == 0x4840: i = mk('swap', 4); i.dst = ('D', r0); return i
    if w & 0xFFF8 == 0x4880: i = mk('ext', 2); i.dst = ('D', r0); return i
    if w & 0xFFF8 == 0x48C0: i = mk('ext', 4); i.dst = ('D', r0); return i
    if w & 0xFFC0 == 0x4840:
        e = ea(m3, r0, 4)
        if e is None or not is_ctl(e): return None
        i = mk('pea', 4); i.src = e; return i
    if w & 0xFB80 == 0x4880:                            # movem
        sz = 4 if w & 0x40 else 2
        mask = cur.w()
        e = ea(m3, r0, sz)
        if e is None: return None
        if w & 0x400:                                   # memory to registers
            if not (is_ctl(e) or e[0] == 'post'): return None
            i = mk('movem_load', sz)
        else:
            if not ((is_ctl(e) and is_alt(e)) or e[0] == 'pre'): return None
            i = mk('movem_store', sz)
        i.mask, i.dst = mask, e
        return i
    if w & 0xFFC0 == 0x40C0:
        e = ea(m3, r0, 2)
        if e is None or not is_dalt(e): return None
        i = mk('move_from_sr', 2); i.dst = e; return i
    if w & 0xFFC0 == 0x44C0:
        e = ea(m3, r0, 2)
        if e is None or not is_data(e): return None
        i = mk('move_to_ccr', 2); i.src = e; return i
    if w & 0xFFC0 == 0x46C0:
        e = ea(m3, r0, 2)
        if e is None or not is_data(e): return None
        i = mk('move_to_sr', 2); i.src = e; return i
    if w & 0xFFC0 == 0x4800:
        e = ea(m3, r0, 1)
        if e is None or not is_dalt(e): return None
        i = mk('nbcd', 1); i.dst = e; return i
    if w & 0xFFC0 == 0x4AC0:
        e = ea(m3, r0, 1)
        if e is None or not is_dalt(e): return None
        i = mk('tas', 1); i.dst = e; return i
    if size is None: return None
    op = {0x4000: 'negx', 0x4200: 'clr', 0x4400: 'neg', 0x4600: 'not', 0x4A00: 'tst'}.get(w & 0xFF00)
    if op is None: return None
    e = ea(m3, r0, size)
    if e is None or not is_dalt(e): return None
    i = mk(op, size); i.dst = e
    return i


def _decode_arith(w, top, cur, mk, ea, r9, m6, m3, r0, szf, size):
    if m6 in (3, 7):                                    # word-size special forms
        if top == 8: op = 'divu' if m6 == 3 else 'divs'
        elif top == 12: op = 'mulu' if m6 == 3 else 'muls'
        else: op = {9: 'suba', 11: 'cmpa', 13: 'adda'}[top]
        sz = 2 if (m6 == 3 or op in ('divu', 'divs', 'mulu', 'muls')) else 4
        e = ea(m3, r0, sz)
        if e is None: return None
        if op in ('divu', 'divs', 'mulu', 'muls'):
            if not is_data(e): return None
            i = mk(op, 2); i.src, i.dst = e, ('D', r9)
        else:
            i = mk(op, sz); i.src, i.dst = e, ('A', r9)
        return i
    to_ea = bool(m6 & 4)
    if top in (8, 12) and m6 == 4 and m3 in (0, 1):     # sbcd / abcd
        i = mk('sbcd' if top == 8 else 'abcd', 1)
        i.src, i.dst = (('pre', r0), ('pre', r9)) if m3 else (('D', r0), ('D', r9))
        return i
    if top == 12 and m6 in (5, 6) and m3 in (0, 1):     # exg
        mode = (w >> 3) & 0x1F
        i = mk('exg', 4)
        if mode == 0x08: i.src, i.dst = ('D', r9), ('D', r0)
        elif mode == 0x09: i.src, i.dst = ('A', r9), ('A', r0)
        elif mode == 0x11: i.src, i.dst = ('D', r9), ('A', r0)
        else: return None
        return i
    if top in (9, 13) and to_ea and m3 in (0, 1):      # subx / addx
        i = mk('subx' if top == 9 else 'addx', size)
        i.src, i.dst = (('pre', r0), ('pre', r9)) if m3 else (('D', r0), ('D', r9))
        return i
    if top == 11:
        if to_ea and m3 == 1:
            i = mk('cmpm', size); i.src, i.dst = ('post', r0), ('post', r9); return i
        if to_ea:
            e = ea(m3, r0, size)
            if e is None or not is_dalt(e): return None
            i = mk('eor', size); i.src, i.dst = ('D', r9), e; return i
        e = ea(m3, r0, size)
        if e is None or (size == 1 and e[0] == 'A'): return None
        i = mk('cmp', size); i.src, i.dst = e, ('D', r9); return i
    op = {8: 'or', 9: 'sub', 12: 'and', 13: 'add'}[top]
    e = ea(m3, r0, size)
    if e is None: return None
    if to_ea:
        if not is_malt(e): return None
        i = mk(op, size); i.src, i.dst = ('D', r9), e
    else:
        if op in ('or', 'and') and not is_data(e): return None
        if size == 1 and e[0] == 'A': return None
        i = mk(op, size); i.src, i.dst = e, ('D', r9)
    return i


# --- text (for the comments of the generated code) ---

def ea_text(e):
    k = e[0]
    if k == 'D': return 'd%d' % e[1]
    if k == 'A': return 'a%d' % e[1] if e[1] < 7 else 'sp'
    if k == 'ind': return '(a%d)' % e[1]
    if k == 'post': return '(a%d)+' % e[1]
    if k == 'pre': return '-(a%d)' % e[1]
    if k == 'disp': return '%d(a%d)' % (e[2], e[1])
    if k == 'idx': return '%d(a%d,%s%d.%s)' % (e[2], e[1], e[3][0].lower(), e[3][1], 'l' if e[4] else 'w')
    if k == 'absw': return '$%X.w' % e[1]
    if k == 'absl': return '$%X.l' % e[1]
    if k == 'pcd': return '$%X(pc)' % e[1]
    if k == 'pci': return '$%X(pc,%s%d.%s)' % (e[1], e[2][0].lower(), e[2][1], 'l' if e[3] else 'w')
    if k == 'imm': return '#$%X' % e[1]
    return '?'


def text(i):
    op = i.op
    if op in ('bcc', 'scc', 'dbcc'): op = op[0:-2] + CC[i.cond] if op != 'dbcc' else 'db' + CC[i.cond]
    if i.size and op not in ('moveq', 'exg', 'lea', 'pea', 'swap', 'link'): op += '.' + SZ[i.size]
    args = []
    if i.op == 'trap': args.append('#%d' % i.data)
    if i.src is not None: args.append(ea_text(i.src))
    if i.op == 'movem_store': args.append('$%04X' % i.mask)
    if i.dst is not None: args.append(ea_text(i.dst))
    if i.op == 'movem_load': args.append('$%04X' % i.mask)
    if i.target is not None and i.op not in ('jsr', 'jmp'): args.append('$%X' % i.target)
    return op + (' ' + ', '.join(args) if args else '')
