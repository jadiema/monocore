#!/usr/bin/env python3
"""Assemble the single-file simulator page: core + page template + docs + embedded ROMs.
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
}
page = open(os.path.join(ROOT, 'sim/page.html'), encoding='utf-8').read()
core = open(os.path.join(ROOT, 'sim/core.js'), encoding='utf-8').read()
assert '</script' not in core
page = page.replace('__CORE_JS__', core)
page = page.replace('__ROMS_JSON__', json.dumps(roms))
page = page.replace('__SPEC_HTML__', md('SPEC.md'))
page = page.replace('__GUIDE_HTML__', md('GUIDE.md'))
os.makedirs(os.path.dirname(out), exist_ok=True)
open(out, 'w', encoding='utf-8').write(page)
print('%s: %d bytes' % (out, len(page)))
