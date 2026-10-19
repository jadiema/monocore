#!/usr/bin/env python3
"""Differential test of the 68000 translator against Musashi (MIT licence, fetched, never committed).

usage: python3 fuzz.py [count] [seed] [rounds]     (default 3000 tests, seed 1, 1 round)
Generates random instruction sequences (plus branch, loop and call patterns), translates them with
recomp68k.py, and runs them on Musashi and as translated code from the same state (harness.c).
Work files go to tools/recomp68k/test/build/. Needs gcc and git.
"""
import json, os, random, struct, subprocess, sys
here = os.path.dirname(os.path.abspath(__file__)); top = os.path.dirname(here)
sys.path.insert(0, top)
import m68k

B = os.path.join(here, 'build'); os.makedirs(B, exist_ok=True)
MUS = os.environ.get('MUSASHI', os.path.join(B, 'Musashi'))
SKIP = {'bcc', 'bra', 'bsr', 'dbcc', 'jmp', 'jsr', 'rts', 'rte', 'rtr', 'stop', 'reset', 'move_to_sr',
        'andi_sr', 'ori_sr', 'eori_sr', 'linea', 'linef'}  # line A/F: Musashi decodes FPU ops even as a 68000
SENT = bytes.fromhex('4EF900FFF000')              # jmp $FFF000.l


def musashi():
    if not os.path.exists(os.path.join(MUS, 'm68kcpu.c')):
        subprocess.check_call(['git', 'clone', '-q', '--depth', '1', 'https://github.com/kstenerud/Musashi.git', MUS])
    conf = os.path.join(MUS, 'm68kconf.h')
    s = open(conf).read()
    if 'test_hook' not in s:
        s = s.replace('#define M68K_INSTRUCTION_HOOK       M68K_OPT_OFF', '#define M68K_INSTRUCTION_HOOK       M68K_OPT_SPECIFY_HANDLER')
        s = s.replace('your_instruction_hook_function(pc)', 'test_hook(pc)')
        s = s.replace('#define M68K_INSTRUCTION_CALLBACK(pc)', 'void test_hook(unsigned int pc);\n#define M68K_INSTRUCTION_CALLBACK(pc)')
        open(conf, 'w').write(s)
    if not os.path.exists(os.path.join(MUS, 'm68kops.c')):
        subprocess.check_call(['gcc', '-o', 'm68kmake', 'm68kmake.c'], cwd=MUS)
        subprocess.check_call(['./m68kmake'], cwd=MUS)


def rword(rng):
    k = rng.random()
    if k < 0.25: return 0x0010                        # upper word of a RAM address (abs.l)
    if k < 0.45: return rng.randrange(0, 0x40) * 2    # small even displacement / index word
    if k < 0.55: return 0x8000 | rng.randrange(0x800) * 2
    return rng.randrange(0x10000)


def random_insn(rng, addr):
    while True:
        w = [rng.randrange(0x10000)] + [rword(rng) for _ in range(5)]
        mem = lambda a: w[(a - addr) // 2] if 0 <= a - addr < 12 else None
        i = m68k.decode(mem, addr)
        if i is None or i.op in SKIP: continue
        return b''.join(struct.pack('>H', x) for x in w[:i.len // 2]), i


def gen_test(rng, addr):
    """Returns (code, interrupt level asked during the test, other entry points as offsets)."""
    kind = rng.random()
    code = b''
    if kind < 0.06:                                   # move.w #sr,sr ; insn ; L: bra.s L with an interrupt
        mask = rng.randrange(6)                      # levels 1-6 only: level 7 is an edge-triggered NMI
        code += struct.pack('>HH', 0x46FC, 0x2000 | mask << 8 | rng.randrange(32))
        b, _ = random_insn(rng, addr + 4); code += b
        code += struct.pack('>H', 0x60FE)
        return code, rng.randrange(mask + 1, 7), []
    if kind < 0.10:                                   # andi/ori/eori/move to sr, may leave supervisor mode
        op = rng.choice([0x027C, 0x007C, 0x0A7C, 0x46FC])
        v = rng.randrange(0x10000) & 0xA71F
        if op == 0x027C: v |= 0x0700                  # keep the interrupt mask high (no interrupt asked)
        if op == 0x46FC: v |= 0x0700
        if op == 0x007C: v &= 0x271F
        if op == 0x0A7C: v &= 0x201F                  # flips S and flags only
        return code + struct.pack('>HH', op, v) + SENT, 0, []
    if kind < 0.13:                                   # pea E(pc) ; move.w #sr,-(sp) ; rte ; E:
        sr = 0x2700 | rng.randrange(32) | (0x2000 if rng.random() < 0.7 else 0)
        code += struct.pack('>HhHHH', 0x487A, 8, 0x3F3C, sr & 0xA71F, 0x4E73)
        return code + SENT, 0, [len(code)]           # E is reached only through RTE: an entry point
    if kind < 0.70:
        for _ in range(rng.randrange(1, 6)):
            b, _ = random_insn(rng, addr + len(code)); code += b
    elif kind < 0.82:                                 # flags, then Bcc.s over one moveq
        b, _ = random_insn(rng, addr); code += b
        code += struct.pack('>HH', 0x6000 | rng.randrange(16) << 8 | 2, 0x7E00 | rng.randrange(256))
        if code[len(b):len(b) + 2] in (b'\x61\x02',): code = code[:len(b)] + b'\x60\x02' + code[len(b) + 2:]
    elif kind < 0.92:                                 # moveq #n,d6 ; L: insn ; DBcc d6,L
        code += struct.pack('>H', 0x7C00 | rng.randrange(1, 5))
        loop = addr + len(code)
        b, _ = random_insn(rng, loop); code += b
        disp = loop - (addr + len(code) + 2)
        code += struct.pack('>Hh', 0x50C8 | rng.randrange(16) << 8 | 6, disp)
    else:                                             # bsr.s S ; bra.s E ; S: insn ; rts ; E:
        b, _ = random_insn(rng, addr + 4)
        code += struct.pack('>HH', 0x6102, 0x6000 | (len(b) + 2))
        code += b + struct.pack('>H', 0x4E75)
    return code + SENT, 0, []


def regs(rng):
    def dval():
        k = rng.random()
        if k < 0.2: return rng.randrange(0, 0x40) * 2
        if k < 0.3: return rng.choice([0, 1, 0xFFFFFFFF, 0x80000000, 0x7FFFFFFF, 0xFFFF, 0x8000, 0xFF, 0x80])
        return rng.getrandbits(32)
    d = [dval() for _ in range(8)]
    a = [0x101000 + rng.randrange(0x6000) * 2 for _ in range(7)] + [0x10F000]
    return d + a + [rng.randrange(32), 0x10E000]


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    rounds = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    musashi()
    total_fail = 0
    for rd in range(rounds):
        rng = random.Random(seed + rd)
        img = bytearray(0x1000); entries, rg, ipl, extra = [], [], [], []
        while len(entries) < count:
            a = len(img); c, lv, ex = gen_test(rng, a); img += c; extra += [a + x for x in ex]
            if len(img) % 4: img += b'\x4e\x71'      # keep tests aligned
            entries.append(a); rg.append(regs(rng)); ipl.append(lv)
        open(os.path.join(B, 'fuzz.bin'), 'wb').write(img)
        json.dump({'entries': {'%06x' % e: 'test' for e in entries + extra}}, open(os.path.join(B, 'fuzz.json'), 'w'))
        with open(os.path.join(B, 'tests.h'), 'w') as f:
            f.write('#define NTESTS %d\nstatic const uint32_t test_entry[NTESTS] = {%s};\n' % (count, ','.join('0x%x' % e for e in entries)))
            f.write('static const uint8_t test_ipl[NTESTS] = {%s};\n' % ','.join(map(str, ipl)))
            f.write('static const uint32_t test_regs[NTESTS][18] = {%s};\n' % ',\n'.join('{%s}' % ','.join('0x%xu' % v for v in r) for r in rg))
        subprocess.check_call([sys.executable, os.path.join(top, 'recomp68k.py'), os.path.join(B, 'fuzz.bin'), '0',
                               os.path.join(B, 'fuzz_code.c'), '--map', os.path.join(B, 'fuzz.json')])
        exe = os.path.join(B, 'harness')
        srcs = [os.path.join(here, 'harness.c'), os.path.join(B, 'fuzz_code.c')]
        objs = [os.path.join(MUS, x) for x in ('m68kcpu.c', 'm68kops.c', 'softfloat/softfloat.c')]
        subprocess.check_call(['gcc', '-O1', '-w', '-o', exe, '-I', here, '-I', top, '-I', MUS, '-I', B] + srcs + objs + ['-lm'])
        r = subprocess.run([exe], cwd=B)
        total_fail += r.returncode != 0
    sys.exit(1 if total_fail else 0)


if __name__ == '__main__':
    main()
