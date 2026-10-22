#!/usr/bin/env python3
"""Static translator from 68000 machine code to C (then RV32IM through clang).

usage: python3 recomp68k.py IMAGE BASE OUT.c [options] [ENTRY ...]
  IMAGE        big-endian binary image, loaded at BASE (hexadecimal)
  ENTRY        entry points (hexadecimal)
  --map F.json entry points and jump-table targets from a code map (tools such as m68kmap.py)
  --alias SRC:DST:LEN  code copied at run time from SRC (in the image) to DST: translated as if at DST
  --chunk N    log2 of the address range of one C function (default 12, 4 KB)
  --vectors    also take every exception vector of the image as an entry point
  --idle ADDR  the branch at ADDR closes a wait loop: it calls M68K_IDLE(c) first (empty unless the
               port defines it), so that the port can let the processor sleep until an interrupt

Method (same as recomp6809.py): every instruction becomes C; registers are locals of the C function
that holds its 4 KB address range; branches are gotos; returns, indirect jumps and jumps to another
range go through a switch on the 68000 address ("dispatch"). The 68000 stack stays in emulated
memory, so code that swaps stacks (a task kernel) works unchanged. Flags are only computed when a
later instruction may read them (liveness over the control-flow graph). Interrupts are taken on
backward branches, after an SR change and on STOP. An address that is not translated calls
m68k_external(), which the port implements (BIOS replacement, or report of a missing entry point).

The generated file includes "m68k_rt.h" and "m68k_port.h", where the port defines the memory access
macros RD8/RD16/RD32/WR8/WR16/WR32 (24-bit addresses, word access at even addresses).
"""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import m68k
from m68k import CC

X, N, Z, V, C = 16, 8, 4, 2, 1
ALL = 31
CCUSE = [0, 0, C | Z, C | Z, C, C, Z, Z, V, V, N, N, N | V, N | V, N | V | Z, N | V | Z]
CCEXPR = ['1', '0', '!fc && !fz', 'fc || fz', '!fc', 'fc', '!fz', 'fz', '!fv', 'fv', '!fn', 'fn',
          'fn == fv', 'fn != fv', '!fz && fn == fv', 'fz || fn != fv']
MASK = {1: '0xFFu', 2: '0xFFFFu', 4: '0xFFFFFFFFu'}
BITS = {1: 8, 2: 16, 4: 32}
MSB = {1: 7, 2: 15, 4: 31}
REGS = ['d%d' % i for i in range(8)] + ['a%d' % i for i in range(8)]


def sx(v, s):
    """C expression: v sign-extended from size s to 32 bits."""
    return {1: '(uint32_t)(int32_t)(int8_t)(%s)', 2: '(uint32_t)(int32_t)(int16_t)(%s)', 4: '(%s)'}[s] % v


def msk(v, s): return v if s == 4 else '((%s) & %s)' % (v, MASK[s])


class Image:
    def __init__(self, data, base, aliases):
        self.data, self.base, self.aliases = data, base, aliases

    def phys(self, a):
        for src, dst, ln in self.aliases:
            if dst <= a < dst + ln: return a - dst + src
        return a

    def word(self, a):
        a = self.phys(a) - self.base
        if 0 <= a < len(self.data) - 1 and a % 2 == 0: return (self.data[a] << 8) | self.data[a + 1]
        return None


class Translator:
    def __init__(self, img, chunk_bits=12, idle=()):
        self.img, self.cb, self.idle = img, chunk_bits, set(idle)
        self.ins = {}            # address -> Insn
        self.labels = set()
        self.missing = set()     # targets that could not be decoded

    # --- discovery ---
    def mem(self, a): return self.img.word(a)

    def discover(self, entries):
        todo = list(entries)
        self.labels |= set(entries)
        while todo:
            a = todo.pop()
            while a not in self.ins:
                i = m68k.decode(self.mem, a)
                if i is None: self.missing.add(a); break
                self.ins[a] = i
                nxt = a + i.len
                op = i.op
                if i.target is not None and self.mem(i.target) is not None:
                    self.labels.add(i.target); todo.append(i.target)
                if op in ('rts', 'rte', 'rtr', 'jmp', 'bra', 'illegal', 'linea', 'linef'): break
                if op in ('jsr', 'bsr', 'trap', 'trapv', 'chk', 'divu', 'divs', 'stop', 'move_to_sr',
                          'andi_sr', 'eori_sr', 'ori_sr'):
                    self.labels.add(nxt)    # return point of a call or an exception
                a = nxt
        self.labels = {l for l in self.labels if l in self.ins}

    # --- flag liveness ---
    def flags(self, i):
        """(use, kill, gen) for one instruction."""
        op, d = i.op, i.dst
        if op in ('move', 'moveq', 'and', 'or', 'eor', 'not', 'clr', 'tst', 'andi', 'ori', 'eori', 'swap',
                  'ext', 'mulu', 'muls', 'tas'): return 0, N | Z | V | C, N | Z | V | C
        if op in ('add', 'sub', 'addi', 'subi', 'neg'): return 0, ALL, ALL
        if op in ('addq', 'subq'): return (0, 0, 0) if d[0] == 'A' else (0, ALL, ALL)
        if op in ('addx', 'subx', 'negx', 'abcd', 'sbcd', 'nbcd'): return X | Z, ALL, ALL
        if op in ('cmp', 'cmpa', 'cmpi', 'cmpm'): return 0, N | Z | V | C, N | Z | V | C
        if op in ('btst', 'bset', 'bclr', 'bchg'): return 0, Z, Z
        if op in ('asl', 'asr', 'lsl', 'lsr'):
            return (0, ALL, ALL) if i.src[0] == 'imm' else (0, N | Z | V | C, ALL)
        if op in ('rol', 'ror'): return 0, N | Z | V | C, N | Z | V | C
        if op in ('roxl', 'roxr'): return X, ALL, ALL
        if op in ('divu', 'divs'): return ALL, ALL, ALL
        if op == 'chk': return ALL, ALL, ALL
        if op in ('scc', 'bcc', 'dbcc'): return CCUSE[i.cond], 0, 0
        if op in ('move_to_ccr', 'move_to_sr', 'stop'): return 0, ALL, ALL
        if op in ('andi_ccr', 'ori_ccr', 'eori_ccr', 'andi_sr', 'ori_sr', 'eori_sr'): return ALL, ALL, ALL
        if op in ('move_from_sr', 'trap', 'trapv', 'illegal', 'linea', 'linef'): return ALL, 0, 0
        if op in ('rte', 'rtr'): return 0, ALL, ALL
        return 0, 0, 0

    def succ(self, i):
        """Successors inside the translated code; None means unknown (all flags live)."""
        op, nxt = i.op, i.addr + i.len
        known = lambda t: t in self.ins
        if op in ('rts', 'rte', 'rtr', 'trap', 'trapv', 'illegal', 'linea', 'linef', 'divu', 'divs',
                  'chk', 'stop', 'move_to_sr', 'andi_sr', 'ori_sr', 'eori_sr'): return None
        if op in ('jmp', 'jsr', 'bra', 'bsr'):
            return [i.target] if i.target is not None and known(i.target) else None
        if op in ('bcc', 'dbcc'):
            s = [nxt] + ([i.target] if known(i.target) else [])
            return s if known(nxt) and known(i.target) else None
        return [nxt] if known(nxt) else None

    def liveness(self):
        info = {a: self.flags(i) for a, i in self.ins.items()}
        succ = {a: self.succ(i) for a, i in self.ins.items()}
        live_in = {a: ALL for a in self.ins}    # start from "all live" and shrink to the fixpoint
        changed = True
        order = sorted(self.ins, reverse=True)
        while changed:
            changed = False
            for a in order:
                s = succ[a]
                out = ALL if s is None else 0
                if s is not None:
                    for t in s: out |= live_in[t]
                use, kill, _ = info[a]
                new = use | (out & ~kill)
                if new != live_in[a]: live_in[a] = new; changed = True
        self.live_out = {}
        for a in self.ins:
            s = succ[a]
            out = ALL if s is None else 0
            if s is not None:
                for t in s: out |= live_in[t]
            self.live_out[a] = out

    # --- code generation ---
    def chunk(self, a): return a >> self.cb

    def generate(self, out):
        order = sorted(self.ins)
        for k, a in enumerate(order):
            i = self.ins[a]; nxt = a + i.len
            if self.ends(i) or nxt not in self.ins: continue
            if k + 1 == len(order) or order[k + 1] != nxt or self.chunk(nxt) != self.chunk(a): self.labels.add(nxt)
        self.liveness()
        chunks = sorted({self.chunk(a) for a in self.ins})
        w = []
        w.append('// Generated by tools/recomp68k/recomp68k.py: do not edit.')
        w.append('#include "m68k_rt.h"\n#include "m68k_port.h"\n')
        w.append('#define LOAD d0=c->d[0];d1=c->d[1];d2=c->d[2];d3=c->d[3];d4=c->d[4];d5=c->d[5];d6=c->d[6];d7=c->d[7];'
                 'a0=c->a[0];a1=c->a[1];a2=c->a[2];a3=c->a[3];a4=c->a[4];a5=c->a[5];a6=c->a[6];a7=c->a[7];'
                 'fx=c->x;fn=c->n;fz=c->z;fv=c->v;fc=c->c;srh=c->srh;')
        w.append('#define STORE c->d[0]=d0;c->d[1]=d1;c->d[2]=d2;c->d[3]=d3;c->d[4]=d4;c->d[5]=d5;c->d[6]=d6;c->d[7]=d7;'
                 'c->a[0]=a0;c->a[1]=a1;c->a[2]=a2;c->a[3]=a3;c->a[4]=a4;c->a[5]=a5;c->a[6]=a6;c->a[7]=a7;'
                 'c->x=fx;c->n=fn;c->z=fz;c->v=fv;c->c=fc;c->srh=srh;')
        w.append('#define SRVAL ((srh << 8) | (fx << 4) | (fn << 3) | (fz << 2) | (fv << 1) | fc)')
        w.append('#define SETCCR(v) do { uint32_t _v = (v); fx = (_v >> 4) & 1; fn = (_v >> 3) & 1; fz = (_v >> 2) & 1; '
                 'fv = (_v >> 1) & 1; fc = _v & 1; } while (0)')
        w.append('#define SETSR(v) do { uint32_t _s = (v); SETCCR(_s); _s = (_s >> 8) & 0xA7; '
                 'if ((_s ^ srh) & 0x20) { uint32_t _t = a7; a7 = c->osp; c->osp = _t; } srh = _s; } while (0)')
        w.append('#ifndef M68K_IACK\n#define M68K_IACK(c, level)\n#endif')
        w.append('#ifndef M68K_IDLE   // called on the branches given with --idle: a wait loop\n'
                 '#define M68K_IDLE(c)\n#endif')
        w.append('#ifndef M68K_TICK   // called on backward branches and on dispatch: lets a test bench measure time\n'
                 '#define M68K_TICK(c, pc)\n#endif\n')
        for ch in chunks: w.append('static uint32_t ch_%03x(m68k_t *c, uint32_t pc);' % ch)
        w.append('')
        for ch in chunks: self.gen_chunk(ch, w)
        w.append('void m68k_run(m68k_t *c) {\n  uint32_t pc = c->pc & 0xFFFFFFu;\n  for (;;) {')
        w.append('    switch (pc >> %d) {' % self.cb)
        for ch in chunks: w.append('    case 0x%x: pc = ch_%03x(c, pc); break;' % (ch, ch))
        w.append('    default: pc = m68k_external(c, pc); break;\n    }')
        w.append('    if (pc == M68K_EXIT) return;\n    pc &= 0xFFFFFFu;\n  }\n}')
        open(out, 'w').write('\n'.join(w) + '\n')

    def gen_chunk(self, ch, w):
        addrs = sorted(a for a in self.ins if self.chunk(a) == ch)
        labels = sorted(a for a in addrs if a in self.labels)
        self.cur = ch
        w.append('static uint32_t ch_%03x(m68k_t *c, uint32_t pc) {' % ch)
        w.append('  uint32_t d0, d1, d2, d3, d4, d5, d6, d7, a0, a1, a2, a3, a4, a5, a6, a7, fx, fn, fz, fv, fc, srh, vec;')
        w.append('  LOAD;')
        # dispatch: a table with one entry per even address of the range (labels as values, a GNU C
        # extension of gcc and clang): one load and one jump, where a switch on a hundred sparse
        # addresses compiles to a binary search
        n = 1 << (self.cb - 1); lab = set(labels); base = ch << self.cb
        w.append('  static void *const jt[%d] = {' % n)
        row = []
        for k in range(n):
            a = base + 2 * k
            row.append('&&L_%06x' % a if a in lab else '&&nolabel')
            if len(row) == 8: w.append('    ' + ', '.join(row) + ','); row = []
        if row: w.append('    ' + ', '.join(row) + ',')
        w.append('  };')
        w.append('dispatch:\n  M68K_TICK(c, pc);\n  pc &= 0xFFFFFFu;')
        w.append('  if ((pc >> %d) != 0x%x) goto leave;' % (self.cb, ch))
        w.append('  if (pc & 1) goto ext;')
        w.append('  goto *jt[(pc & 0x%x) >> 1];' % ((1 << self.cb) - 1))
        w.append('nolabel:\n  goto ext;')
        w.append('ext:\n  STORE; pc = m68k_external(c, pc); LOAD;\n  if (pc == M68K_EXIT) goto leave;\n  goto dispatch;')
        w.append('irq:\n  if (c->ipl > (srh & 7)) {\n    uint32_t lvl = c->ipl, sr = SRVAL;')
        w.append('    if (!(srh & 0x20)) { uint32_t t = a7; a7 = c->osp; c->osp = t; }')
        w.append('    srh = 0x20 | lvl; a7 -= 4; WR32(a7, pc); a7 -= 2; WR16(a7, sr);')
        w.append('    M68K_IACK(c, lvl); pc = RD32((24 + lvl) * 4);\n  }\n  goto dispatch;')
        w.append('exc: {\n    uint32_t sr = SRVAL;')
        w.append('    if (!(srh & 0x20)) { uint32_t t = a7; a7 = c->osp; c->osp = t; }')
        w.append('    srh = (srh & 0x07) | 0x20; a7 -= 4; WR32(a7, pc); a7 -= 2; WR16(a7, sr);')
        w.append('    pc = RD32(vec * 4); goto dispatch;\n  }')
        w.append('leave:\n  STORE; return pc;')
        for k, a in enumerate(addrs):
            i = self.ins[a]
            if a in self.labels: w.append('L_%06x:' % a)
            w.append('  // %06X  %s' % (a, m68k.text(i)))
            try:
                body = self.gen_insn(i)
                if a in self.idle: body = ['STORE; M68K_IDLE(c); LOAD;'] + body
            except NotImplementedError as e:
                body = ['pc = 0x%x; goto ext; /* not translated: %s */' % (a, e)]
            w.extend('  ' + l for l in body)
            nxt = a + i.len
            if not self.ends(i) and nxt not in self.ins:
                w.append('  pc = 0x%x; goto ext;' % nxt)     # falls into code that is not translated
            elif not self.ends(i) and self.chunk(nxt) != ch:
                w.append('  pc = 0x%x; goto leave;' % nxt)   # falls into the next range
            elif not self.ends(i) and (k + 1 == len(addrs) or addrs[k + 1] != nxt):
                w.append('  goto L_%06x;' % nxt)             # the next instruction is not the next one here
        w.append('}\n')

    def ends(self, i):
        return i.op in ('rts', 'rte', 'rtr', 'jmp', 'bra', 'illegal', 'linea', 'linef', 'trap', 'stop',
                        'jsr', 'bsr', 'move_to_sr', 'andi_sr', 'ori_sr', 'eori_sr')

    def jump(self, t, src, poll=True):
        """Code that continues at t (a constant)."""
        if t in self.labels and self.chunk(t) == self.cur:
            if poll and t <= src: return 'M68K_TICK(c, 0x%x); if (c->ipl > (srh & 7)) { pc = 0x%x; goto irq; } goto L_%06x;' % (t, t, t)
            return 'goto L_%06x;' % t
        return 'pc = 0x%x; goto dispatch;' % (t & 0xFFFFFF)

    # --- operands ---
    def addr(self, e):
        k = e[0]
        if k == 'ind': return 'a%d' % e[1]
        if k == 'disp': return '(a%d + %d)' % (e[1], e[2])
        if k == 'idx':
            x = '%s%d' % (e[3][0].lower(), e[3][1])
            return '(a%d + %d + %s)' % (e[1], e[2], x if e[4] else sx(x, 2))
        if k in ('absw', 'absl', 'pcd'): return '0x%xu' % e[1]
        if k == 'pci':
            x = '%s%d' % (e[2][0].lower(), e[2][1])
            return '(0x%xu + %s)' % (e[1], x if e[3] else sx(x, 2))
        raise NotImplementedError('no address for ' + k)

    def operand(self, e, s, name, code):
        """Prepares an operand; returns an object with read() and write(v)."""
        g = self
        k = e[0]

        class Opnd: pass
        o = Opnd()
        if k == 'D':
            r = 'd%d' % e[1]
            o.read = lambda: msk(r, s)
            o.write = lambda v: '%s = %s;' % (r, v) if s == 4 else '%s = (%s & ~%s) | ((%s) & %s);' % (r, r, MASK[s], v, MASK[s])
        elif k == 'A':
            r = 'a%d' % e[1]
            o.read = lambda: msk(r, s)
            o.write = lambda v: '%s = %s;' % (r, sx(v, s))
        elif k == 'imm':
            o.read = lambda: '0x%xu' % (e[1] & int(MASK[s][:-1], 16))
            o.write = None
        else:
            if k in ('post', 'pre'):
                inc = 2 if (s == 1 and e[1] == 7) else s
                if k == 'pre': code.append('a%d -= %d; uint32_t %s = a%d;' % (e[1], inc, name, e[1]))
                else: code.append('uint32_t %s = a%d; a%d += %d;' % (name, e[1], e[1], inc))
            else:
                code.append('uint32_t %s = %s;' % (name, g.addr(e)))
            o.read = lambda: 'RD%d(%s)' % (BITS[s], name)
            o.write = lambda v: 'WR%d(%s, %s);' % (BITS[s], name, v)
        return o

    # flags helpers (r: masked result expression, s: size)
    def nz(self, live, r, s):
        out = []
        if live & N: out.append('fn = (%s >> %d) & 1;' % (r, MSB[s]))
        if live & Z: out.append('fz = (%s == 0);' % r)
        return out

    def logic_flags(self, live, r, s):
        out = self.nz(live, r, s)
        if live & V: out.append('fv = 0;')
        if live & C: out.append('fc = 0;')
        return out

    def gen_insn(self, i):
        op, s, a = i.op, i.size, i.addr
        live = self.live_out[a]
        _, kill, gen = self.flags(i)
        need = live & gen                      # flags to compute
        nxt = a + i.len
        code = []
        if op in ('move', 'movea'):
            src = self.operand(i.src, s, 's', code)
            code.append('uint32_t v = %s;' % src.read())
            dst = self.operand(i.dst, s, 'ea', code)
            code.append(dst.write('v'))
            if op == 'move': code += self.logic_flags(need, 'v', s)
            return self.block(code)
        if op == 'moveq':
            code.append('d%d = 0x%xu;' % (i.dst[1], i.src[1]))
            code += self.logic_flags(need, '0x%xu' % i.src[1], 4)
            return code
        if op in ('add', 'sub', 'addi', 'subi', 'addq', 'subq', 'cmp', 'cmpi', 'cmpm'):
            base = op.rstrip('iq').replace('cmpm', 'cmp')
            if op in ('addq', 'subq') and i.dst[0] == 'A':
                code.append('a%d %s= %du;' % (i.dst[1], '+' if op == 'addq' else '-', i.src[1]))
                return code
            src = self.operand(i.src, s, 's', code)
            code.append('uint32_t sv = %s;' % src.read())
            dst = self.operand(i.dst, s, 'ea', code)
            code.append('uint32_t dv = %s;' % dst.read())
            if base == 'add':
                code.append('uint32_t r = %s;' % msk('dv + sv', s))
                code += self.add_flags(need, s, 'sv', 'dv', 'r', 'dv + sv')
            else:
                code.append('uint32_t r = %s;' % msk('dv - sv', s))
                code += self.sub_flags(need if base == 'sub' else need & ~X, s, 'sv', 'dv', 'r')
            if base != 'cmp': code.append(dst.write('r'))
            return self.block(code)
        if op in ('adda', 'suba', 'cmpa'):
            src = self.operand(i.src, s, 's', code)
            code.append('uint32_t sv = %s;' % sx(src.read(), s))
            r = 'a%d' % i.dst[1]
            if op == 'adda': code.append('%s += sv;' % r)
            elif op == 'suba': code.append('%s -= sv;' % r)
            else:
                code.append('uint32_t dv = %s, r = dv - sv;' % r)
                code += self.sub_flags(need, 4, 'sv', 'dv', 'r')
            return self.block(code)
        if op in ('addx', 'subx'):
            src = self.operand(i.src, s, 's', code)
            code.append('uint32_t sv = %s;' % src.read())
            dst = self.operand(i.dst, s, 'ea', code)
            code.append('uint32_t dv = %s;' % dst.read())
            if op == 'addx':
                code.append('uint32_t r = %s;' % msk('dv + sv + fx', s))
                if need & (X | C):
                    cexpr = '((dv + sv + fx) >> %d) & 1' % BITS[s] if s < 4 else '(r < dv || (fx && r == dv))'
                    code.append('uint32_t cy = %s;' % cexpr)
                if need & V: code.append('fv = (((sv ^ r) & (dv ^ r)) >> %d) & 1;' % MSB[s])
            else:
                code.append('uint32_t r = %s;' % msk('dv - sv - fx', s))
                if need & (X | C):
                    cexpr = '(sv + fx > dv)' if s < 4 else '(sv > dv || (fx && sv == dv))'
                    code.append('uint32_t cy = %s;' % cexpr)
                if need & V: code.append('fv = (((sv ^ dv) & (r ^ dv)) >> %d) & 1;' % MSB[s])
            if need & (X | C): code.append('fx = fc = cy;' if need & X else 'fc = cy;')
            if need & N: code.append('fn = (r >> %d) & 1;' % MSB[s])
            if need & Z: code.append('fz = fz & (r == 0);')
            code.append(dst.write('r'))
            return self.block(code)
        if op in ('neg', 'negx'):
            dst = self.operand(i.dst, s, 'ea', code)
            code.append('uint32_t sv = %s;' % dst.read())
            if op == 'neg':
                code.append('uint32_t r = %s;' % msk('0u - sv', s))
                if need & (X | C): code.append('%s(sv != 0);' % ('fx = fc = ' if need & X else 'fc = '))
                if need & N: code.append('fn = (r >> %d) & 1;' % MSB[s])
                if need & Z: code.append('fz = (r == 0);')
            else:
                code.append('uint32_t r = %s;' % msk('0u - sv - fx', s))
                if need & (X | C): code.append('%s((sv | fx) != 0);' % ('fx = fc = ' if need & X else 'fc = '))
                if need & N: code.append('fn = (r >> %d) & 1;' % MSB[s])
                if need & Z: code.append('fz = fz & (r == 0);')
            if need & V: code.append('fv = ((sv & r) >> %d) & 1;' % MSB[s])
            code.append(dst.write('r'))
            return self.block(code)
        if op in ('and', 'or', 'eor', 'andi', 'ori', 'eori'):
            cop = {'and': '&', 'or': '|', 'eor': '^'}[op.rstrip('i')]
            src = self.operand(i.src, s, 's', code)
            code.append('uint32_t sv = %s;' % src.read())
            dst = self.operand(i.dst, s, 'ea', code)
            code.append('uint32_t r = %s %s sv;' % (dst.read(), cop))
            code.append(dst.write('r'))
            code += self.logic_flags(need, 'r', s)
            return self.block(code)
        if op in ('not', 'clr', 'tst', 'tas'):
            dst = self.operand(i.dst, s, 'ea', code)
            if op == 'clr':
                code.append(dst.write('0'))
                if need & N: code.append('fn = 0;')
                if need & Z: code.append('fz = 1;')
                if need & V: code.append('fv = 0;')
                if need & C: code.append('fc = 0;')
                return self.block(code)
            code.append('uint32_t r = %s;' % dst.read())
            if op == 'not': code.append('r = %s;' % msk('~r', s)); code.append(dst.write('r'))
            code += self.logic_flags(need, 'r', s)
            if op == 'tas': code.append(dst.write('r | 0x80'))
            return self.block(code)
        if op in ('ori_ccr', 'andi_ccr', 'eori_ccr', 'ori_sr', 'andi_sr', 'eori_sr'):
            cop = {'o': '|', 'a': '&', 'e': '^'}[op[0]]
            if op.endswith('ccr'):
                code.append('SETCCR((SRVAL & 0x1F) %s 0x%xu);' % (cop, i.src[1] & 0x1F))
                return self.block(code)
            code.append('SETSR(SRVAL %s 0x%xu);' % (cop, i.src[1]))
            code.append('pc = 0x%x; goto irq;' % nxt)
            return self.block(code)
        if op == 'move_to_ccr':
            src = self.operand(i.src, 2, 's', code)
            code.append('SETCCR(%s);' % src.read())
            return self.block(code)
        if op == 'move_to_sr':
            src = self.operand(i.src, 2, 's', code)
            code.append('SETSR(%s);' % src.read())
            code.append('pc = 0x%x; goto irq;' % nxt)
            return self.block(code)
        if op == 'move_from_sr':
            dst = self.operand(i.dst, 2, 'ea', code)
            code.append(dst.write('SRVAL'))
            return self.block(code)
        if op == 'move_to_usp': return ['c->osp = a%d;' % i.src[1]]
        if op == 'move_from_usp': return ['a%d = c->osp;' % i.dst[1]]
        if op in ('asl', 'asr', 'lsl', 'lsr', 'rol', 'ror', 'roxl', 'roxr'):
            return self.block(self.shift(i, need))
        if op in ('btst', 'bset', 'bclr', 'bchg'):
            if i.src[0] == 'imm': code.append('uint32_t b = %d;' % (i.src[1] & (31 if s == 4 else 7)))
            else: code.append('uint32_t b = d%d & %d;' % (i.src[1], 31 if s == 4 else 7))
            dst = self.operand(i.dst, s, 'ea', code)
            code.append('uint32_t v = %s;' % dst.read())
            if need & Z: code.append('fz = !((v >> b) & 1);')
            if op != 'btst':
                code.append({'bset': 'v |= 1u << b;', 'bclr': 'v &= ~(1u << b);', 'bchg': 'v ^= 1u << b;'}[op])
                code.append(dst.write('v'))
            return self.block(code)
        if op in ('mulu', 'muls'):
            src = self.operand(i.src, 2, 's', code)
            r = 'd%d' % i.dst[1]
            if op == 'mulu': code.append('uint32_t r = (%s & 0xFFFFu) * %s;' % (r, src.read()))
            else: code.append('uint32_t r = (uint32_t)((int32_t)(int16_t)%s * (int32_t)(int16_t)%s);' % (r, src.read()))
            code.append('%s = r;' % r)
            code += self.logic_flags(need, 'r', 4)
            return self.block(code)
        if op in ('divu', 'divs'):
            src = self.operand(i.src, 2, 's', code)
            r = 'd%d' % i.dst[1]
            code.append('uint32_t sv = %s;' % src.read())
            code.append('if (sv == 0) { pc = 0x%x; vec = 5; goto exc; }' % nxt)
            if op == 'divu':
                code.append('uint32_t q = %s / sv, m = %s %% sv;' % (r, r))
                code.append('if (q > 0xFFFFu) fv = 1;')
            else:
                code.append('int32_t n = (int32_t)%s, dv = (int16_t)sv;' % r)
                code.append('int32_t q = (n == (int32_t)0x80000000 && dv == -1) ? 0x8000 * 2 : n / dv;')
                code.append('int32_t m = (n == (int32_t)0x80000000 && dv == -1) ? 0 : n % dv;')
                code.append('if (q != (int16_t)q) fv = 1;')
            code.append('else { %s = ((uint32_t)m << 16) | ((uint32_t)q & 0xFFFFu); fn = ((uint32_t)q >> 15) & 1; '
                        'fz = ((q & 0xFFFF) == 0); fv = 0; fc = 0; }' % r)
            return self.block(code)
        if op == 'chk':
            src = self.operand(i.src, 2, 's', code)
            code.append('int32_t v = (int16_t)d%d, bound = (int16_t)%s;' % (i.dst[1], src.read()))
            code.append('fz = (v == 0); fv = 0; fc = 0;')
            code.append('if (v < 0 || v > bound) { fn = v < 0; pc = 0x%x; vec = 6; goto exc; }' % nxt)
            return self.block(code)
        if op in ('abcd', 'sbcd'):
            src = self.operand(i.src, 1, 's', code)
            code.append('uint32_t sv = %s;' % src.read())
            dst = self.operand(i.dst, 1, 'ea', code)
            code.append('uint32_t dv = %s;' % dst.read())
            if op == 'abcd':
                code.append('uint32_t r = (sv & 15) + (dv & 15) + fx; uint32_t vv = ~r;')
                code.append('if (r > 9) r += 6;')
                code.append('r += (sv & 0xF0) + (dv & 0xF0);')
                code.append('uint32_t cy = r > 0x99; if (cy) r -= 0xA0;')
            else:
                code.append('uint32_t r = (dv & 15) - (sv & 15) - fx; uint32_t vv = ~r;')
                code.append('if (r > 9) r -= 6;')
                code.append('r += (dv & 0xF0) - (sv & 0xF0);')
                code.append('uint32_t cy = r > 0x99; if (cy) r += 0xA0;')
            code.append('fv = ((vv & r) >> 7) & 1; fn = (r >> 7) & 1; fx = fc = cy; r &= 0xFF; if (r) fz = 0;')
            code.append(dst.write('r'))
            return self.block(code)
        if op == 'nbcd':
            dst = self.operand(i.dst, 1, 'ea', code)
            code.append('uint32_t dv = %s;' % dst.read())
            code.append('uint32_t r = (0x9Au - dv - fx) & 0xFF;')
            code.append('if (r != 0x9A) { uint32_t vv = ~r; if ((r & 15) == 10) r = (r & 0xF0) + 0x10; r &= 0xFF; '
                        'fv = ((vv & r) >> 7) & 1; %s if (r) fz = 0; fc = fx = 1; } else { fv = 0; fc = fx = 0; }'
                        % dst.write('r'))
            code.append('fn = (r >> 7) & 1;')
            return self.block(code)
        if op == 'swap':
            r = 'd%d' % i.dst[1]
            code.append('%s = (%s >> 16) | (%s << 16);' % (r, r, r))
            code += self.logic_flags(need, r, 4)
            return code
        if op == 'ext':
            r = 'd%d' % i.dst[1]
            if s == 2: code.append('%s = (%s & 0xFFFF0000u) | (%s & 0xFFFFu);' % (r, r, sx(r, 1)))
            else: code.append('%s = %s;' % (r, sx(r, 2)))
            code += self.logic_flags(need, msk(r, s), s)
            return code
        if op == 'exg':
            x, y = REGS[(8 if i.src[0] == 'A' else 0) + i.src[1]], REGS[(8 if i.dst[0] == 'A' else 0) + i.dst[1]]
            return ['{ uint32_t t = %s; %s = %s; %s = t; }' % (x, x, y, y)]
        if op == 'lea': return ['a%d = %s;' % (i.dst[1], self.addr(i.src))]
        if op == 'pea': return ['{ uint32_t ea = %s; a7 -= 4; WR32(a7, ea); }' % self.addr(i.src)]
        if op == 'link':
            r = 'a%d' % i.dst[1]
            return ['a7 -= 4; WR32(a7, %s); %s = a7; a7 += 0x%xu;' % (r, r, i.src[1])]
        if op == 'unlk':
            if i.dst[1] == 7: return ['a7 = RD32(a7);']
            r = 'a%d' % i.dst[1]
            return ['a7 = %s; %s = RD32(a7); a7 += 4;' % (r, r)]
        if op == 'movem_store':
            regs = [REGS[k] for k in range(16) if i.mask >> (15 - k if i.dst[0] == 'pre' else k) & 1]
            if i.dst[0] == 'pre':
                r = 'a%d' % i.dst[1]
                code.append('uint32_t ea = %s;' % r)
                for reg in reversed(regs):
                    code.append('ea -= %d; WR%d(ea, %s);' % (s, BITS[s], reg))
                code.append('%s = ea;' % r)
            else:
                code.append('uint32_t ea = %s;' % self.addr(i.dst))
                for reg in regs: code.append('WR%d(ea, %s); ea += %d;' % (BITS[s], reg, s))
            return self.block(code)
        if op == 'movem_load':
            regs = [REGS[k] for k in range(16) if i.mask >> k & 1]
            if i.dst[0] == 'post': code.append('uint32_t ea = a%d;' % i.dst[1])
            else: code.append('uint32_t ea = %s;' % self.addr(i.dst))
            for reg in regs:
                code.append('%s = %s; ea += %d;' % (reg, sx('RD%d(ea)' % BITS[s], s), s))
            if i.dst[0] == 'post': code.append('a%d = ea;' % i.dst[1])
            return self.block(code)
        if op == 'movep':
            if i.src[0] == 'D':
                r, e = 'd%d' % i.src[1], i.dst
                code.append('uint32_t ea = %s;' % self.addr(e))
                sh = [24, 16, 8, 0] if s == 4 else [8, 0]
                for k, b in enumerate(sh): code.append('WR8(ea + %d, (%s >> %d) & 0xFF);' % (2 * k, r, b))
            else:
                r, e = 'd%d' % i.dst[1], i.src
                code.append('uint32_t ea = %s;' % self.addr(e))
                if s == 4: code.append('%s = (RD8(ea) << 24) | (RD8(ea + 2) << 16) | (RD8(ea + 4) << 8) | RD8(ea + 6);' % r)
                else: code.append('%s = (%s & 0xFFFF0000u) | (RD8(ea) << 8) | RD8(ea + 2);' % (r, r))
            return self.block(code)
        if op == 'scc':
            dst = self.operand(i.dst, 1, 'ea', code)
            code.append(dst.write('(%s) ? 0xFF : 0' % CCEXPR[i.cond]))
            return self.block(code)
        if op == 'dbcc':
            r = 'd%d' % i.dst[1]
            code.append('if (!(%s)) { uint32_t w = (%s - 1) & 0xFFFFu; %s = (%s & 0xFFFF0000u) | w; '
                        'if (w != 0xFFFFu) { %s } }' % (CCEXPR[i.cond], r, r, r, self.jump(i.target, a)))
            return code
        if op == 'bcc':
            return ['if (%s) { %s }' % (CCEXPR[i.cond], self.jump(i.target, a))]
        if op == 'bra': return [self.jump(i.target, a)]
        if op == 'bsr':
            return ['a7 -= 4; WR32(a7, 0x%xu); %s' % (nxt, self.jump(i.target, a, poll=False))]
        if op in ('jmp', 'jsr'):
            push = 'a7 -= 4; WR32(a7, 0x%xu); ' % nxt if op == 'jsr' else ''
            if i.target is not None: return [push + self.jump(i.target, a, poll=False)]
            return ['{ uint32_t t = %s; %spc = t; goto dispatch; }' % (self.addr(i.dst), push)]
        if op == 'rts': return ['pc = RD32(a7); a7 += 4; goto dispatch;']
        if op == 'rtr': return ['SETCCR(RD16(a7)); pc = RD32(a7 + 2); a7 += 6; goto dispatch;']
        if op == 'rte': return ['{ uint32_t sr = RD16(a7); pc = RD32(a7 + 2); a7 += 6; SETSR(sr); goto irq; }']
        if op == 'trap': return ['pc = 0x%x; vec = %d; goto exc;' % (nxt, 32 + i.data)]
        if op == 'trapv': return ['if (fv) { pc = 0x%x; vec = 7; goto exc; }' % nxt]
        if op == 'illegal': return ['pc = 0x%x; vec = 4; goto exc;' % a]
        if op == 'linea': return ['pc = 0x%x; vec = 10; goto exc;' % a]
        if op == 'linef': return ['pc = 0x%x; vec = 11; goto exc;' % a]
        if op == 'nop': return []
        if op == 'reset': return ['M68K_RESET_DEVICES(c);']
        if op == 'stop':
            return ['SETSR(0x%xu); pc = 0x%x; STORE; m68k_stop(c); LOAD; goto irq;' % (i.src[1], nxt)]
        raise NotImplementedError(op)

    def block(self, code): return ['{ ' + ' '.join(code) + ' }'] if code else []

    def add_flags(self, need, s, sv, dv, r, full):
        out = []
        if need & (X | C):
            cy = '((%s) >> %d) & 1' % (full, BITS[s]) if s < 4 else '(%s < %s)' % (r, dv)
            out.append(('fx = fc = %s;' if need & X else 'fc = %s;') % cy)
        if need & V: out.append('fv = (((%s ^ %s) & (%s ^ %s)) >> %d) & 1;' % (sv, r, dv, r, MSB[s]))
        out += self.nz(need, r, s)
        return out

    def sub_flags(self, need, s, sv, dv, r):
        out = []
        if need & (X | C): out.append(('fx = fc = (%s > %s);' if need & X else 'fc = (%s > %s);') % (sv, dv))
        if need & V: out.append('fv = (((%s ^ %s) & (%s ^ %s)) >> %d) & 1;' % (sv, dv, r, dv, MSB[s]))
        out += self.nz(need, r, s)
        return out

    def shift(self, i, need):
        op, s = i.op, i.size
        B, M = BITS[s], MASK[s]
        code = []
        dst = self.operand(i.dst, s, 'ea', code)
        code.append('uint32_t v = %s, r, cy = 0;' % dst.read())
        if i.src[0] == 'imm': code.append('uint32_t n = %d;' % i.src[1])
        else: code.append('uint32_t n = d%d & 63;' % i.src[1])
        setx = 'fx = cy; ' if need & X else ''
        if op in ('asl', 'lsl'):
            code.append('if (n == 0) { r = v; } else if (n < %d) { r = (v << n) & %s; cy = (v >> (%d - n)) & 1; %s}'
                        ' else { r = 0; cy = (n == %d) ? (v & 1) : 0; %s}' % (B, M, B, setx, B, setx))
            if op == 'asl' and need & V:
                code.append('if (n == 0) fv = 0; else if (n < %d) { uint32_t m = ((%s >> (%d - n - 1)) << (%d - n - 1)) & %s, t = v & m; '
                            'fv = !(t == 0 || t == m); } else fv = (v != 0);' % (B, M, B, B, M))
            elif need & V: code.append('fv = 0;')
        elif op == 'lsr':
            code.append('if (n == 0) { r = v; } else if (n < %d) { r = v >> n; cy = (v >> (n - 1)) & 1; %s}'
                        ' else { r = 0; cy = (n == %d) ? (v >> %d) & 1 : 0; %s}' % (B, setx, B, B - 1, setx))
            if need & V: code.append('fv = 0;')
        elif op == 'asr':
            code.append('int32_t sv = (int32_t)%s;' % sx('v', s))
            code.append('if (n == 0) { r = v; } else if (n < %d) { r = (uint32_t)(sv >> n) & %s; cy = (uint32_t)(sv >> (n - 1)) & 1; %s}'
                        ' else { r = sv < 0 ? %s : 0; cy = sv < 0; %s}' % (B, M, setx, M, setx))
            if need & V: code.append('fv = 0;')
        elif op in ('rol', 'ror'):
            code.append('uint32_t k = n & %d;' % (B - 1))
            if op == 'rol': code.append('r = k ? ((v << k) | (v >> (%d - k))) & %s : v; if (n) cy = r & 1;' % (B, M))
            else: code.append('r = k ? ((v >> k) | (v << (%d - k))) & %s : v; if (n) cy = (r >> %d) & 1;' % (B, M, B - 1))
            if need & V: code.append('fv = 0;')
        else:
            code.append('uint32_t k = n %% %d;' % (B + 1))
            code.append('uint64_t wv = ((uint64_t)fx << %d) | v, wm = (1ull << %d) - 1;' % (B, B + 1))
            if op == 'roxl': code.append('if (k) wv = ((wv << k) | (wv >> (%d - k))) & wm;' % (B + 1))
            else: code.append('if (k) wv = ((wv >> k) | (wv << (%d - k))) & wm;' % (B + 1))
            code.append('r = (uint32_t)wv & %s; cy = (uint32_t)(wv >> %d) & 1; fx = cy;' % (M, B))
            if need & V: code.append('fv = 0;')
        if need & C: code.append('fc = cy;')
        code += self.nz(need, 'r', s)
        code.append(dst.write('r'))
        return code


def main():
    args = sys.argv[1:]
    if len(args) < 3: sys.exit(__doc__)
    path, base, out = args[0], int(args[1], 16), args[2]
    entries, aliases, cb, mapf, vectors, idle = [], [], 12, None, False, []
    k = 3
    while k < len(args):
        x = args[k]
        if x == '--map': mapf = args[k + 1]; k += 2; continue
        if x == '--alias': aliases.append(tuple(int(v, 16) for v in args[k + 1].split(':'))); k += 2; continue
        if x == '--chunk': cb = int(args[k + 1]); k += 2; continue
        if x == '--vectors': vectors = True; k += 1; continue
        if x == '--idle': idle.append(int(args[k + 1], 16)); k += 2; continue
        entries.append(int(x, 16)); k += 1
    data = open(path, 'rb').read()
    img = Image(data, base, aliases)
    if mapf:
        m = json.load(open(mapf))
        entries += [int(a, 16) for a in m.get('entries', {})]
        for t in m.get('tables', {}).values(): entries += [int(a, 16) for a in t['targets']]
    if vectors:
        for v in range(2, 64):
            t = (img.word(base + 4 * v) or 0) << 16 | (img.word(base + 4 * v + 2) or 0)
            if t >= base + 0x100 and img.word(t) is not None: entries.append(t)   # 0 = unused vector
    entries = [e for e in entries if img.word(e) is not None]
    t = Translator(img, cb, idle)
    t.discover(entries)
    t.generate(out)
    print('%s: %d instructions, %d labels, %d C functions%s' % (
        out, len(t.ins), len(t.labels), len({t.chunk(a) for a in t.ins}),
        ', undecodable targets: ' + ' '.join('%06X' % a for a in sorted(t.missing)[:12]) if t.missing else ''))


if __name__ == '__main__':
    main()
