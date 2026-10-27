#!/usr/bin/env python3
"""Builds the tools of MONOCORE-1, ready for the SD card: each one alone, and the atelier with all of them.

usage: python3 tools/mkoutils.py      -> outils/*.mnc and outils/LISEZMOI.md (sizes, SHA-256)

Each standalone tool has its own save on MiSTer (saves/MONOCORE/<file name>.sav); the atelier has one
save for all its programs (sdk/ATELIER.md). They need the core in version 5 (640 x 480 interlaced).
"""
import datetime, hashlib, os, shutil, subprocess, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
OUT = os.path.join(ROOT, 'outils')
TOOLS = [  # file on the SD card, project, what it is
    ('MONOATELIER.mnc', None, "l'atelier : les cinq programmes ci-dessous dans une seule ROM, avec une seule sauvegarde"),
    ('MONOPIXEL.mnc', 'examples/monopixel', 'les images : planche, palettes, lutins animés, décors, attributs des tuiles, objets'),
    ('MONOSFX.mnc', 'examples/monosfx', 'les bruitages : 32 sons PSG par banque, générateurs'),
    ('MONOTRACKER.mnc', 'examples/monotracker', 'la musique : morceaux de 16 pistes (PSG et échantillons), clavier MIDI'),
    ('MONOBASIC.mnc', 'examples/monobasic', 'le BASIC : écrire un jeu sur la console'),
    ('PLATEFORMES.mnc', 'examples/plateformes', 'un jeu de plateformes tout fait, à refaire avec les outils'),
]
os.makedirs(OUT, exist_ok=True)
rows = []
for fname, folder, what in TOOLS:
    if folder:
        d = os.path.join(ROOT, folder)
        subprocess.run([sys.executable, os.path.join(ROOT, 'sdk/build.py'), d], cwd=d, check=True, stdout=subprocess.DEVNULL)
        name = [l.split('=', 1)[1].strip() for l in open(os.path.join(d, 'project.txt')) if l.startswith('name')][0]
        src = os.path.join(d, 'build', name + '.mnc')
    else:
        subprocess.run([sys.executable, os.path.join(ROOT, 'tools/mkatelier.py')], check=True, stdout=subprocess.DEVNULL)
        src = os.path.join(ROOT, 'examples/atelier/build/monoatelier.mnc')
    dst = os.path.join(OUT, fname)
    shutil.copyfile(src, dst)
    data = open(dst, 'rb').read()
    rows.append((fname, what, len(data), hashlib.sha256(data).hexdigest()))
    print('%-16s %9d octets' % (fname, len(data)))
commit = subprocess.run(['git', 'rev-parse', '--short', 'HEAD'], cwd=ROOT, capture_output=True, text=True).stdout.strip() or '?'
lines = ['# Les outils de la MONOCORE-1, prêts à l\'emploi', '',
         'Copiez les fichiers `.mnc` dans `games/MONOCORE` sur la carte SD de la MiSTer. Ils demandent le core en',
         '**version 5** (`releases/v5_haute_resolution/`), branché à une **souris** et un **clavier** USB.', '',
         '- **`MONOATELIER.mnc`** suffit pour tout faire. Il réunit les cinq programmes, qui partagent une seule',
         '  sauvegarde (`saves/MONOCORE/MONOATELIER.sav`) : ce qu\'on fait dans un outil sert aussitôt aux autres et',
         '  au jeu. Le mode d\'emploi est `manual/MONOATELIER_Manuel.pdf`.',
         '- Les autres fichiers sont les mêmes programmes **seuls**. Chacun a sa propre sauvegarde',
         '  (`saves/MONOCORE/MONOPIXEL.sav`…). `tools/atelier/atelier.py --import` en reprend le contenu dans l\'atelier.', '',
         '| Fichier | Contenu | Taille | SHA-256 |', '|---|---|---|---|']
for fname, what, size, sha in rows:
    lines.append('| `%s` | %s | %s Ko | `%s…` |' % (fname, what, format(size // 1024, ',').replace(',', ' '), sha[:16]))
lines += ['', 'Construits par `python3 tools/mkoutils.py` le %s, à partir du commit `%s`.' % (datetime.date.today().isoformat(), commit),
          'Dans le simulateur (`dist/monocore.html`), MONOATELIER et chacun des outils sont dans la liste des ROM intégrées.']
open(os.path.join(OUT, 'LISEZMOI.md'), 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
print('outils/LISEZMOI.md')
