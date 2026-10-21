#!/usr/bin/env python3
"""Translates the 68000 program to C for the ROM: build/tr/pb2_code.c (needed by sdk/build.py).

usage: python3 tools/translate.py
Needs p1_be.bin (tools/prepare.py) and the code map (tools/m68kmap.py); the entry points come from
entries.txt and from the code addresses stored in the ROM (computed once, about a minute).
"""
import os, sys
here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
for f, step in (('p1_be.bin', 'python3 tools/prepare.py pbobbl2n.zip'),
                (os.path.join('build', 'check', 'codemap.json'), 'python3 tools/m68kmap.py')):
    if not os.path.exists(os.path.join(root, f)): sys.exit('missing %s: run first  %s' % (f, step))
sys.path.insert(0, here)
import discover
discover.translate()
print('build/tr/pb2_code.c ready: now  python3 ../../sdk/build.py .')
