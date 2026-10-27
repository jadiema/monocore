#!/usr/bin/env python3
"""Screen shots of MONOATELIER and its tools for its manual, taken in the simulator (tools/run.js).

usage: python3 manual/tools/atelier_shots.py   -> manual/images/atelier_*.png (640 x 480; the games doubled)
One run of the atelier ROM goes through every program with one NVRAM, as a user would.
"""
import os, subprocess, tempfile
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ROM = os.path.join(ROOT, 'examples/atelier/build/monoatelier.mnc')
IMG = os.path.join(ROOT, 'manual/images')
SC = dict(A=0x1C, B=0x32, C=0x21, D=0x23, E=0x24, F=0x2B, G=0x34, H=0x33, I=0x43, J=0x3B, K=0x42, L=0x4B, M=0x3A, N=0x31,
          O=0x44, P=0x4D, Q=0x15, R=0x2D, S=0x1B, T=0x2C, U=0x3C, V=0x2A, W=0x1D, X=0x22, Y=0x35, Z=0x1A)
F1, F2, F3, F4, F5, F12, ESC, ENTER, SPACE = 0x05, 0x06, 0x04, 0x0C, 0x03, 0x07, 0x76, 0x5A, 0x29


class Script:
    def __init__(self): self.f, self.x, self.y, self.b, self.ev = 2, 0, 0, 0, []
    def wait(self, n): self.f += n; return self
    def move(self, x, y):
        while (self.x, self.y) != (x, y):
            dx, dy = max(-200, min(200, x - self.x)), max(-200, min(200, y - self.y))
            self.ev.append('%d:ms=%d,%d,%d,0' % (self.f, dx, dy, self.b)); self.x += dx; self.y += dy; self.f += 1
        return self
    def btn(self, b): self.b = b; self.ev.append('%d:ms=0,0,%d,0' % (self.f, b)); self.f += 2; return self
    def click(self, x, y): return self.move(x, y).btn(1).btn(0)
    def key(self, code, ctrl=False):
        if ctrl: self.ev.append('%d:kd=14' % self.f)
        self.ev.append('%d:kd=%x' % (self.f, code)); self.ev.append('%d:ku=%x' % (self.f + 1, code))
        if ctrl: self.ev.append('%d:ku=14' % (self.f + 1))
        self.f += 3; return self
    def type(self, text):                         # AZERTY
        for ch in text:
            if ch == ' ': self.key(SPACE)
            elif ch == '\n': self.key(ENTER)
            elif ch == 'M': self.key(0x4C)
            else: self.key(SC[{'A': 'Q', 'Q': 'A', 'Z': 'W', 'W': 'Z'}.get(ch, ch)])
        return self
    def pad(self, v): self.ev.append('%d:p1=%x' % (self.f, v)); self.f += 1; return self
    def shot(self, name): self.ev.append('%d:shot=%s' % (self.f, name)); self.f += 1; return self


s = Script().wait(30).shot('lanceur_vide')
s.key(F1).wait(40).click(352 + 8, 48 + 8 * 16 + 8).move(150, 150).wait(5).shot('pixel_dessin')       # MONOPIXEL, the hero
s.key(F2).wait(20).shot('pixel_lutins')
s.key(F3).wait(10).type('O').move(120, 200).wait(5).shot('pixel_decors')
s.key(SC['S'], ctrl=True).key(F4).wait(10).shot('pixel_banque')
s.key(F12).wait(40)
s.key(F2).wait(40).click(30, 3 * 16 + 8).wait(4).shot('sfx_effets')                                 # MONOSFX: SAUT
s.key(SC['S'], ctrl=True).key(F12).wait(40)
s.key(F3).wait(80).key(SC['S'], ctrl=True).wait(5).key(F12).wait(40)                                # MONOTRACKER: the demo song saved
s.shot('lanceur_plein')
s.key(F4).wait(40).type('EXEMPLE\n').type('LIST\n').wait(5).shot('basic_liste')                   # MONOBASIC
s.key(F5).wait(60).pad(0x08).wait(40).pad(0x18).wait(14).pad(0x08).wait(10).shot('basic_jeu')
s.pad(0).key(ESC).wait(10).type('AIDE\n').wait(5).shot('basic_aide')
s.key(F12).wait(40)
s.key(F5).wait(60).shot('jeu_titre').pad(0x10).wait(2).pad(0).wait(30).pad(0x08).wait(60).shot('jeu_partie')
out = tempfile.mkdtemp()
env = dict(os.environ, NVRAM=os.path.join(out, 'monoatelier.sav'))
subprocess.run(['node', os.path.join(ROOT, 'tools/run.js'), ROM, out, ' '.join(s.ev)], check=True, env=env, stdout=subprocess.DEVNULL)
for f in sorted(os.listdir(out)):
    if f.endswith('.ppm'):
        im = Image.open(os.path.join(out, f))
        if im.height < 400: im = im.resize((im.width * 2, im.height * 2), Image.NEAREST)
        im.save(os.path.join(IMG, 'atelier_%s.png' % f[:-4]))
        print('atelier_%s.png' % f[:-4], im.size)
