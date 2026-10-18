"""Small 6809 decoder for the static translator (only what a translator needs)."""
MEM_OPS = {0x0:'neg',0x3:'com',0x4:'lsr',0x6:'ror',0x7:'asr',0x8:'asl',0x9:'rol',0xa:'dec',
           0xc:'inc',0xd:'tst',0xe:'jmp',0xf:'clr'}
ACC_OPS = {0x0:'sub',0x1:'cmp',0x2:'sbc',0x4:'and',0x5:'bit',0x6:'ld',0x7:'st',0x8:'eor',
           0x9:'adc',0xa:'or',0xb:'add'}
BR = {0x0:'bra',0x1:'brn',0x2:'bhi',0x3:'bls',0x4:'bcc',0x5:'bcs',0x6:'bne',0x7:'beq',0x8:'bvc',
      0x9:'bvs',0xa:'bpl',0xb:'bmi',0xc:'bge',0xd:'blt',0xe:'bgt',0xf:'ble'}
REG8 = {0:'D',1:'X',2:'Y',3:'U',4:'S',5:'PC',8:'A',9:'B',0xa:'CC',0xb:'DP'}
IDXR = ['X','Y','U','S']

class Ins:
    def __init__(s, **k): s.__dict__.update(k)
    def __repr__(s): return '%04X %s %s' % (s.addr, s.op, s.__dict__.get('arg', ''))

def s8(v): return v - 256 if v & 0x80 else v
def s16(v): return v - 65536 if v & 0x8000 else v

def indexed(m, p):
    """returns (dict describing the effective address, new position)"""
    pb = m[p]; p += 1
    r = IDXR[(pb >> 5) & 3]
    if not pb & 0x80:
        off = pb & 0x1f
        return dict(kind='off', reg=r, off=off - 32 if off & 0x10 else off, ind=False), p
    ind = bool(pb & 0x10); t = pb & 0x0f
    if t == 0x0: return dict(kind='inc', reg=r, n=1, ind=False), p
    if t == 0x1: return dict(kind='inc', reg=r, n=2, ind=ind), p
    if t == 0x2: return dict(kind='dec', reg=r, n=1, ind=False), p
    if t == 0x3: return dict(kind='dec', reg=r, n=2, ind=ind), p
    if t == 0x4: return dict(kind='off', reg=r, off=0, ind=ind), p
    if t == 0x5: return dict(kind='acc', reg=r, acc='B', ind=ind), p
    if t == 0x6: return dict(kind='acc', reg=r, acc='A', ind=ind), p
    if t == 0x8: return dict(kind='off', reg=r, off=s8(m[p]), ind=ind), p + 1
    if t == 0x9: return dict(kind='off', reg=r, off=s16(m[p] << 8 | m[p+1]), ind=ind), p + 2
    if t == 0xb: return dict(kind='acc', reg=r, acc='D', ind=ind), p
    if t == 0xc: return dict(kind='pcrel', pc=(p + 1 + s8(m[p])) & 0xffff, ind=ind), p + 1
    if t == 0xd: return dict(kind='pcrel', pc=(p + 2 + s16(m[p] << 8 | m[p+1])) & 0xffff, ind=ind), p + 2
    if t == 0xf: return dict(kind='abs', a=m[p] << 8 | m[p+1], ind=True), p + 2
    raise ValueError('bad postbyte %02X' % pb)

def operand(m, p, mode, wide):
    if mode == 'imm':
        if wide: return dict(kind='imm', v=m[p] << 8 | m[p+1]), p + 2
        return dict(kind='imm', v=m[p]), p + 1
    if mode == 'dir': return dict(kind='dir', a=m[p]), p + 1
    if mode == 'ext': return dict(kind='ext', a=m[p] << 8 | m[p+1]), p + 2
    if mode == 'idx': return indexed(m, p)

def decode(m, a):
    p = a; op = m[p]; p += 1; page = 0
    if op in (0x10, 0x11): page = op; op = m[p]; p += 1
    hi, lo = op >> 4, op & 0xf
    I = lambda **k: Ins(addr=a, **k)
    if page == 0:
        if hi in (0x0, 0x6, 0x7) and lo in MEM_OPS:
            mode = {0: 'dir', 6: 'idx', 7: 'ext'}[hi]
            arg, p = operand(m, p, mode, False)
            return I(op=MEM_OPS[lo], reg='M', arg=arg, size=p - a)
        if hi in (0x4, 0x5) and lo in MEM_OPS and lo not in (0xe,):
            return I(op=MEM_OPS[lo], reg='A' if hi == 4 else 'B', size=p - a)
        if hi == 0x2:
            return I(op=BR[lo], target=(p + 1 + s8(m[p])) & 0xffff, size=p + 1 - a)
        if op == 0x16: return I(op='bra', target=(p + 2 + s16(m[p] << 8 | m[p+1])) & 0xffff, size=p + 2 - a)
        if op == 0x17: return I(op='bsr', target=(p + 2 + s16(m[p] << 8 | m[p+1])) & 0xffff, size=p + 2 - a)
        if op == 0x8d: return I(op='bsr', target=(p + 1 + s8(m[p])) & 0xffff, size=p + 1 - a)
        simple = {0x12: 'nop', 0x13: 'sync', 0x19: 'daa', 0x1d: 'sex', 0x39: 'rts', 0x3a: 'abx',
                  0x3b: 'rti', 0x3d: 'mul', 0x3f: 'swi'}
        if op in simple: return I(op=simple[op], size=p - a)
        if op in (0x1a, 0x1c, 0x3c): return I(op={0x1a: 'orcc', 0x1c: 'andcc', 0x3c: 'cwai'}[op], v=m[p], size=p + 1 - a)
        if op in (0x1e, 0x1f):
            return I(op='exg' if op == 0x1e else 'tfr', src=REG8[m[p] >> 4], dst=REG8[m[p] & 15], size=p + 1 - a)
        if 0x30 <= op <= 0x33:
            arg, p = indexed(m, p)
            return I(op='lea', reg='XYSU'[op - 0x30], arg=arg, size=p - a)
        if 0x34 <= op <= 0x37:
            return I(op=['pshs', 'puls', 'pshu', 'pulu'][op - 0x34], mask=m[p], size=p + 1 - a)
        if op >= 0x80:
            mode = ['imm', 'dir', 'idx', 'ext'][(op >> 4) & 3]
            reg = 'A' if op < 0xc0 else 'B'
            if lo in ACC_OPS and not (lo == 7 and mode == 'imm'):
                arg, p = operand(m, p, mode, False)
                return I(op=ACC_OPS[lo], reg=reg, arg=arg, size=p - a)
            wide = {(0, 3): ('sub', 'D'), (1, 3): ('add', 'D'), (0, 0xc): ('cmp', 'X'), (1, 0xc): ('ld', 'D'),
                    (1, 0xd): ('st', 'D'), (0, 0xe): ('ld', 'X'), (0, 0xf): ('st', 'X'), (1, 0xe): ('ld', 'U'),
                    (1, 0xf): ('st', 'U')}
            k = (0 if op < 0xc0 else 1, lo)
            if k in wide and not (wide[k][0] == 'st' and mode == 'imm'):
                arg, p = operand(m, p, mode, True)
                return I(op=wide[k][0], reg=wide[k][1], arg=arg, size=p - a)
            if op == 0x8d: pass
            if lo == 0xd and op < 0xc0 and mode != 'imm':
                arg, p = operand(m, p, mode, True)
                return I(op='jsr', arg=arg, size=p - a)
    elif page == 0x10:
        if hi == 0x2: return I(op=BR[lo], target=(p + 2 + s16(m[p] << 8 | m[p+1])) & 0xffff, size=p + 2 - a)
        if op == 0x3f: return I(op='swi2', size=p - a)
        if op >= 0x80:
            mode = ['imm', 'dir', 'idx', 'ext'][(op >> 4) & 3]
            t = {(0, 3): ('cmp', 'D'), (0, 0xc): ('cmp', 'Y'), (0, 0xe): ('ld', 'Y'), (0, 0xf): ('st', 'Y'),
                 (1, 0xe): ('ld', 'S'), (1, 0xf): ('st', 'S')}.get((0 if op < 0xc0 else 1, lo))
            if t:
                arg, p = operand(m, p, mode, True)
                return I(op=t[0], reg=t[1], arg=arg, size=p - a)
    elif page == 0x11:
        if op >= 0x80:
            mode = ['imm', 'dir', 'idx', 'ext'][(op >> 4) & 3]
            t = {(0, 3): ('cmp', 'U'), (0, 0xc): ('cmp', 'S')}.get((0 if op < 0xc0 else 1, lo))
            if t:
                arg, p = operand(m, p, mode, True)
                return I(op=t[0], reg=t[1], arg=arg, size=p - a)
    raise ValueError('undecodable opcode %02X%02X at %04X' % (page, op, a))
