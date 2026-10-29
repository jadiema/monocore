#!/usr/bin/env python3
"""Assemble the single-file simulator page: core + page template + docs (French and English) + embedded ROMs
+ MiSTer video filter files.
usage: build_page.py out.html"""
import base64, json, os, sys
import markdown

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'dist', 'monocore.html')

def md(path):
    return markdown.markdown(open(os.path.join(ROOT, path), encoding='utf-8').read(), extensions=['tables', 'fenced_code'])

roms = {
    'aube': base64.b64encode(open(os.path.join(ROOT, 'examples/aube/build/aube.mnc'), 'rb').read()).decode(),
    'test': base64.b64encode(open(os.path.join(ROOT, 'rom/build/testrom.mnc'), 'rb').read()).decode(),
    'hello': base64.b64encode(open(os.path.join(ROOT, 'examples/hello/build/hello.mnc'), 'rb').read()).decode(),
    'tracker': base64.b64encode(open(os.path.join(ROOT, 'examples/monotracker/build/monotracker.mnc'), 'rb').read()).decode(),
    'plateformes': base64.b64encode(open(os.path.join(ROOT, 'examples/plateformes/build/plateformes.mnc'), 'rb').read()).decode(),
    'basic': base64.b64encode(open(os.path.join(ROOT, 'examples/monobasic/build/monobasic.mnc'), 'rb').read()).decode(),
    'atelier': base64.b64encode(open(os.path.join(ROOT, 'examples/atelier/build/monoatelier.mnc'), 'rb').read()).decode(),
    'sfx': base64.b64encode(open(os.path.join(ROOT, 'examples/monosfx/build/monosfx.mnc'), 'rb').read()).decode(),
    'pixel': base64.b64encode(open(os.path.join(ROOT, 'examples/monopixel/build/monopixel.mnc'), 'rb').read()).decode(),
    'cascade': base64.b64encode(open(os.path.join(ROOT, 'examples/cascade/build/cascade.mnc'), 'rb').read()).decode(),
    'defender': base64.b64encode(open(os.path.join(ROOT, 'examples/defender/build/defender.mnc'), 'rb').read()).decode(),
    'solfege': base64.b64encode(open(os.path.join(ROOT, 'examples/solfege/build/solfege.mnc'), 'rb').read()).decode(),
}
page = open(os.path.join(ROOT, 'sim/page.html'), encoding='utf-8').read()
core = open(os.path.join(ROOT, 'sim/core.js'), encoding='utf-8').read()
assert '</script' not in core
page = page.replace('__CORE_JS__', core)
page = page.replace('__ROMS_JSON__', json.dumps(roms))
# MiSTer video filters (sim/crt/: Presets, Filters, Gamma, Shadow_Masks, copied from the MiSTer-devel repositories)
crt_dir = os.path.join(ROOT, 'sim', 'crt')
crt = {'files': {}, 'presets': []}
for d, _, names in sorted(os.walk(crt_dir)):
    for n in sorted(names):
        if not n.endswith(('.txt', '.ini')):
            continue
        rel = os.path.relpath(os.path.join(d, n), crt_dir).replace(os.sep, '/')
        crt['files'][rel] = open(os.path.join(d, n), encoding='utf-8', errors='replace').read()
        if rel.startswith('Presets/'):
            crt['presets'].append(rel[len('Presets/'):])
crt['presets'].sort(key=lambda p: (p.count('/') == 0, p.lower()))
page = page.replace('__CRT_JSON__', json.dumps(crt, sort_keys=True).replace('</', '<\\/'))
page = page.replace('__SPEC_HTML_EN__', md('SPEC.en.md'))
page = page.replace('__GUIDE_HTML_EN__', md('GUIDE.en.md'))
page = page.replace('__SPEC_HTML__', md('SPEC.md'))
page = page.replace('__GUIDE_HTML__', md('GUIDE.md'))
os.makedirs(os.path.dirname(out), exist_ok=True)
open(out, 'w', encoding='utf-8').write(page)
print('%s: %d bytes' % (out, len(page)))
