#!/usr/bin/env python3
"""Static translator: 6809 machine code of L'Aigle d'Or -> C (no interpreter loop).

Every instruction becomes C statements on the 6809 registers; direct jumps and branches become
gotos, JSR pushes its return address on the 6809 stack (in mem[]) like the original, and RTS /
PULS PC / indirect jumps go through a switch on the target address. Calls into the MO5 ROM
(SWI, JSR $Cxxx) become calls to C functions of the runtime (mo5rt.c).
usage: recomp6809.py image.bin base out.c entry...
"""
import sys, m6809

def ea_code(arg, ins):
    """C statements computing the effective address in 'ea' (indexed, direct, extended)."""
    k = arg['kind']
    if k == 'dir': return 'ea = (uint16_t)(DP << 8 | 0x%02X);' % arg['a']
    if k == 'ext': return 'ea = 0x%04X;' % arg['a']
    if k == 'off': s = 'ea = (uint16_t)(%s + %d);' % (arg['reg'], arg['off'])
    elif k == 'acc':
        acc = {'A': '(int8_t)A', 'B': '(int8_t)B', 'D': 'D'}[arg['acc']]
        s = 'ea = (uint16_t)(%s + %s);' % (arg['reg'], acc)
    elif k == 'inc': s = 'ea = %s; %s += %d;' % (arg['reg'], arg['reg'], arg['n'])
    elif k == 'dec': s = '%s -= %d; ea = %s;' % (arg['reg'], arg['n'], arg['reg'])
    elif k == 'pcrel': s = 'ea = 0x%04X;' % arg['pc']
    elif k == 'abs': s = 'ea = 0x%04X;' % arg['a']
    if arg.get('ind'): s += ' ea = RD16(ea);'
    return s

R16 = {'D': 'D', 'X': 'X', 'Y': 'Y', 'U': 'U', 'S': 'S'}
def setreg(r, v):
    if r == 'D': return 'SETD(%s);' % v
    if r in 'XYUS': return '%s = (uint16_t)(%s);' % (r, v)
    if r == 'A': return 'A = (uint8_t)(%s);' % v
    if r == 'B': return 'B = (uint8_t)(%s);' % v
    if r == 'CC': return 'SETCC(%s);' % v
    if r == 'DP': return 'DP = (uint8_t)(%s);' % v
    if r == 'PC': return 'pc = (uint16_t)(%s); goto dispatch;' % v
def getreg(r, pcval):
    return {'D': 'D', 'CC': 'GETCC()', 'PC': '0x%04X' % pcval}.get(r, r)

COND = {'bra': '1', 'brn': '0', 'bhi': '!(fC | fZ)', 'bls': '(fC | fZ)', 'bcc': '!fC', 'bcs': 'fC',
        'bne': '!fZ', 'beq': 'fZ', 'bvc': '!fV', 'bvs': 'fV', 'bpl': '!fN', 'bmi': 'fN',
        'bge': '!(fN ^ fV)', 'blt': '(fN ^ fV)', 'bgt': '!(fZ | (fN ^ fV))', 'ble': '(fZ | (fN ^ fV))'}

class T:
    def __init__(s, mem, code, ext_calls):
        s.m, s.code, s.ext = mem, code, ext_calls

    def jump(s, t):
        return 'goto L_%04X;' % t if t in s.code else 'pc = 0x%04X; goto dispatch;' % t

    def ins(s, i):
        a, op, nxt = i.addr, i.op, i.addr + i.size
        out = []
        val8 = lambda: ('0x%02X' % i.arg['v']) if i.arg['kind'] == 'imm' else 'RD(ea)'
        val16 = lambda: ('0x%04X' % i.arg['v']) if i.arg['kind'] == 'imm' else 'RD16(ea)'
        if hasattr(i, 'arg') and i.arg['kind'] != 'imm' and op not in ('lea',):
            if op in ('jmp', 'jsr') and i.arg['kind'] == 'ext' and not i.arg.get('ind'):
                pass
            else:
                out.append(ea_code(i.arg, i))
        if op == 'lea':
            out.append(ea_code(i.arg, i))
            out.append('%s = ea;' % i.reg)
            if i.reg in 'XY': out.append('fZ = ea == 0;')
        elif op in ('ld', 'st', 'add', 'sub', 'cmp', 'and', 'or', 'eor', 'bit', 'adc', 'sbc') and i.reg in 'AB':
            r = i.reg
            if op == 'ld': out.append('%s = %s; NZ8(%s); fV = 0;' % (r, val8(), r))
            elif op == 'st': out.append('WR(ea, %s); NZ8(%s); fV = 0;' % (r, r))
            elif op == 'add': out.append('%s = add8(%s, %s, 0);' % (r, r, val8()))
            elif op == 'adc': out.append('%s = add8(%s, %s, fC);' % (r, r, val8()))
            elif op == 'sub': out.append('%s = sub8(%s, %s, 0);' % (r, r, val8()))
            elif op == 'sbc': out.append('%s = sub8(%s, %s, fC);' % (r, r, val8()))
            elif op == 'cmp': out.append('sub8(%s, %s, 0);' % (r, val8()))
            elif op == 'and': out.append('%s &= %s; NZ8(%s); fV = 0;' % (r, val8(), r))
            elif op == 'or': out.append('%s |= %s; NZ8(%s); fV = 0;' % (r, val8(), r))
            elif op == 'eor': out.append('%s ^= %s; NZ8(%s); fV = 0;' % (r, val8(), r))
            elif op == 'bit': out.append('{ uint8_t t = %s & %s; NZ8(t); fV = 0; }' % (r, val8()))
        elif op in ('ld', 'st', 'add', 'sub', 'cmp'):
            r = i.reg
            if op == 'ld': out.append('%s NZ16(%s); fV = 0;' % (setreg(r, val16()), R16[r]))
            elif op == 'st': out.append('WR16(ea, %s); NZ16(%s); fV = 0;' % (R16[r], R16[r]))
            elif op == 'add': out.append('SETD(add16(D, %s));' % val16())
            elif op == 'sub': out.append('SETD(sub16(D, %s));' % val16())
            elif op == 'cmp': out.append('sub16(%s, %s);' % (R16[r], val16()))
        elif op in m6809.MEM_OPS.values() and op != 'jmp':
            if i.reg == 'M': src, dst = 'RD(ea)', 'WR(ea, t);'
            else: src, dst = i.reg, '%s = t;' % i.reg
            body = {'neg': 't = sub8(0, t, 0);', 'com': 't = ~t; NZ8(t); fV = 0; fC = 1;',
                    'lsr': 'fC = t & 1; t >>= 1; NZ8(t);',
                    'ror': '{ uint8_t c = fC; fC = t & 1; t = (uint8_t)(t >> 1 | c << 7); NZ8(t); }',
                    'asr': 'fC = t & 1; t = (uint8_t)((int8_t)t >> 1); NZ8(t);',
                    'asl': 'fC = t >> 7; fV = (t ^ t << 1) >> 7 & 1; t <<= 1; NZ8(t);',
                    'rol': '{ uint8_t c = fC; fC = t >> 7; fV = (t ^ t << 1) >> 7 & 1; t = (uint8_t)(t << 1 | c); NZ8(t); }',
                    'dec': 'fV = t == 0x80; t--; NZ8(t);', 'inc': 'fV = t == 0x7F; t++; NZ8(t);',
                    'tst': 'NZ8(t); fV = 0;', 'clr': 't = 0; fN = 0; fZ = 1; fV = 0; fC = 0;'}[op]
            if op in ('tst',): out.append('{ uint8_t t = %s; %s }' % (src, body))
            elif op == 'clr': out.append('{ uint8_t t; %s %s }' % (body, dst))
            else: out.append('{ uint8_t t = %s; %s %s }' % (src, body, dst))
        elif op == 'jmp':
            if i.arg['kind'] == 'ext' and not i.arg.get('ind'): out.append(s.jump(i.arg['a']))
            else: out.append('pc = ea; goto dispatch;')
        elif op in ('jsr', 'bsr'):
            if op == 'bsr': t = i.target; direct = True
            else:
                direct = i.arg['kind'] == 'ext' and not i.arg.get('ind')
                t = i.arg['a'] if direct else None
            if direct and t in s.ext:
                out.append('%s(); ' % s.ext[t])
            else:
                out.append('PUSH16(S, 0x%04X);' % nxt)
                out.append(s.jump(t) if direct else 'pc = ea; goto dispatch;')
        elif op in COND:
            c = COND[op]
            if c == '1': out.append(s.jump(i.target))
            elif c != '0': out.append('if (%s) %s' % (c, s.jump(i.target)))
        elif op == 'rts': out.append('pc = PULL16(S); goto dispatch;')
        elif op == 'rti': out.append('pc = 0; goto dispatch; /* rti */')
        elif op == 'nop': pass
        elif op == 'mul': out.append('SETD((uint16_t)(A * B)); fZ = D == 0; fC = B >> 7;')
        elif op == 'abx': out.append('X = (uint16_t)(X + B);')
        elif op == 'sex': out.append('A = (B & 0x80) ? 0xFF : 0; NZ16(D);')
        elif op == 'orcc': out.append('SETCC(GETCC() | 0x%02X);' % i.v)
        elif op == 'andcc': out.append('SETCC(GETCC() & 0x%02X);' % i.v)
        elif op == 'tfr': out.append(setreg(i.dst, getreg(i.src, nxt)))
        elif op == 'exg':
            out.append('{ uint16_t t1 = %s, t2 = %s; %s %s }' % (getreg(i.src, nxt), getreg(i.dst, nxt),
                       setreg(i.src, 't2'), setreg(i.dst, 't1')))
        elif op in ('pshs', 'pshu', 'puls', 'pulu'):
            sp = 'S' if op[-1] == 's' else 'U'; other = 'U' if sp == 'S' else 'S'
            order = [(0x80, 'PC', 16), (0x40, other, 16), (0x20, 'Y', 16), (0x10, 'X', 16),
                     (0x08, 'DP', 8), (0x04, 'B', 8), (0x02, 'A', 8), (0x01, 'CC', 8)]
            if op.startswith('psh'):
                for bit, r, w in order:
                    if i.mask & bit:
                        v = getreg(r, nxt)
                        out.append(('PUSH16(%s, %s);' if w == 16 else 'PUSH8(%s, %s);') % (sp, v))
            else:
                tail = ''
                for bit, r, w in reversed(order):
                    if i.mask & bit:
                        v = ('PULL16(%s)' if w == 16 else 'PULL8(%s)') % sp
                        if r == 'PC': tail = 'pc = %s; goto dispatch;' % v
                        else: out.append(setreg(r, v))
                if tail: out.append(tail)
        elif op == 'swi':
            out.append('mo5_swi(0x%02X);' % s.m[nxt]); nxt += 1
        else:
            raise ValueError('not translated: %r' % i)
        return out, nxt

# Self-modifying code: before drawing, $5855-$58F6 rewrites these bytes to choose the drawing
# mode (RO / RB flags). Each site lists every byte string the game can write there.
SMC = {}
for a in (0x53D9, 0x5418, 0x5478, 0x54A7, 0x5528, 0x556B):        # nop nop / orb ,y / eorb ,y
    SMC[a] = [b'\x12\x12', b'\xea\xa4', b'\xe8\xa4']
for a in (0x538A, 0x53C1, 0x5403, 0x543F, 0x5466, 0x5498, 0x54C9, 0x5504, 0x5549):   # beq / brn
    SMC[a] = ['op', 0x27, 0x21]
for a, targets in ((0x56EE, (0x5436, 0x54BE)), (0x56FD, (0x548F, 0x553E)),
                   (0x5759, (0x5460, 0x54F9)), (0x5762, (0x548F, 0x553E))):  # jsr to a patched address
    SMC[a] = [bytes([0xBD, t >> 8, t & 255]) for t in targets]

def main():
    img, base, outp = sys.argv[1], int(sys.argv[2], 16), sys.argv[3]
    entries = [int(x, 16) for x in sys.argv[4:]]
    raw = open(img, 'rb').read()
    mem = bytearray(0x10000); mem[base:base + len(raw)] = raw
    ext = {0xCE2C: 'mo5_print_string', 0xD83E: 'mo5_print_int'}
    # recursive descent from the entry points and the jump tables
    w = lambda a: mem[a] << 8 | mem[a + 1]
    todo = list(entries) + [w(0x5200 + 2 * k) for k in range(20)] + \
           [w(0x4310 + 2 * k) for k in range(59) if w(0x4310 + 2 * k)] + [0x6AFF, 0x6BCA, 0x6B1D, 0x6BF4, 0x5436, 0x54BE, 0x548F, 0x553E, 0x5460, 0x54F9]
    code = {}
    while todo:
        a = todo.pop()
        while a not in code and 0x2100 <= a < 0xA000:
            i = m6809.decode(mem, a); code[a] = i
            nxt = a + i.size + (1 if i.op == 'swi' else 0)
            t = getattr(i, 'target', None)
            if i.op in ('jmp', 'jsr') and i.arg['kind'] == 'ext' and not i.arg.get('ind'): t = i.arg['a']
            if t is not None and t not in ext: todo.append(t)
            if i.op in ('bra', 'jmp', 'rts', 'rti') or (i.op == 'puls' and i.mask & 0x80): break
            a = nxt
    tr = T(mem, code, ext)
    o = ['// Generated by recomp6809.py from the original 6809 code of L\'Aigle d\'Or (MO5, 1985).',
         '// Do not edit: change the translator or the runtime instead.', '#include "cpu6809.h"', '',
         '#pragma clang diagnostic ignored "-Wunused-label"',
         'void cpu_exec(uint16_t pc) {', '  uint16_t ea; (void)ea;', '  PUSH16(S, 0xFFFF);', 'dispatch:',
         '  switch (pc) {', '  case 0xFFFF: return;']
    inner = {k for a0, vs in SMC.items() if a0 in code for k in range(a0 + 1, a0 + (code[a0].size if vs[0] == 'op' else len(vs[0])))}
    for a in sorted(code):
        if a not in inner: o.append('  case 0x%04X: goto L_%04X;' % (a, a))
    o += ['  default: cpu_bad_jump(pc); return;', '  }']
    skip = set()
    for a in sorted(code):
        if a in skip: continue
        i = code[a]
        if a in SMC:
            o.append('L_%04X: /* self-modifying site */' % a)
            vs = SMC[a]
            if vs[0] == 'op':
                n = i.size; variants = [bytes([v]) + bytes(mem[a + 1:a + n]) for v in vs[1:]]
            else:
                variants = vs; n = len(vs[0])
            for k in range(a + 1, a + n): skip.add(k)
            first = True
            for v in variants:
                tmp = bytearray(mem); tmp[a:a + n] = v
                t2 = T(tmp, code, ext); p = a; body = []
                while p < a + n:
                    ii = m6809.decode(tmp, p); ls, p = t2.ins(ii); body += ls
                test = ' && '.join('mem[0x%04X] == 0x%02X' % (a + k, v[k]) for k in range(n))
                o.append('  %sif (%s) {' % ('' if first else 'else ', test)); first = False
                o += ['    ' + l for l in body]
                o.append('    goto L_%04X;' % (a + n) if (a + n) in code else '    pc = 0x%04X; goto dispatch;' % (a + n))
                o.append('  }')
            o.append('  else { cpu_bad_jump(0x%04X); return; }' % a)
            continue
        lines, nxt = tr.ins(i)
        o.append('L_%04X: /* %s */' % (a, repr(i).split(' ', 1)[1]))
        o += ['  ' + l for l in lines]
        ends = i.op in ('bra', 'jmp', 'rts', 'rti') or (i.op == 'puls' and i.mask & 0x80) or \
            (i.op in ('tfr', 'exg') and 'PC' in (i.dst, i.src))
        if not ends and nxt not in code:
            o.append('  pc = 0x%04X; goto dispatch;' % nxt)
    o.append('}')
    open(outp, 'w').write('\n'.join(o) + '\n')
    print('%d instructions translated -> %s' % (len(code), outp))

main()
