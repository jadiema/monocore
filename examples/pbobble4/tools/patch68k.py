#!/usr/bin/env python3
"""Applies the 68000 patches of patches/*.s to the program (big-endian image), before translation.

Each patch is a section named after its address (.p_074F1E, .p_0F0000 ...). The sources are
assembled with GNU as (m68k-linux-gnu-as, package binutils-m68k-linux-gnu), linked with a script
that puts every section at its address (so labels and absolute addresses between patches work),
and the bytes are written into the image. A line "| expect ADDR LEN crc32 0xCRC" in a source checks
the original bytes of a site first (the patches only fit this version of the game); the code of the
port goes into the free area of the program (0x0E8000-0x0FFFFF, filled with 0xE5), checked as well.

usage (from prepare.py): patch68k.apply(image) -> patched image, or
       python3 tools/patch68k.py p1_be.bin        (checks and lists the patches)
"""
import glob, os, re, subprocess, sys, tempfile, zlib
here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
FREE = (0x0E8000, 0x100000)
AS, LD, OBJCOPY = 'm68k-linux-gnu-as', 'm68k-linux-gnu-ld', 'm68k-linux-gnu-objcopy'


def sources():                  # patches/*.s, and private/*.s when there (native.c)
    return sorted(glob.glob(os.path.join(root, 'patches', '*.s'))) + sorted(glob.glob(os.path.join(root, 'private', '*.s')))


def apply(image, verbose=False):
    img = bytearray(image)
    srcs = sources()
    if not srcs: return bytes(img)
    text = ''.join(open(s).read() + '\n' for s in srcs)
    for a, n, crc in re.findall(r'\|\s*expect\s+([0-9A-Fa-f]+)\s+(\d+)\s+crc32\s+0x([0-9A-Fa-f]+)', text):
        a, n = int(a, 16), int(n)
        if zlib.crc32(bytes(img[a:a + n])) != int(crc, 16):
            sys.exit('patch68k: the bytes at %06X are not those the patches expect (another version of the game?)' % a)
    secs = sorted(set(re.findall(r'\.section\s+\.p_([0-9A-Fa-f]{6})', text)), key=lambda s: int(s, 16))
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, 'all.s'); open(src, 'w').write(text)
        obj, elf, srec = os.path.join(d, 'all.o'), os.path.join(d, 'all.elf'), os.path.join(d, 'all.srec')
        lds = os.path.join(d, 'link.ld')
        open(lds, 'w').write('SECTIONS {\n' + ''.join('  .p_%s 0x%s : { *(.p_%s) }\n' % (s, s, s) for s in secs) +
                             '  /DISCARD/ : { *(*) }\n}\n')
        subprocess.check_call([AS, '-m68000', '-o', obj, src])
        subprocess.check_call([LD, '-T', lds, '-o', elf, obj])
        subprocess.check_call([OBJCOPY, '-O', 'srec', elf, srec])
        written = 0
        for line in open(srec):
            line = line.strip()
            if line[:2] not in ('S1', 'S2', 'S3'): continue
            alen = {'S1': 2, 'S2': 3, 'S3': 4}[line[:2]]
            raw = bytes.fromhex(line[2:])
            addr = int.from_bytes(raw[1:1 + alen], 'big'); data = raw[1 + alen:-1]
            if FREE[0] <= addr < FREE[1] and any(b != 0xE5 for b in img[addr:addr + len(data)]):
                sys.exit('patch68k: %06X is not free (two patches overlap?)' % addr)
            img[addr:addr + len(data)] = data; written += len(data)
    if verbose: print('patch68k: %d sections, %d bytes written: %s' % (len(secs), written, ' '.join(secs)))
    return bytes(img)


if __name__ == '__main__':
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, 'p1_be.bin')
    apply(open(path, 'rb').read(), verbose=True)
