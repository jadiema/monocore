"""Building blocks shared by the user's manuals (MONOTRACKER, MIDI guide): same look as the programmer's
manual (manual.css), HTML then PDF through Chromium, table of contents with page numbers."""
import html, os, re

# ---------------------------------------------------------------- building blocks (as in manual.py)
def esc(s): return html.escape(s, quote=False)
KW = r'\b(int|void|static|const|char|for|while|if|else|return|break|continue|typedef|struct|uint8_t|uint16_t|uint32_t|int8_t|int16_t|volatile|extern|sizeof|unsigned|bool|size_t|true|false)\b'
def highlight(code):
    out, pos = [], 0
    pat = re.compile(r'(//[^\n]*)|("(?:[^"\\]|\\.)*")|(^#\w+[^\n]*)|' + KW + r'|(\b\d+\b|\b0x[0-9A-Fa-f]+\b)|([A-Z][A-Z0-9_]{2,})', re.M)
    for m in pat.finditer(code):
        out.append(esc(code[pos:m.start()]))
        t = esc(m.group(0))
        cls = 'c-com' if m.group(1) else 'c-str' if m.group(2) else 'c-pre' if m.group(3) else 'c-kw' if m.group(4) else 'c-num' if m.group(5) else 'c-cst'
        out.append('<span class="%s">%s</span>' % (cls, t))
        pos = m.end()
    out.append(esc(code[pos:]))
    return ''.join(out)
def listing(code, caption=None, numbers=True, lang='c'):
    body = highlight(code.strip('\n')) if lang == 'c' else esc(code.strip('\n'))
    rows = [('<span class="ln">%3d</span>' % (i + 1) if numbers else '') + (l or ' ') for i, l in enumerate(body.split('\n'))]
    cap = '<div class="lst-cap"><span>LISTING</span> %s</div>' % esc(caption) if caption else ''
    return '<div class="listing %s">%s<pre>%s</pre></div>' % ('' if numbers else 'nonum', cap, '\n'.join(rows))
def shell(code, caption=None):
    cap = '<div class="lst-cap sh"><span>TERMINAL</span> %s</div>' % esc(caption) if caption else ''
    return '<div class="terminal">%s<pre>%s</pre></div>' % (cap, esc(code.strip('\n')))
def screen(img, caption, small=False):
    return ('<figure class="tv%s"><div class="bezel"><img src="images/%s"></div><figcaption>%s</figcaption></figure>'
            % (' small' if small else '', img, caption))
def box(kind, title, text): return '<div class="box %s"><div class="box-t">%s</div>%s</div>' % (kind, title, text)
def table(head, rows, cls=''):
    h = ''.join('<th>%s</th>' % e for e in head)
    b = ''.join('<tr>%s</tr>' % ''.join('<td>%s</td>' % c for c in r) for r in rows)
    return '<table class="%s"><thead><tr>%s</tr></thead><tbody>%s</tbody></table>' % (cls, h, b)
CHAPTERS = []
def chapter(num, title, lead, colour):
    CHAPTERS.append((num, title))
    return ('<section class="chap-open c%s"><span class="mark">QQCH%sQQ</span><div class="chap-num">%s</div><div class="chap-band"></div>'
            '<h1 id="ch%s">%s</h1><p class="lead">%s</p></section>') % (colour, num, num, num, title, lead)
def h2(t): return '<h2>%s</h2>' % t
def h3(t): return '<h3>%s</h3>' % t
def p(t): return '<p>%s</p>' % t
def ul(items): return '<ul>%s</ul>' % ''.join('<li>%s</li>' % i for i in items)
def ol(items): return '<ol>%s</ol>' % ''.join('<li>%s</li>' % i for i in items)
def c(t): return '<code>%s</code>' % esc(t)
def k(*keys): return ' '.join('<kbd>%s</kbd>' % esc(x) for x in keys)


EXTRA_CSS = """
kbd { font: 600 8pt 'IBM Plex Mono', monospace; display: inline-block; padding: 0 1.4mm; border: 1px solid #2c2a25; border-bottom-width: 2px;
  border-radius: 1.2mm; background: #fbf6e8; white-space: nowrap; line-height: 1.35; }
.c10 .chap-num { background: var(--blue); } .c11 .chap-num { background: var(--red); } .c12 .chap-num { background: var(--orange); }
.cover-title { font-size: 34pt; }
"""
CSS = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'manual.css'), encoding='utf-8').read() + EXTRA_CSS

def build(parts, cover, out_pdf, out_html, title, footer_label, subject, toc=True):
    """parts: HTML pieces ('@@TOC@@' where the table of contents goes); writes the PDF and the HTML"""
    from playwright.sync_api import sync_playwright
    from pypdf import PdfReader, PdfWriter
    def page(content):
        return """<!doctype html><html lang="fr"><head><meta charset="utf-8"><title>%s</title>
<style>%s</style></head><body>%s</body></html>""" % (esc(title), CSS, content)
    t = ['<section class="toc-page"><h2>Sommaire</h2><ol class="toc">']
    for num, ttl in CHAPTERS:
        t.append('<li><a href="#ch%s"><span class="tn">%s</span><span class="tt">%s</span><span class="tp"></span><span class="pg">@@PG%s@@</span></a></li>' % (num, num, ttl, num))
    t.append('</ol></section>')
    body = '\n'.join(parts).replace('@@TOC@@', '\n'.join(t) if toc else '')
    foot = """<div style="width:100%%;font-family:Archivo,sans-serif;font-size:8px;color:#777;padding:0 20mm;display:flex;justify-content:space-between;align-items:center">
<span style="letter-spacing:.15em">%s</span>
<span style="display:flex;gap:2px;align-items:center"><i style="display:inline-block;width:10px;height:3px;background:#e23b2e"></i><i style="display:inline-block;width:10px;height:3px;background:#f28c1b"></i><i style="display:inline-block;width:10px;height:3px;background:#f2c81b"></i><i style="display:inline-block;width:10px;height:3px;background:#3aa655"></i><i style="display:inline-block;width:10px;height:3px;background:#2f6fd6"></i>
<b style="margin-left:6px;color:#222;font-size:9px" class="pageNumber"></b></span></div>""" % esc(footer_label)
    def render(html_text, path, footer=True):
        open(out_html, 'w', encoding='utf-8').write(html_text)
        with sync_playwright() as pw:
            b = pw.chromium.launch()
            pg = b.new_page()
            pg.goto('file://' + out_html)
            pg.wait_for_timeout(500)
            pg.pdf(path=path, print_background=True, prefer_css_page_size=True, display_header_footer=footer,
                   header_template='<div></div>', footer_template=foot)
            b.close()
    tmp_body, tmp_cover = out_pdf + '.body.pdf', out_pdf + '.cover.pdf'
    render(page(body), tmp_body)
    pages = {}
    for i, pgo in enumerate(PdfReader(tmp_body).pages):
        for m in re.finditer(r'QQCH(\w+)QQ', pgo.extract_text() or ''):
            pages.setdefault(m.group(1), i + 1)
    final = body
    for num, _ in CHAPTERS:
        final = final.replace('@@PG%s@@' % num, str(pages.get(str(num), '?')))
    final = re.sub(r'<span class="mark">QQCH\w+QQ</span>', '', final)
    render(page(final), tmp_body)
    render(page(cover).replace('<body>', '<body class="cover-page">').replace('</head>', '<style>@page { margin: 0 }</style></head>'), tmp_cover, footer=False)
    w = PdfWriter()
    for f in (tmp_cover, tmp_body):
        for pgo in PdfReader(f).pages: w.add_page(pgo)
    w.add_metadata({'/Title': title, '/Author': 'MONOCORE-1', '/Subject': subject})
    with open(out_pdf, 'wb') as fo: w.write(fo)
    os.remove(tmp_body); os.remove(tmp_cover)
    open(out_html, 'w', encoding='utf-8').write(page(cover + final))
    print('PDF:', out_pdf, '-', len(PdfReader(out_pdf).pages), 'pages', pages)
