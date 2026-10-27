#!/usr/bin/env python3
"""The graphics banks of MONOPIXEL, between the NVRAM save, the games and the picture files.

usage:
    python3 mgfx.py monopixel.sav                         lists the 8 slots
    python3 mgfx.py monopixel.sav 2 -o bank.mgf           slot 2 -> bank.mgf (data = bank.mgf, module mgfx)
    python3 mgfx.py monopixel.sav 2 -o bank.h             slot 2 -> C array (const uint8_t bank[])
    python3 mgfx.py bank.mgf --dump                       contents as text (palettes, sprites, maps)
    python3 mgfx.py bank.mgf --png outdir                 the sheet, each sprite and each map as PNG
    python3 mgfx.py --new bank.mgf --import hero.png:0,0:1 [--import ...] [--name NAME]
                                                          a bank made from pictures (at pixel x,y of the
                                                          sheet, in palette p); --sprite NAME:x,y,w,h,pal[,frames,speed]
    python3 mgfx.py bank.mgf --to-sav monopixel.sav 3     puts the bank into slot 3 of a save (created
                                                          if missing), to go on drawing it in MONOPIXEL
On MiSTer, the save is saves/MONOCORE/<name of the ROM>.sav on the SD card; it is written when the
OSD is opened after a save in MONOPIXEL. In the simulator: Exporter .sav / Banques .mgf.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mgbank import Bank, Sprite, read_nvram, write_nvram, rgb888, ATTR_NAMES


def dump(b):
    print('%s - %d page(s), %d palette(s), %d sprite(s), %d decor(s)' % (b.name, b.pages, len(b.pals), len(b.sprites), len(b.maps)))
    for i, p in enumerate(b.pals):
        print('palette %2d: %s' % (i, ' '.join('%02X%02X%02X' % rgb888(c) for c in p)))
    for i, s in enumerate(b.sprites):
        print('sprite %2d %-8s cellule %2d,%2d  %dx%d cellules  palette %d  %d image(s)  vitesse %d%s'
              % (i, s.name, s.x, s.y, s.w, s.h, s.pal, s.frames, s.speed, '  aller-retour' if s.flags & 1 else ''))
    for i, m in enumerate(b.maps):
        used = sum(1 for e in m.cells if e)
        print('decor %d: %dx%d tuiles de %d pixels, %d cases non vides, %d objet(s)' % (i, m.w, m.h, 16 if m.tile16 else 8, used, len(m.objects)))
        for o in m.objects:
            nm = b.sprites[o.sprite].name if o.sprite < len(b.sprites) else '?'
            print('  objet %-8s x %4d y %4d  param %3d%s' % (nm, o.x, o.y, o.param, '  miroir' if o.flags & 1 else ''))
    tiles16 = any(m.tile16 for m in b.maps) or not b.maps
    for t in range(len(b.attrs) // 4 if tiles16 else len(b.attrs)):
        a = b.tile_attr(t, tiles16)
        if a:
            print('tuile %s %3d : %s' % ('16' if tiles16 else '8', t, ', '.join(n for bit, n in ATTR_NAMES.items() if a & bit)))


def main():
    ap = argparse.ArgumentParser(description='MONOPIXEL graphics banks')
    ap.add_argument('file', nargs='?', help='monopixel.sav or bank.mgf')
    ap.add_argument('slot', nargs='?', type=int, help='slot 1-8 of the save')
    ap.add_argument('-o', '--output', help='.mgf (binary) or .h (C array)')
    ap.add_argument('--dump', action='store_true')
    ap.add_argument('--png', metavar='DIR')
    ap.add_argument('--new', metavar='OUT')
    ap.add_argument('--name')
    ap.add_argument('--import', dest='imports', action='append', default=[], metavar='PNG:x,y:pal')
    ap.add_argument('--sprite', action='append', default=[], metavar='NAME:x,y,w,h,pal[,frames,speed]')
    ap.add_argument('--to-sav', nargs=2, metavar=('SAV', 'SLOT'))
    a = ap.parse_args()

    if a.new:
        b = Bank(a.name or 'IMPORT')
        for spec in a.imports:
            path, pos, pal = (spec.split(':') + ['0,0', '0'])[:3]
            x, y = (int(v) for v in pos.split(','))
            b.import_image(path, x, y, int(pal))
        for spec in a.sprite:
            nm, nums = spec.split(':')
            v = [int(n) for n in nums.split(',')] + [1, 0]
            b.sprites.append(Sprite(v[0], v[1], v[2], v[3], v[4], v[5], v[6], 0, nm.upper()[:8]))
        open(a.new, 'wb').write(b.to_bytes())
        print('%s: %d bytes' % (a.new, len(b.to_bytes())))
        return
    if not a.file:
        ap.error('a file is needed')
    data = open(a.file, 'rb').read()
    if data[:4] == b'MGF1':
        bank_bytes = data
    else:
        banks = read_nvram(data)
        if a.slot is None:
            if not banks:
                print('aucune banque dans cette sauvegarde')
            for k, bb in banks:
                bk = Bank.from_bytes(bb)
                print('%d: %-16s %5d octets, %d page(s), %d sprite(s), %d decor(s)' % (k + 1, bk.name, len(bb), bk.pages, len(bk.sprites), len(bk.maps)))
            return
        found = dict(banks)
        if a.slot - 1 not in found:
            sys.exit('emplacement %d vide' % a.slot)
        bank_bytes = found[a.slot - 1]
    b = Bank.from_bytes(bank_bytes)
    if a.dump:
        dump(b)
    if a.png:
        os.makedirs(a.png, exist_ok=True)
        for p in range(len(b.pals)):
            b.sheet_image(p, 2).save(os.path.join(a.png, 'planche_pal%d.png' % p))
        for i, s in enumerate(b.sprites):
            ims = [b.sprite_image(i, f, 4) for f in range(s.frames)]
            nm = (s.name or 'sprite%d' % i).lower()
            if len(ims) > 1 and s.speed:
                ims[0].save(os.path.join(a.png, nm + '.gif'), save_all=True, append_images=ims[1:],
                            duration=max(20, s.speed * 1000 // 60), loop=0, disposal=2)
            ims[0].save(os.path.join(a.png, nm + '.png'))
        for i in range(len(b.maps)):
            b.map_image(i).save(os.path.join(a.png, 'decor%d.png' % i))
        print('images dans', a.png)
    if a.to_sav:
        sav, slot = a.to_sav[0], int(a.to_sav[1])
        old = open(sav, 'rb').read() if os.path.exists(sav) else None
        open(sav, 'wb').write(write_nvram(old, slot - 1, bank_bytes))
        print('%s: banque %s dans l\'emplacement %d' % (sav, b.name, slot))
    if a.output:
        if a.output.endswith('.h'):
            ident = ''.join(c if c.isalnum() else '_' for c in os.path.splitext(os.path.basename(a.output))[0])
            lines = ['// %s: MONOPIXEL bank (tools/mgfx/mgfx.py)' % b.name, '#include <stdint.h>',
                     'static const uint8_t %s[%d] __attribute__((aligned(4))) = {' % (ident, len(bank_bytes))]
            for i in range(0, len(bank_bytes), 24):
                lines.append('  ' + ', '.join(str(x) for x in bank_bytes[i:i + 24]) + ',')
            lines.append('};')
            open(a.output, 'w').write('\n'.join(lines) + '\n')
        else:
            open(a.output, 'wb').write(bank_bytes)
        print('%s: %d octets' % (a.output, len(bank_bytes)))


if __name__ == '__main__':
    main()
