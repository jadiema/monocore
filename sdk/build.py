#!/usr/bin/env python3
"""Builds a MONOCORE-1 ROM from the project.txt file of the current folder.

usage: python3 path/to/sdk/build.py [folder]

project.txt (one setting per line, # starts a comment):
    name    = stars                 output file: build/stars.mnc
    title   = ATTRAPE-ETOILES       title stored in the ROM header
    sources = main.c other.c        C files of the program
    cflags  = -I../lib -Wno-unused  extra compiler options for these files (optional)
    sprites = sheet.png             sprite sheets (16, 32, 64 wide) -> sheet_chr, sheet_pal
    maps    = level.png             maps in 16 x 16 tiles           -> level_chr, level_pal, level_map,
                                                                       level_width, level_height
    sounds  = boom.wav              sounds (WAV), for play_sample() -> boom_data, boom_count, boom_rate
    maps8   = title.png             256-colour maps (8 bpp, 16 x 16 tiles)
    data    = level1.dat music.bin  raw files, embedded as is   -> level1_file, level1_size
    library = mc                    "easy" (default) or "mc", the low-level SDK (mc.h, libc)

The SDK font is always included.
"""
import os, struct, subprocess, sys, wave, shutil

SDK = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(SDK, 'tools')
os.chdir(sys.argv[1] if len(sys.argv) > 1 else '.')

def fail(msg):
    print('ERROR: ' + msg)
    sys.exit(1)

if not os.path.exists('project.txt'):
    fail('no project.txt file in this folder (' + os.getcwd() + ')')
conf = {}
for ln in open('project.txt', encoding='utf-8'):
    ln = ln.split('#', 1)[0].strip()
    if '=' in ln:
        k, v = ln.split('=', 1)
        conf[k.strip()] = v.strip()
name = conf.get('name', 'game')
title = conf.get('title', name.upper())
sources = conf.get('sources', 'main.c').split()
for s in sources:
    if not os.path.exists(s): fail('source file %s not found' % s)

def tool(*names):
    for n in names:
        p = shutil.which(n)
        if p: return p
    fail('tool not found: %s (see chapter 2 of the manual to install LLVM)' % names[0])
CC = tool('clang', 'clang-18', 'clang-17', 'clang-16', 'clang-15', 'clang-14')
LD = tool('ld.lld', 'ld.lld-18', 'ld.lld-17', 'ld.lld-16', 'ld.lld-15', 'ld.lld-14')
OBJCOPY = tool('llvm-objcopy', 'llvm-objcopy-18', 'llvm-objcopy-17', 'llvm-objcopy-16', 'llvm-objcopy-15', 'llvm-objcopy-14')
ARCH = ['--target=riscv32', '-march=rv32im', '-mabi=ilp32', '-mno-relax']
LIBRARY = conf.get('library', 'easy')
if LIBRARY not in ('easy', 'mc'): fail('library = easy ou mc')
CFLAGS = ARCH + ['-O2', '-ffreestanding', '-fno-builtin', '-nostdlib', '-Wall', '-I' + SDK]
if LIBRARY == 'mc': CFLAGS += ['-I' + os.path.join(SDK, 'libc')]
PCFLAGS = CFLAGS + conf.get('cflags', '').split()   # the C files of the program get the extra options

os.makedirs('build', exist_ok=True)
def run(cmd):
    r = subprocess.run(cmd)
    if r.returncode: fail('command failed: ' + ' '.join(os.path.basename(c) if i == 0 else c for i, c in enumerate(cmd)))

def ident(path):
    b = os.path.splitext(os.path.basename(path))[0]
    return ''.join(c if c.isalnum() else '_' for c in b)

# ---------------------------------------------------------------- data
asm = ['    .section .rodata.data, "a"', '    .balign 4']
def blob(label, path):
    asm.append('    .globl %s' % label)
    asm.append('    .balign 4')
    asm.append('%s: .incbin "%s"' % (label, os.path.abspath(path).replace('\\', '/')))
def word(label, value):
    asm.append('    .globl %s' % label)
    asm.append('    .balign 4')
    asm.append('%s: .word %d' % (label, value))

run([sys.executable, os.path.join(TOOLS, 'png2chr.py'), os.path.join(SDK, 'font.png'), 'build/font'])
blob('font_chr', 'build/font.chr'); blob('font_pal', 'build/font.pal')
for f in conf.get('sprites', '').split():
    if not os.path.exists(f): fail('image %s not found' % f)
    n = ident(f)
    run([sys.executable, os.path.join(TOOLS, 'png2chr.py'), f, 'build/' + n])
    blob(n + '_chr', 'build/%s.chr' % n); blob(n + '_pal', 'build/%s.pal' % n)
for key, bpp in (('maps', '4'), ('maps8', '8')):
    for f in conf.get(key, '').split():
        if not os.path.exists(f): fail('image %s not found' % f)
        n = ident(f)
        run([sys.executable, os.path.join(TOOLS, 'png2chr.py'), f, 'build/' + n, '--tile', '16', '--map', '--bpp', bpp])
        blob(n + '_chr', 'build/%s.chr' % n); blob(n + '_pal', 'build/%s.pal' % n); blob(n + '_map', 'build/%s.map' % n)
        from PIL import Image
        w, h = Image.open(f).size
        word(n + '_width', w // 16); word(n + '_height', h // 16)
for f in conf.get('sounds', '').split():
    if not os.path.exists(f): fail('sound %s not found' % f)
    n = ident(f)
    subprocess.run([sys.executable, os.path.join(TOOLS, 'wav2pcm.py'), f, 'build/%s.raw' % n, 's8'], stdout=subprocess.DEVNULL)
    blob(n + '_data', 'build/%s.raw' % n)
    word(n + '_count', os.path.getsize('build/%s.raw' % n)); word(n + '_rate', wave.open(f).getframerate())
for f in conf.get('data', '').split():
    if not os.path.exists(f): fail('file %s not found' % f)
    n = ident(f)
    blob(n + '_file', f)
    word(n + '_size', os.path.getsize(f))
open('build/data.S', 'w').write('\n'.join(asm) + '\n')

# ---------------------------------------------------------------- compilation
objs = []
def cc(src, obj, flags):
    run([CC] + flags + ['-c', src, '-o', obj]); objs.append(obj)
cc(os.path.join(SDK, 'crt0.S'), 'build/crt0.o', ARCH)
cc(os.path.join(SDK, 'mini.c'), 'build/mini.o', CFLAGS)
if LIBRARY == 'easy':
    cc(os.path.join(SDK, 'easy.c'), 'build/easy.o', CFLAGS)
else:
    cc(os.path.join(SDK, 'mc.c'), 'build/mc.o', CFLAGS)
    cc(os.path.join(SDK, 'libc.c'), 'build/libc.o', CFLAGS)
cc('build/data.S', 'build/data.o', ARCH)
for s in sources:
    cc(s, 'build/%s.o' % ident(s), PCFLAGS)
run([LD, '-T', os.path.join(SDK, 'link.ld')] + objs + ['-o', 'build/%s.elf' % name])
run([OBJCOPY, '-O', 'binary', 'build/%s.elf' % name, 'build/%s.bin' % name])
run([sys.executable, os.path.join(SDK, 'mkrom.py'), 'build/%s.bin' % name, 'build/%s.mnc' % name, title, conf.get('origin', 'MONOCORE-1'),
     conf.get('rotation', '0'), conf.get('dip', '0')])   # rotation 0-3 (x 90 degrees), DIP switches by default
print('ROM ready: ' + os.path.join(os.getcwd(), 'build', name + '.mnc'))
