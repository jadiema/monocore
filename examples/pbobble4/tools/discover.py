#!/usr/bin/env python3
"""Translates the program, builds the PC bench and runs it; every untranslated address it reaches is
added to the entry points (entries.txt), then everything is done again, until a run goes through.

usage: python3 tools/discover.py [--no-harvest] FRAMES [SCRIPT]   PC bench (see pc/bench.c for the script)
       python3 tools/discover.py --sim FRAMES [SCRIPT]     MONOCORE ROM in the simulator (tools/run.js;
                                                           p1=HEX are bits of IN_P1, start = 400)
entries.txt only holds addresses (no data from the game): it is kept in the repository.
"""
import os, re, struct, subprocess, sys
here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here); top = os.path.dirname(os.path.dirname(root))
sys.path.insert(0, os.path.join(top, 'tools', 'recomp68k'))
import m68k
TR = os.path.join(root, 'build', 'tr'); os.makedirs(TR, exist_ok=True)
ENTRIES = os.path.join(root, 'entries.txt')
# code the program copies to RAM before running it (source:destination:length, hexadecimal)
# branches that close the game's wait loops (see neo_idle in neort.c)
IDLE = ['2B24',                   # main loop: bra $2AEC after the task scheduler
        '100920']                 # eye catcher (copied to RAM): waits for SYSTEM_INT1
ALIASES = ['D3EFE:100000:114A',      # copied by D3DCA (0x8A5 words), called at 100000
           'D3E94:10FC00:6A']        # copied by D3E80 up to its rts, run at a5+$7C00
rom = open(os.path.join(root, 'p1_be.bin'), 'rb').read()


def mem(a):
    for x in ALIASES:                    # code copied to RAM: read it from its source in the ROM
        src, dst, ln = (int(v, 16) for v in x.split(':'))
        if dst <= a < dst + ln: a = a - dst + src
    return struct.unpack('>H', rom[a:a + 2])[0] if 0 <= a < len(rom) - 1 and a % 2 == 0 else None


def harvest():
    """Code addresses stored as data in the ROM (pointer tables, scripts of the game): every aligned
    32-bit value that points into the ROM and decodes cleanly (m68kmap.Mapper.plausible). About half
    of the addresses reached at run time are found this way, before running anything; the false
    positives are data translated as code that never runs. Also the targets of lea/pea d16(pc): the
    game's tasks store their next step with "lea next(pc),a6 / move.l a6,(a4) / rts", a 16-bit
    displacement the scan of 32-bit values misses. Computed once (build/check/harvest2.txt)."""
    path = os.path.join(root, 'build', 'check', 'harvest2.txt')
    if not os.path.exists(path):
        sys.path.insert(0, here); import m68kmap
        mp = m68kmap.Mapper(rom)
        cand = {struct.unpack('>I', rom[a:a + 4])[0] for a in range(0, len(rom) - 3, 2)}
        for a in range(0, len(rom) - 4, 2):                    # lea d16(pc),An and pea d16(pc)
            w = struct.unpack('>H', rom[a:a + 2])[0]
            if (w & 0xF1FF) == 0x41FA or w == 0x487A: cand.add(a + 2 + struct.unpack('>h', rom[a + 2:a + 4])[0])
        found = sorted(v for v in cand if 0x200 <= v < 0x100000 and v % 2 == 0 and mp.plausible(v))
        open(path, 'w').write('\n'.join('%x' % v for v in found) + '\n')
    return open(path).read().split()


def entries():
    if not os.path.exists(ENTRIES): return []
    return [l.split()[0] for l in open(ENTRIES) if l.strip() and not l.startswith('#')]


# log2 of the address range of one C function of the translated program: a call or a return between
# two functions goes through the main loop of the program (all the registers stored and loaded), a
# call inside one is a goto
CHUNK = 12

def translate():
    """The translated program, build/tr/pb2_code.c."""
    subprocess.check_call([sys.executable, os.path.join(top, 'tools', 'recomp68k', 'recomp68k.py'),
                           os.path.join(root, 'p1_be.bin'), '0', os.path.join(TR, 'pb2_code.c'),
                           '--map', os.path.join(root, 'build', 'check', 'codemap.json'), '--vectors',
                           '--chunk', os.environ.get('PB_CHUNK', str(CHUNK))]
                          + sum((['--alias', x] for x in ALIASES), []) + sum((['--idle', x] for x in IDLE), []) + [x.split(':')[1] for x in ALIASES] + entries()
                          + ([] if '--no-harvest' in sys.argv else harvest()),
                          stdout=subprocess.DEVNULL)


def build():
    """The translated program and the PC bench."""
    translate()
    subprocess.check_call(['gcc', '-O1', '-w', '-include', os.path.join(root, 'pc', 'tick.h'),
                           '-I', root, '-I', os.path.join(top, 'tools', 'recomp68k'), '-o', os.path.join(TR, 'bench'),
                           os.path.join(root, 'pc', 'bench.c'), os.path.join(root, 'neort.c'), os.path.join(TR, 'pb2_code.c')])


def run_sim(frames, script):
    """Builds the ROM and runs it in the simulator; returns (output, unknown address match)."""
    subprocess.check_call([sys.executable, os.path.join(top, 'sdk', 'build.py'), '.'], cwd=root, stdout=subprocess.DEVNULL)
    out = os.path.join(root, 'build', 'sim'); os.makedirs(out, exist_ok=True)
    last = int(frames)
    r = subprocess.run(['node', os.path.join(top, 'tools', 'run.js'), os.path.join(root, 'build', 'pbobble4.mnc'), out,
                        ('%s %d:shot=last' % (script, last - 1)).strip()], capture_output=True, text=True)
    return r.stdout, re.search(r'UNKNOWN ([0-9A-F]{6}) \(frame (\d+)\)', r.stdout)


def main():
    args = [a for a in sys.argv[1:] if a not in ('--sim', '--no-harvest')]
    sim = '--sim' in sys.argv
    frames = args[0] if args else '600'
    script = args[1] if len(args) > 1 else ''
    for rnd in range(200):
        build()
        if sim:
            text, m = run_sim(frames, script)
            print((text.strip().splitlines() or [''])[0])
            if not m: return 0
        else:
            r = subprocess.run([os.path.join(TR, 'bench'), root, frames, script], cwd=TR, capture_output=True, text=True)
            print(r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr.strip())
            m = re.search(r'UNKNOWN ([0-9A-F]{6}) frame (\d+)', r.stdout)
            if r.returncode != 2 or not m: return r.returncode
        a = int(m.group(1), 16)
        if mem(a) is None or m68k.decode(mem, a) is None:
            print('-> %06X is not code in the ROM: to look at by hand' % a); return 2
        if '%06x' % a in entries():
            print('-> %06X is already an entry point: the build is stale or the code is not reached' % a); return 2
        with open(ENTRIES, 'a') as f: f.write('%06x  reached at frame %s\n' % (a, m.group(2)))
    return 1


if __name__ == '__main__':
    sys.exit(main())
