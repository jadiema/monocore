#!/usr/bin/env python3
"""Builds the atelier ROM: the launcher (examples/atelier) and complete programs in one .mnc.

usage: python3 tools/mkatelier.py          -> examples/atelier/build/monoatelier.mnc

Each program is built as usual (sdk/build.py), then linked again at its own ROM address (sdk/link.ld
with another origin): they all use the same WRAM, as only one runs at a time (sdk/atelier.h). The
table of the programs goes at ROM address 0x1F0000. Being one ROM, they share one NVRAM (nvslots).
"""
import os, struct, subprocess, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
SDK = os.path.join(ROOT, 'sdk')
OUT = os.path.join(ROOT, 'examples/atelier/build')
TABLE = 0x1F0000
# program folder, ROM address (the launcher first, at 0)
PROGRAMS = [('examples/atelier', 0x000000), ('examples/monopixel', 0x200000), ('examples/monosfx', 0x280000),
            ('examples/plateformes', 0x300000), ('examples/monotracker', 0x480000), ('examples/monobasic', 0x600000)]


def tool(*names):
    for n in names:
        for d in os.environ.get('PATH', '').split(os.pathsep):
            if os.path.exists(os.path.join(d, n)):
                return n
    sys.exit('missing: ' + names[0])


LD = tool('ld.lld', 'ld.lld-18', 'ld.lld-17', 'ld.lld-16', 'ld.lld-15', 'ld.lld-14')
OBJCOPY = tool('llvm-objcopy', 'llvm-objcopy-18', 'llvm-objcopy-17', 'llvm-objcopy-16', 'llvm-objcopy-15', 'llvm-objcopy-14')
os.makedirs(OUT, exist_ok=True)
rom = bytearray()
entries = []
for i, (folder, base) in enumerate(PROGRAMS):
    d = os.path.join(ROOT, folder)
    subprocess.run([sys.executable, os.path.join(SDK, 'build.py'), d], cwd=d, check=True, stdout=subprocess.DEVNULL)
    conf = dict(l.split('=', 1) for l in open(os.path.join(d, 'project.txt')) if '=' in l and not l.lstrip().startswith('#'))
    conf = {k.strip(): v.strip() for k, v in conf.items()}
    name, title = conf['name'], conf.get('title', conf['name']).upper()
    objs = open(os.path.join(d, 'build/link.txt')).read().split()
    ld = open(os.path.join(SDK, 'link.ld')).read()
    ld = ld.replace('ORIGIN = 0x00000000, LENGTH = 32M', 'ORIGIN = 0x%08X, LENGTH = 0x%X' % (base, 0x2000000 - base))
    ld = ld.replace('.text 0x100 :', '.text 0x%X :' % (base + 0x100))
    script = os.path.join(OUT, name + '.ld')
    open(script, 'w').write(ld)
    elf, binf = os.path.join(OUT, name + '.elf'), os.path.join(OUT, name + '.bin')
    subprocess.run([LD, '-T', script] + objs + ['-o', elf], check=True)
    subprocess.run([OBJCOPY, '-O', 'binary', elf, binf], check=True)
    b = open(binf, 'rb').read()
    if base and b[:4] != b'MNC1':
        sys.exit('%s: no header at the start' % name)
    nxt = PROGRAMS[i + 1][1] if i + 1 < len(PROGRAMS) else 0x2000000
    limit = (TABLE if base == 0 else nxt) - base
    if len(b) > limit:
        sys.exit('%s: %d bytes, more than its %d' % (name, len(b), limit))
    rom += bytes(base - len(rom)) + b
    entries.append((base, title))
    print('%-12s 0x%06X  %8d octets' % (title, base, len(b)))
# the table, then the header of the whole ROM (mkrom.py: size, title)
table = b'MATL' + struct.pack('<I', len(entries))
for base, title in entries:
    table += struct.pack('<I', base) + title.encode('ascii', 'replace')[:16].ljust(16, b'\0') + bytes(4)
rom[TABLE:TABLE + len(table)] = table
raw = os.path.join(OUT, 'monoatelier.bin')
open(raw, 'wb').write(rom)
subprocess.run([sys.executable, os.path.join(SDK, 'mkrom.py'), raw, os.path.join(OUT, 'monoatelier.mnc'), 'MONOATELIER', 'MONOCORE-1'], check=True)
