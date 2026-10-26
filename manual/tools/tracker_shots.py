#!/usr/bin/env python3
"""Screen shots of MONOTRACKER for its manual, taken in the simulator (tools/run.js).

usage: python3 manual/tools/tracker_shots.py   -> manual/images/tracker_*.png (twice the size, as the other shots)
"""
import os, subprocess, tempfile
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ROM = os.path.join(ROOT, 'examples/monotracker/build/monotracker.mnc')
IMG = os.path.join(ROOT, 'manual/images')
KEY = dict(F1=0x05, F2=0x06, F3=0x04, F4=0x0C, ENTER=0x5A, TAB=0x0D, F12=0x07, CTRL=0x14, S=0x1B, E=0x24,
           SPACE=0x29, Z=0x1A, C=0x21, B=0x32, COMMA=0x41, X=0x22, V=0x2A, DOWN=0x172)

class Script:
    def __init__(self): self.ev, self.f = [], 10
    def wait(self, n): self.f += n; return self
    def tap(self, *keys, hold=None):
        for k in keys:
            if hold: self.ev.append('%d:kd=%x' % (self.f, KEY[hold])); self.f += 1
            self.ev.append('%d:kd=%x' % (self.f, KEY[k])); self.f += 2
            self.ev.append('%d:ku=%x' % (self.f, KEY[k])); self.f += 1
            if hold: self.ev.append('%d:ku=%x' % (self.f, KEY[hold])); self.f += 1
            self.f += 2
        return self
    def shot(self, name): self.ev.append('%d:shot=%s' % (self.f, name)); self.f += 1; return self

def run(script, nvram=None):
    out = tempfile.mkdtemp()
    env = dict(os.environ)
    if nvram: env['NVRAM'] = nvram
    subprocess.run(['node', os.path.join(ROOT, 'tools/run.js'), ROM, out, ' '.join(script.ev)], check=True,
                   env=env, stdout=subprocess.DEVNULL)
    for f in os.listdir(out):
        if f.endswith('.ppm'):
            im = Image.open(os.path.join(out, f))
            im.resize((im.width * 2, im.height * 2), Image.NEAREST).save(os.path.join(IMG, 'tracker_%s.png' % f[:-4]))
            print('tracker_%s.png' % f[:-4])

nv = os.path.join(tempfile.mkdtemp(), 'monotracker.sav')
# the demo playing: patterns (PSG then PCM tracks), sounds, song page with the demo saved
s = Script().wait(20).tap('ENTER').wait(560).shot('motifs')
s.tap('TAB', 'TAB', 'TAB', 'TAB', 'TAB', 'TAB', 'TAB', 'TAB').wait(40).shot('motifs_pcm')
s.tap(*['TAB'] * 8).tap('F2').tap(*['F12'] * 6).wait(30).shot('son_psg')
s.tap(*['F12'] * 18).wait(30).shot('son_pcm')
s.tap('S', hold='CTRL').tap('F3').wait(30).shot('morceau')
s.tap('F4').wait(10).shot('aide')
run(s, nv)
# a first song: new song, edit mode, four notes
t = Script().wait(20).tap('F3').tap('E', hold='CTRL').tap('F1').tap('SPACE').tap('Z', 'C', 'B', 'COMMA').wait(250).shot('premier')
run(t)
