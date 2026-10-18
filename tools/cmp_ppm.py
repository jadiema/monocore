#!/usr/bin/env python3
"""Compare two PPM screenshots pixel by pixel (tools/run.js vs the RTL testbench).
usage: cmp_ppm.py a.ppm b.ppm [diff.png]   -> prints the number of differing pixels and their bounds"""
import sys
from PIL import Image, ImageChops

a, b = Image.open(sys.argv[1]).convert('RGB'), Image.open(sys.argv[2]).convert('RGB')
if a.size != b.size:
    print('tailles differentes : %s / %s' % (a.size, b.size))
    sys.exit(2)
d = ImageChops.difference(a, b)
mask = d.convert('L').point(lambda v: 255 if v else 0)
n = sum(1 for v in mask.tobytes() if v)
bbox = mask.getbbox()
print('%s %dx%d : %d pixels differents%s' % (sys.argv[1].split('/')[-1], a.width, a.height, n, (' dans %s' % (bbox,)) if bbox else ''))
if n and len(sys.argv) > 3:
    out = Image.new('RGB', (a.width * 3, a.height))
    out.paste(a, (0, 0)); out.paste(b, (a.width, 0)); out.paste(mask.convert('RGB'), (a.width * 2, 0))
    out.resize((out.width * 2, out.height * 2), Image.NEAREST).save(sys.argv[3])
sys.exit(1 if n else 0)
