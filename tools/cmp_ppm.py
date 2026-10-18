#!/usr/bin/env python3
"""Compare two PPM screenshots pixel by pixel (tools/run.js vs the RTL testbench).
usage: cmp_ppm.py [--cpu-meter] a.ppm b.ppm [diff.png]   -> prints the number of differing pixels and their bounds
--cpu-meter: ignore the test ROM's CPU load meter (top right, first text row). It shows a measured
cycle count, which the simulator only approximates (fixed cost per instruction and per DMA)."""
import sys
from PIL import Image, ImageChops, ImageDraw

cpu_meter = '--cpu-meter' in sys.argv[1:]
args = [x for x in sys.argv[1:] if not x.startswith('--')]
a, b = Image.open(args[0]).convert('RGB'), Image.open(args[1]).convert('RGB')
if a.size != b.size:
    print('tailles differentes : %s / %s' % (a.size, b.size))
    sys.exit(2)
if cpu_meter:
    for im in (a, b):
        ImageDraw.Draw(im).rectangle((im.width - 40, 0, im.width - 1, 7), fill=(0, 0, 0))
d = ImageChops.difference(a, b)
mask = d.convert('L').point(lambda v: 255 if v else 0)
n = sum(1 for v in mask.tobytes() if v)
bbox = mask.getbbox()
print('%s %dx%d : %d pixels differents%s' % (args[0].split('/')[-1], a.width, a.height, n, (' dans %s' % (bbox,)) if bbox else ''))
if n and len(args) > 2:
    out = Image.new('RGB', (a.width * 3, a.height))
    out.paste(a, (0, 0)); out.paste(b, (a.width, 0)); out.paste(mask.convert('RGB'), (a.width * 2, 0))
    out.resize((out.width * 2, out.height * 2), Image.NEAREST).save(args[2])
sys.exit(1 if n else 0)
