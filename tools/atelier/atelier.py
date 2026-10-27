#!/usr/bin/env python3
"""The NVRAM of MONOATELIER (monoatelier.sav): list, export for the games, import the saves of the
editors alone.

usage:
    python3 atelier.py monoatelier.sav                          what is in it
    python3 atelier.py monoatelier.sav --export dossier/        every item: images_1.mgf, bruits_1.mfx,
                                                                musique_1.mts... (for data = ... of a game)
    python3 atelier.py monoatelier.sav --import monopixel.sav monosfx.sav monotracker.sav
                                                                the items of the editors alone go into the
                                                                same slots of the atelier (created if missing)
    python3 atelier.py monoatelier.sav --put 1 3 niveau.mgf     a file into slot 3 of MONOPIXEL (0 MONOTRACKER,
                                                                1 MONOPIXEL, 2 MONOSFX)
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atnv

EXT = {atnv.TRACKER: ('musique', '.mts'), atnv.PIXEL: ('images', '.mgf'), atnv.SFX: ('bruits', '.mfx')}


def name_of(app, it):
    if app == atnv.PIXEL: return it[16:32].split(b'\0')[0].decode('ascii', 'replace')
    if app == atnv.SFX: return it[12:28].split(b'\0')[0].decode('ascii', 'replace')
    try:
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../mtrack'))
        from mtsong import Song
        return Song.decode(it).name
    except Exception:
        return '?'


def main():
    ap = argparse.ArgumentParser(description='MONOATELIER NVRAM')
    ap.add_argument('sav')
    ap.add_argument('--export', metavar='DIR')
    ap.add_argument('--import', dest='imports', nargs='+', metavar='SAV')
    ap.add_argument('--put', nargs=3, metavar=('APP', 'SLOT', 'FILE'))
    a = ap.parse_args()
    data = open(a.sav, 'rb').read() if os.path.exists(a.sav) else None
    if data and not atnv.is_atelier(data):
        sys.exit('%s : ce n\'est pas la NVRAM de MONOATELIER (ATNV)' % a.sav)
    if a.imports or a.put:
        items = {}
        for f in a.imports or []:
            d = open(f, 'rb').read()
            app = next((k for k, m in atnv.OWN_MAGIC.items() if d[:4] == m), None)
            if app is None:
                sys.exit('%s : NVRAM d\'aucun editeur' % f)
            for k, it in enumerate(atnv.read_own(d, app)):
                if it: items[(app, k)] = it
            print('%s : %s, %d element(s)' % (f, atnv.APP_NAMES[app], sum(1 for it in atnv.read_own(d, app) if it)))
        if a.put:
            items[(int(a.put[0]), int(a.put[1]) - 1)] = open(a.put[2], 'rb').read()
        data = atnv.write(data, items)
        open(a.sav, 'wb').write(data)
    if not data:
        sys.exit('%s : introuvable' % a.sav)
    for app in (atnv.PIXEL, atnv.SFX, atnv.TRACKER):
        print(atnv.APP_NAMES[app])
        for k, it in enumerate(atnv.read(data, app)):
            print('  %d  %s' % (k + 1, '-' if not it else '%-16s %5d octets' % (name_of(app, it), len(it))))
            if it and a.export:
                os.makedirs(a.export, exist_ok=True)
                p = os.path.join(a.export, '%s_%d%s' % (EXT[app][0], k + 1, EXT[app][1]))
                open(p, 'wb').write(it)
    if a.export:
        print('fichiers dans', a.export)


if __name__ == '__main__':
    main()
