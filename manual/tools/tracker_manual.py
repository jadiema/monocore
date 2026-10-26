#!/usr/bin/env python3
"""Builds the MONOTRACKER user's manual: printable HTML, then PDF through Chromium.

usage: python3 manual/tools/tracker_manual.py   -> manual/MONOTRACKER_Manuel.pdf
Same look as the programmer's manual (manual.css). The screen shots come from
manual/tools/tracker_shots.py, which runs the tracker in the simulator.
"""
import html, os, re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(ROOT)
OUT_HTML = os.path.join(ROOT, 'tracker.html')
OUT_PDF = os.path.join(ROOT, 'MONOTRACKER_Manuel.pdf')

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

parts = []
A = parts.append

# ---------------------------------------------------------------- figures
def fig_screen():
    """the screen of the tracker with its zones numbered"""
    zones = [(0, 0, 120, 76, 1), (120, 0, 128, 76, 2), (248, 0, 136, 76, 3), (0, 76, 384, 24, 4), (0, 100, 384, 140, 5)]
    g = []
    for x, y, w, h, n in zones:
        g.append('<rect x="%d" y="%d" width="%d" height="%d" fill="none" stroke="#f2c81b" stroke-width="2" stroke-dasharray="5 3"/>' % (x + 1, y + 1, w - 2, h - 2))
        g.append('<circle cx="%d" cy="%d" r="9" fill="#e23b2e" stroke="#fff" stroke-width="1.5"/><text x="%d" y="%d" fill="#fff" font-size="11" font-weight="700" text-anchor="middle">%d</text>'
                 % (x + w - 12, y + 12, x + w - 12, y + 16, n))
    return ('<figure class="fig"><svg viewBox="0 0 384 240" xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" font-family="Archivo" style="width:125mm">'
            '<image href="images/tracker_motifs.png" x="0" y="0" width="384" height="240" style="image-rendering:pixelated"/>%s</svg>'
            '<figcaption>Les cinq zones de l\'écran, ici sur la page des motifs pendant la lecture de la démo.</figcaption></figure>') % ''.join(g)

def fig_cell():
    parts_ = [('C-4', 0, 3, '#f5efdf', 'note', 'C-4 = do de la 4e octave ; === note coupée'),
              ('01', 3, 2, '#5ad2f0', 'instrument', '01 à 1F, en hexadécimal'),
              ('40', 5, 2, '#78e66e', 'volume', '00 (muet) à 40 (plein)'),
              ('A05', 7, 3, '#fa9f3c', 'effet', 'une lettre 0-F et un paramètre')]
    cw, x0 = 30, 70
    g = ['<rect x="%d" y="40" width="%d" height="44" fill="#12162c" rx="3"/>' % (x0 - 8, cw * 10 + 16)]
    for txt, col, n, color, lab, desc in parts_:
        for i, ch in enumerate(txt):
            g.append('<text x="%d" y="72" fill="%s" font-family="IBM Plex Mono" font-size="28" font-weight="600" text-anchor="middle">%s</text>' % (x0 + (col + i) * cw + cw // 2, color, esc(ch)))
        cx = x0 + (col + n / 2) * cw
        g.append('<line x1="%d" y1="90" x2="%d" y2="108" stroke="#2c2a25"/>' % (cx, cx))
        g.append('<text x="%d" y="124" font-size="13" font-weight="700" text-anchor="middle" fill="#1d1b18">%s</text>' % (cx, lab))
    return ('<figure class="fig"><svg viewBox="0 0 440 132" xmlns="http://www.w3.org/2000/svg" font-family="Archivo" style="width:120mm">%s</svg>'
            '<figcaption>Une case de motif : dix caractères, quatre colonnes, chacune avec sa couleur à l\'écran.</figcaption></figure>') % ''.join(g)

def fig_piano():
    """the two rows of the piano on an AZERTY keyboard"""
    def keyrow(y, keys, notes, black, x0, dx=44):
        out = []
        for i, (key, note) in enumerate(zip(keys, notes)):
            x = x0 + i * dx
            if key is None: continue
            fill, ink = ('#2c2a25', '#f5efdf') if black else ('#fbf6e8', '#1d1b18')
            out.append('<rect x="%d" y="%d" width="38" height="38" rx="4" fill="%s" stroke="#2c2a25" stroke-width="1.5"/>' % (x, y, fill))
            out.append('<text x="%d" y="%d" font-family="IBM Plex Mono" font-size="15" font-weight="600" fill="%s">%s</text>' % (x + 6, y + 19, ink, esc(key)))
            out.append('<text x="%d" y="%d" font-size="9.5" font-weight="700" fill="%s" text-anchor="end">%s</text>' % (x + 34, y + 33, '#f2c81b' if black else '#e23b2e', esc(note)))
        return out
    g = []
    # upper row: octave + 1 (digits = sharps)
    g += keyrow(10, ['é', '"', None, '(', '-', 'è', None, 'ç', 'à'], ['C#', 'D#', '', 'F#', 'G#', 'A#', '', 'C#', 'D#'], True, 52)
    g += keyrow(52, ['A', 'Z', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P'], ['C', 'D', 'E', 'F', 'G', 'A', 'B', 'C', 'D', 'E'], False, 30)
    # lower row: the octave of OCT
    g += keyrow(112, ['S', 'D', None, 'G', 'H', 'J', None, 'L', 'M'], ['C#', 'D#', '', 'F#', 'G#', 'A#', '', 'C#', 'D#'], True, 74)
    g += keyrow(154, ['W', 'X', 'C', 'V', 'B', 'N', ',', ';', ':', '!'], ['C', 'D', 'E', 'F', 'G', 'A', 'B', 'C', 'D', 'E'], False, 52)
    g.append('<text x="490" y="62" font-size="12" font-weight="700">octave OCT + 1</text><text x="490" y="78" font-size="10" fill="#5d574c">rangées des chiffres</text><text x="490" y="92" font-size="10" fill="#5d574c">et de A Z E R T Y</text>')
    g.append('<text x="490" y="164" font-size="12" font-weight="700">octave OCT</text><text x="490" y="180" font-size="10" fill="#5d574c">rangées Q S D F</text><text x="490" y="194" font-size="10" fill="#5d574c">et W X C V</text>')
    return ('<figure class="fig"><svg viewBox="0 0 610 200" xmlns="http://www.w3.org/2000/svg" font-family="Archivo">%s</svg>'
            '<figcaption>Le piano sur un clavier AZERTY : touches claires pour les notes naturelles, foncées pour les dièses. '
            'Sur un clavier QWERTY, ce sont les mêmes touches physiques (Z S X D C… et Q 2 W 3 E…).</figcaption></figure>') % ''.join(g)

FIG_ADSR = '''
<figure class="fig"><svg viewBox="0 0 520 200" style="width:130mm" xmlns="http://www.w3.org/2000/svg" font-family="Archivo" font-size="11">
 <rect x="40" y="20" width="460" height="150" fill="#12162c" rx="3"/>
 <g stroke="#3c4270" stroke-dasharray="3 3"><line x1="110" y1="20" x2="110" y2="170"/><line x1="200" y1="20" x2="200" y2="170"/><line x1="380" y1="20" x2="380" y2="170"/></g>
 <polyline points="40,170 110,40 200,95 380,95 480,170" fill="none" stroke="#f2c81b" stroke-width="3"/>
 <g fill="#f5efdf" font-weight="700" text-anchor="middle"><text x="75" y="160">ATTAQUE</text><text x="155" y="160">DÉCLIN</text><text x="290" y="160">MAINTIEN</text><text x="430" y="160">RELÂCHE</text></g>
 <g fill="#5d574c" text-anchor="middle" font-size="10"><text x="75" y="188">montée</text><text x="155" y="188">descente</text><text x="290" y="188">niveau tenu tant que la note dure</text><text x="430" y="188">extinction</text></g>
 <text x="34" y="44" text-anchor="end" fill="#5d574c" font-size="9">max</text><text x="34" y="99" text-anchor="end" fill="#5d574c" font-size="9">tenu</text><text x="34" y="172" text-anchor="end" fill="#5d574c" font-size="9">0</text>
 <text x="380" y="14" text-anchor="middle" fill="#e23b2e" font-weight="700">=== (note coupée)</text>
</svg><figcaption>L'enveloppe d'un son : le volume suit ces quatre étapes, comptées en ticks (50 par seconde à 125 BPM).</figcaption></figure>'''

# ---------------------------------------------------------------- cover
COVER = '''<section class="cover">
 <div class="cover-stripes"><i></i><i></i><i></i><i></i><i></i></div>
 <div class="cover-top">MONOCORE-1 · MUSIQUE · 16 PISTES</div>
 <div class="cover-title">MONO<span>TRACKER</span></div>
 <div class="cover-sub">MANUEL DE L'UTILISATEUR</div>
 <div class="cover-shot"><img src="images/tracker_motifs.png"></div>
 <div class="cover-bottom">Composez au clavier, à la souris ou au clavier MIDI · Créez vos sons · Mettez la musique dans vos jeux</div>
 <div class="cover-ed">ÉDITION 2 · OCTOBRE 2026</div>
</section>'''

A('@@TOC@@')

A('<section class="foreword">' + h2('Avant-propos') +
p("<b>MONOTRACKER</b> est l'atelier de musique de la MONOCORE-1. Comme les trackers de l'Amiga, il découpe un morceau en <b>motifs</b> : des grilles de notes qui défilent de haut en bas, une ligne après l'autre, sur plusieurs pistes jouées en même temps. On y écrit la musique au clavier, comme sur un piano, et on la règle au clavier ou à la souris.") +
p("Il pilote les <b>16 canaux sonores</b> de la console : huit oscillateurs (PSG), qui fabriquent des sons d'arcade à partir d'ondes simples, et huit voix d'échantillons (PCM), qui jouent des sons enregistrés : batterie, basse, cordes, cuivres, chœur… Une page entière sert à fabriquer vos propres sons.") +
p("Les morceaux se gardent dans la mémoire sauvegardée de la console et passent dans vos jeux : le lecteur du tracker est une bibliothèque du SDK. Ce que vous entendez dans le tracker est exactement ce que le joueur entendra.") +
box('know', 'Ce que veut dire « tracker »', p("Un tracker lit ses motifs comme une partition qui défile : à chaque <b>ligne</b>, il déclenche les notes écrites sur les 16 pistes. Une ligne dure un certain nombre de <b>ticks</b> (la vitesse), et le nombre de ticks par seconde vient du tempo en BPM. Entre deux lignes, les effets et les enveloppes changent le son tick après tick : c'est ce qui donne vie aux notes.")) +
box('tip', 'Comment lire ce manuel', ul([
  "Le chapitre 1 présente l'outil et le chapitre 2 vous fait écrire un premier morceau en quelques minutes : commencez par eux.",
  "Les chapitres 3 à 7 décrivent l'écran, les motifs, les effets, les sons et l'organisation du morceau.",
  "Le chapitre 8 explique la sauvegarde et les échanges entre la MiSTer et le simulateur, le chapitre 9 le clavier MIDI, le chapitre 10 met la musique dans un jeu.",
  "Les annexes rassemblent toutes les touches et tous les gestes de la souris : gardez-les sous la main."])) +
box('know', 'Les nombres en hexadécimal', p("Comme dans tous les trackers, les numéros d'instrument, les volumes et les effets s'écrivent en <b>hexadécimal</b> : on compte de 0 à 9, puis A, B, C, D, E, F, puis 10. Ainsi <code>0A</code> vaut 10, <code>10</code> vaut 16, <code>1F</code> vaut 31 et <code>40</code> vaut 64. Les touches A à F du clavier servent de chiffres dans ces colonnes.")) +
'</section>')

# ---------------------------------------------------------------- 1
A(chapter(1, "MONOTRACKER en un coup d'oeil", "Ce qu'il sait faire, où le lancer, et ce qu'il lui faut.", 1))
A(p("MONOTRACKER est une ROM de la MONOCORE-1, comme un jeu. Il s'ouvre sur le morceau de démonstration : appuyez sur " + k('Entrée') + " pour l'écouter, et encore sur " + k('Entrée') + " pour l'arrêter."))
A(table(['Élément', 'Capacités'], [
  ['Pistes', "16 : pistes 1 à 8 sur les canaux PSG (oscillateurs), pistes A à H sur les voix PCM (échantillons)"],
  ['Motifs', "24 motifs de 64 lignes ; chaque case porte une note, un instrument, un volume et un effet"],
  ['Morceau', "jusqu'à 128 positions, chacune jouant un motif ; une position de reprise pour boucler"],
  ['Sons', "31 instruments, PSG ou PCM, avec enveloppe, vibrato, arpège, glissement et balayage d'onde"],
  ['Échantillons', "21 sons originaux fournis avec le tracker (batterie, basse, piano, cordes, cuivres, chœur…)"],
  ['Tempo', "32 à 255 BPM, 1 à 31 ticks par ligne ; 16 effets jouables à chaque ligne"],
  ['Sauvegarde', "8 emplacements dans la mémoire sauvegardée de 32 Ko, écrite sur la carte SD de la MiSTer"],
  ['Commande', "clavier (indispensable), souris, et clavier MIDI USB en option ; disposition AZERTY ou QWERTY au choix"]], 'specs'))
A(h2('Où le lancer'))
A(table(['Machine', 'Comment'], [
  ['MiSTer', "copiez " + c('monotracker.mnc') + " dans " + c('games/MONOCORE') + ", branchez un clavier USB (et une souris), puis chargez la ROM depuis le menu du core MONOCORE"],
  ['Simulateur', "ouvrez " + c('dist/monocore.html') + " et choisissez <b>MONOTRACKER</b> dans la liste des ROM intégrées : le clavier et la souris de la console s'activent tout seuls"],
  ['Version du core', "le clavier demande le core en version 2, la souris en version 3, le clavier MIDI en version 4 ; avec un core plus ancien, le bandeau l'indique (<i>CORE SANS SOURIS</i>) et tout reste utilisable au clavier"]]))

# ---------------------------------------------------------------- 2
A(chapter(2, "Un premier morceau", "Quatre notes, une batterie, une sauvegarde : le tour complet en dix minutes.", 2))
A(h2('Partir d\'une page blanche'))
A(ol([
  "Appuyez sur " + k('F3') + " pour ouvrir la page du <b>morceau</b>, puis sur " + k('Ctrl', 'E') + " : un nouveau morceau vide apparaît. Il reprend les instruments de la démo, prêts à l'emploi.",
  "Revenez aux motifs avec " + k('F1') + ". Le curseur est sur la piste <b>PSG 1</b>, ligne 00, et l'instrument courant est <b>01 LEAD</b> (dans le bandeau).",
  "Appuyez sur " + k('Espace') + " : la bande du curseur devient rouge et le mot <b>EDIT</b> s'affiche. Vous êtes en mode édition.",
  "Tapez " + k('W') + " " + k('C') + " " + k('B') + " " + k(';') + " : quatre notes s'écrivent l'une sous l'autre, do, mi, sol et le do de l'octave au-dessus. Vous les entendez en les tapant."]))
A(screen('tracker_premier.png', "Les quatre notes de l'accord de do majeur sur la piste PSG 1, en mode édition."))
A(p("Appuyez sur " + k('Maj', 'Entrée') + " : le motif se joue en boucle à partir du curseur. " + k('Échap') + " arrête tout."))
A(h2('Ajouter une batterie'))
A(ol([
  "Appuyez sur " + k('Tab') + " jusqu'à la piste <b>PCM 1</b> (huit fois) : les quatre pistes PCM 1 à 4 apparaissent à l'écran.",
  "Remontez en haut avec " + k('Origine') + ". Sur une piste PCM, l'instrument courant est celui des échantillons : appuyez sur " + k('F12') + " ou " + k('F11') + " jusqu'à <b>0A KICK</b>.",
  "Mettez le pas à 4 avec " + k('F8') + " (trois fois) : après chaque note, le curseur descendra de quatre lignes, soit un temps.",
  "Tapez " + k('W') + " seize fois : une grosse caisse sur chaque temps, du haut au bas du motif.",
  "Faites de même sur la piste PCM 2 avec <b>0B SNARE</b>, en commençant à la ligne 04 avec un pas de 8 : la caisse claire tombe sur les temps 2 et 4."]))
A(box('tip', 'Effacer une erreur', p(k('Suppr') + " efface la note sous le curseur, " + k('Maj', 'Suppr') + " vide toute la case. " + k('Inser') + " et " + k('Retour arrière') + " insèrent ou retirent une ligne, en décalant le reste de la piste.")))
A(h2('Sauvegarder'))
A(p("Appuyez sur " + k('Ctrl', 'S') + ". Le bandeau annonce l'emplacement utilisé. Sur la MiSTer, ouvrez ensuite le menu du core (la touche " + k('F12') + " du MiSTer ou le bouton OSD) : c'est à ce moment que la sauvegarde est écrite sur la carte SD. Au prochain lancement, MONOTRACKER rouvrira ce morceau."))
A(box('know', 'Donner un nom au morceau', p("Sur la page du morceau (" + k('F3') + "), descendez sur <b>NOM</b> dans le cadre RÉGLAGES (" + k('Tab') + " pour changer de cadre), appuyez sur " + k('Entrée') + ", tapez le nom, puis " + k('Entrée') + " de nouveau. Avec la souris, un clic sur le nom suffit.")))

# ---------------------------------------------------------------- 3
A(chapter(3, "L'écran", "Un panneau de commande toujours visible, et la page de travail en dessous.", 3))
A(fig_screen())
A(table(['Zone', 'Rôle'], [
  ['1. Compteurs', "POS (position jouée ou éditée), MOTIF (motif de cette position), LONG (nombre de positions), BPM, VIT (ticks par ligne), VOL (volume général), OCT (octave du piano), PAS (lignes descendues après une saisie) et INSTR (instrument courant). Les flèches ▲▼ les changent à la souris."],
  ['2. Boutons', "JOUER le morceau, MOTIF en boucle, STOP, EDIT (mode édition), SUIVI (le curseur suit la lecture), SAUVER, et les quatre pages : MOTIFS, SONS, MORCEAU, AIDE. Un bouton enfoncé est bleu."],
  ['3. Vumètres', "le niveau des 16 pistes, avec une marque de crête. Sous chaque barre, le nom de la piste : 1 à 8 pour les PSG, A à H pour les PCM ; blanc pour la piste du curseur, rouge si elle est muette."],
  ['4. Bandeau', "nom du morceau, instrument courant, durée de lecture ; en dessous, les messages en rouge, ou une aide sur ce que vous êtes en train de faire."],
  ['5. Page', "les motifs (" + k('F1') + "), les sons (" + k('F2') + ") ou le morceau (" + k('F3') + "). L'aide (" + k('F4') + ") occupe tout l'écran ; un clic ou " + k('F4') + " la referme."]]))
A(p("La souris déplace une flèche blanche. Un clic gauche augmente ou active, un clic droit diminue ou fait l'inverse, la molette fait défiler ou change une valeur. Maintenu sur une flèche ou une valeur, le clic se répète. L'annexe B donne tous les gestes."))

# ---------------------------------------------------------------- 4
A(chapter(4, "Écrire des motifs", "La grille de notes, le piano, et toutes les façons de la remplir.", 4))
A(h2('La grille'))
A(p("La page des motifs montre quatre pistes à la fois : PSG 1 à 4, PSG 5 à 8, PCM 1 à 4 ou PCM 5 à 8. " + k('Tab') + " passe à la piste suivante (" + k('Maj', 'Tab') + " à la précédente) et l'affichage suit. Le numéro du motif est en haut à gauche ; les numéros de ligne sont en hexadécimal, de 00 à 3F, avec un repère tous les quatre temps."))
A(fig_cell())
A(table(['Colonne', 'Contenu'], [
  ['note', "C-4 est le do de la 4e octave, C#4 le do dièse ; les notes vont de C-0 à B-7 ; " + c('===') + " est une note coupée"],
  ['instrument', "de 01 à 1F, en hexadécimal ; vide, la piste garde son instrument"],
  ['volume', "de 00 (muet) à 40 (plein) ; vide, la piste garde son volume"],
  ['effet', "une lettre de 0 à F et un paramètre de deux chiffres (chapitre 5)"]]))
A(p("Une case vide affiche des points. La couleur de l'instrument est rouge si son type ne correspond pas à la piste (un instrument PCM sur une piste PSG, par exemple) : cette note ne jouera pas."))
A(h2('Le piano'))
A(p("Dans la colonne des notes, les deux rangées de lettres du clavier forment un piano de deux octaves et demie. La rangée du bas joue l'octave choisie dans le compteur OCT (" + k('F5') + " et " + k('F6') + " pour la changer), celle du haut l'octave au-dessus."))
A(fig_piano())
A(p("La touche " + k('&') + " (celle du 1) écrit une <b>note coupée</b> " + c('===') + " : le son de la piste s'éteint selon le relâché de son instrument. Une note suivante sur la même piste remplace la précédente, sans note coupée."))
A(h2('Saisir et corriger'))
A(table(['Touches', 'Effet'], [
  [k('Espace'), "mode édition, ou mode jeu : hors édition, le piano joue sans rien écrire"],
  [k('↑') + ' ' + k('↓') + ' ' + k('←') + ' ' + k('→'), "déplace le curseur ; de gauche à droite, il passe par la note, les deux chiffres de l'instrument, ceux du volume, la lettre et les deux chiffres de l'effet"],
  [k('Pg.préc') + ' ' + k('Pg.suiv') + ' ' + k('Origine') + ' ' + k('Fin'), "16 lignes plus haut ou plus bas, première ou dernière ligne"],
  [k('0') + '…' + k('9') + ' ' + k('A') + '…' + k('F'), "chiffres de l'instrument, du volume et de l'effet : le curseur avance dans la colonne, puis descend du pas"],
  [k('F7') + ' ' + k('F8'), "pas d'avancement : 0 (le curseur ne bouge pas) à 16 lignes"],
  [k('F11') + ' ' + k('F12'), "instrument courant (ou " + k('pavé −') + ' ' + k('pavé +') + ") : il y en a un pour les pistes PSG et un pour les pistes PCM"],
  [k('Suppr'), "efface la colonne sous le curseur (la note efface aussi l'instrument) ; " + k('Maj', 'Suppr') + " vide la case"],
  [k('Inser') + ' ' + k('Retour arrière'), "insère une ligne vide, ou retire la ligne du curseur, sur la piste"]]))
A(box('warn', 'F12 sur la MiSTer', p("Sur la MiSTer, la touche " + k('F12') + " ouvre le menu du MiSTer et n'arrive pas au tracker. Pour passer à l'instrument suivant, utilisez " + k('pavé +') + " ou le compteur INSTR à la souris.")))
A(box('know', 'Le numéro d\'instrument', p("Taper un numéro d'instrument dans une case choisit aussi cet instrument pour les notes suivantes. Une note tapée au piano reçoit toujours l'instrument courant du type de la piste.")))
A(h2('Écouter pendant qu\'on écrit'))
A(p(k('Entrée') + " joue le morceau à partir de la position courante, " + k('Maj', 'Entrée') + " joue le motif en boucle à partir du curseur ; " + k('Entrée') + " de nouveau ou " + k('Échap') + " arrête. Avec le <b>suivi</b> (bouton SUIVI ou " + k('Ctrl', 'F') + "), le curseur suit la ligne jouée : on peut écrire en écoutant. Sans suivi, une bande bleu clair montre la ligne jouée."))
A(table(['Touches', 'Effet'], [
  [k('Ctrl', 'M'), "rend la piste du curseur muette, ou la rétablit"],
  [k('Ctrl', 'O'), "solo : seule la piste du curseur reste audible ; une seconde fois, toutes reviennent"],
  [k('Ctrl', 'C') + ' ' + k('Ctrl', 'X') + ' ' + k('Ctrl', 'V'), "copie, coupe ou colle la piste entière du motif"],
  [k('Alt', 'C') + ' ' + k('Alt', 'X') + ' ' + k('Alt', 'V'), "copie, coupe ou colle le motif entier (ses 16 pistes)"],
  [k('Ctrl', '↑') + ' ' + k('Ctrl', '↓'), "position précédente ou suivante du morceau"],
  [k('Ctrl', '←') + ' ' + k('Ctrl', '→'), "change le motif joué à la position courante"]]))
A(screen('tracker_motifs_pcm.png', "Les pistes PCM 1 à 4 de la démo : grosse caisse, caisses claires, charleston et basse, avec leurs volumes en vert."))

# ---------------------------------------------------------------- 5
A(chapter(5, "Les effets", "Une lettre, deux chiffres : arpèges, glissés, vibratos et changements de tempo.", 5))
A(p("La dernière colonne d'une case porte un <b>effet</b> : une lettre de 0 à F, puis un paramètre de deux chiffres hexadécimaux, souvent noté <i>xx</i>, ou <i>x</i> et <i>y</i> pour ses deux chiffres. L'effet ne vaut que pour sa ligne : pour qu'un vibrato dure, répétez-le à chaque ligne. Quand le curseur est sur un effet, le bandeau rappelle ce qu'il fait."))
A(table(['Effet', 'Nom', 'Action', 'Exemple'], [
  [c('0xy'), 'Arpège', "à chaque tick, la note, puis +x demi-tons, puis +y", c('037') + " : accord mineur"],
  [c('1xx'), 'Montée', "monte de xx/16 de demi-ton par tick", c('104') + " : glissé vers le haut"],
  [c('2xx'), 'Descente', "descend de xx/16 de demi-ton par tick", c('210') + " : chute d'un demi-ton par tick"],
  [c('3xx'), 'Portamento', "glisse vers la note de la case sans la redéclencher, à xx/16 de demi-ton par tick", c('308') + " avec une note"],
  [c('4xy'), 'Vibrato', "vitesse x, ampleur y/16 de demi-ton ; 0 garde la valeur précédente", c('464')],
  [c('8xx'), 'Panoramique', "00 à gauche, 80 au centre, FF à droite", c('8C0')],
  [c('9xx'), 'Rapport / début', "PSG : rapport cyclique du carré ; PCM : la note démarre à l'échantillon xx × 256", c('940') + " : carré fin"],
  [c('Axy'), 'Glissé de volume', "+x et −y de volume par tick", c('A02') + " : fondu"],
  [c('Bxx'), 'Saut', "passe à la position xx du morceau à la fin de la ligne", c('B00')],
  [c('Cxx'), 'Volume', "volume de la piste, 00 à 40", c('C20') + " : moitié"],
  [c('Dxx'), 'Fin de motif', "passe au motif suivant, à sa ligne xx", c('D00') + " : motif de 32 lignes"],
  [c('E1x') + ' ' + c('E2x'), 'Réglage fin', "monte ou descend de x/16 de demi-ton, une seule fois", c('E12')],
  [c('E9x'), 'Redéclenchement', "rejoue la note tous les x ticks", c('E93') + " : roulement"],
  [c('ECx'), 'Coupure', "coupe la note au tick x", c('EC2') + " : note très courte"],
  [c('EDx'), 'Retard', "retarde la note de x ticks", c('ED3') + " : contretemps"],
  [c('Fxx'), 'Vitesse / tempo', "01 à 1F : ticks par ligne ; 20 à FF : BPM", c('F04') + ', ' + c('F8C')]], 'ref'))
A(p("Les effets 5, 6 et 7 sont libres pour l'instant : la console les ignore."))
A(box('tip', 'Accords à une seule voix', p("Avec " + c('037') + " (mineur) ou " + c('047') + " (majeur) répété sur chaque ligne, une seule piste PSG joue un accord qui scintille, le son des musiques d'arcade et de démo. La piste ARPEGE de la démo est écrite ainsi.")))

# ---------------------------------------------------------------- 6
A(chapter(6, "Créer des sons", "Oscillateurs et échantillons, enveloppes, vibratos : la page des sons.", 6))
A(p("La page des sons (" + k('F2') + ") règle l'instrument choisi dans le compteur INSTR, avec " + k('Pg.préc') + " et " + k('Pg.suiv') + ", ou avec " + k('F11') + " et " + k('F12') + ". Elle s'ouvre sur l'instrument courant de la piste du curseur. Le piano y reste actif et joue l'instrument affiché, à plusieurs voix : on entend chaque réglage aussitôt."))
A(screen('tracker_son_psg.png', "L'instrument PSG 07 PWM NAPPE : un carré dont le rapport cyclique balaie en rebondissant. En bas, son enveloppe et son onde."))
A(h2('Deux familles de sons'))
A(table(['Type', 'Comment le son est fait', 'Pistes'], [
  ['PSG', "un oscillateur fabrique une onde simple : <b>carré</b> (au rapport cyclique réglable), <b>triangle</b>, <b>dent de scie</b> ou <b>bruit</b>. Sons nets d'arcade, basses, percussions synthétiques.", "1 à 8"],
  ['PCM', "un <b>échantillon</b> enregistré est rejoué à la hauteur de la note. Batterie, instruments acoustiques, chœurs.", "A à H"]]))
A(h2('Les réglages'))
A(p(k('↑') + ' ' + k('↓') + " choisissent un réglage, " + k('←') + ' ' + k('→') + " le changent (" + k('Maj') + " : par 10). À la souris, un clic sur une valeur ajoute 1, un clic droit retire 1, la molette fait varier. Le nom s'écrit après " + k('Entrée') + " sur NOM."))
A(table(['Réglage', 'Rôle'], [
  ['NOM', "dix caractères au plus"],
  ['TYPE', "PSG ou PCM (" + k('Entrée') + " pour changer)"],
  ['ONDE / ÉCHANTILLON', "PSG : carré, triangle, dent de scie ou bruit ; PCM : un des 21 échantillons de la banque"],
  ['RAPPORT / BOUCLE', "PSG : rapport cyclique du carré, de 1 à 255 (128 = carré parfait, plus petit = son plus fin) ; PCM : la boucle de l'échantillon, s'il en a une"],
  ['BALAYAGE, REBOND', "PSG : ajoute cette valeur au rapport cyclique à chaque tick (effet PWM) ; avec le rebond, il repart dans l'autre sens aux bornes"],
  ['VOLUME', "volume de l'instrument, 0 à 64"],
  ['PANORAMIQUE', "position dans la stéréo, du centre vers la gauche ou la droite"],
  ['ATTAQUE, DÉCLIN, MAINTIEN, RELÂCHE', "l'enveloppe du volume (voir la figure ci-dessous)"],
  ['VIB VITESSE, VIB AMPLEUR, VIB DÉLAI', "vibrato automatique : vitesse, ampleur en 1/16 de demi-ton, et ticks d'attente avant qu'il commence"],
  ['ARP +1, ARP +2, ARP VITESSE', "arpège automatique : deux notes ajoutées, en demi-tons, et ticks par note (0 = sans arpège)"],
  ['GLISSEMENT', "la hauteur glisse de cette valeur, en 1/16 de demi-ton, à chaque tick : négatif pour une grosse caisse"],
  ['ACCORD FIN', "décale la hauteur de 1/64 de demi-ton par pas"],
  ['TRANSPOSE', "demi-tons ajoutés à chaque note"],
  ['HAUTEUR FIXE', "l'instrument joue toujours la même note (do 4, plus la transposition), quelle que soit la note écrite : pratique pour la batterie"]]))
A(FIG_ADSR)
A(p("Avec un <b>maintien</b> à 0, le son s'éteint seul à la fin du déclin : c'est un son percussif, une note pincée. Avec un maintien élevé, la note tient jusqu'à la note suivante ou à une note coupée " + c('===') + ", puis s'éteint pendant le relâché. Le volume final d'une note est le produit de l'enveloppe, du volume de l'instrument, du volume de la case et du volume général."))
A(screen('tracker_son_pcm.png', "L'instrument PCM 19 CORDES : l'échantillon de cordes de la banque, avec sa boucle (le trait pointillé) et une attaque lente."))
A(h2('La banque d\'échantillons'))
A(p("Les 21 échantillons sont des sons originaux, synthétisés pour la MONOCORE-1. Les sons tenus ont une boucle sans raccord audible : ils durent aussi longtemps que l'enveloppe le demande."))
A(table(['N°', 'Nom', 'Genre', 'N°', 'Nom', 'Genre'], [
  ['00', 'KICK', 'grosse caisse', '11', 'MARIMBA', 'marimba, frappé'],
  ['01', 'SNARE', 'caisse claire', '12', 'CLOCHE', 'cloche, résonnante'],
  ['02', 'CLAP', 'claquement de mains', '13', 'GUITARE', 'corde pincée'],
  ['03', 'HH FERME', 'charleston fermé', '14', 'CUIVRES', 'cuivres, en boucle'],
  ['04', 'HH OUVERT', 'charleston ouvert', '15', 'CORDES', 'ensemble de cordes, en boucle'],
  ['05', 'CRASH', 'cymbale', '16', 'ORGUE', 'orgue, en boucle'],
  ['06', 'TOM', 'tom', '17', 'CHOEUR', 'chœur « aah », en boucle'],
  ['07', 'RIM', 'bord de caisse', '18', 'FLUTE', 'flûte, en boucle'],
  ['08', 'COWBELL', 'cloche de vache', '19', 'NAPPE', 'nappe de synthé, en boucle'],
  ['09', 'BASSE', 'basse de synthé, en boucle', '20', 'ORCH HIT', 'coup d\'orchestre'],
  ['10', 'PIANO EL', 'piano électrique', '', '', '']]))
A(h2('Recettes'))
A(table(['Son', 'Réglages'], [
  ['Grosse caisse PSG', "triangle, déclin 6, maintien 0, glissement −60, hauteur fixe, transposition −12"],
  ['Caisse claire PSG', "bruit, déclin 7, maintien 0, hauteur fixe, transposition +20"],
  ['Charleston PSG', "bruit, déclin 2, maintien 0, hauteur fixe, transposition +36, volume 34"],
  ['Lead d\'arcade', "carré au rapport 64, attaque 1, déclin 12, maintien 40, relâché 6, vibrato 6/3 après 14 ticks"],
  ['Nappe PWM', "carré, balayage +3 avec rebond, attaque 8, déclin 20, maintien 40, relâché 12"],
  ['Accord arpégé', "carré, ARP +1 = 4, ARP +2 = 7, vitesse 1 : un accord majeur sur chaque note"],
  ['Basse pincée', "triangle, déclin 10, maintien 48, relâché 3 ; ou l'échantillon BASSE en PCM"]]))
A(p("Ces recettes, sauf l'accord arpégé, sont celles des instruments 01 à 09 du morceau de démonstration, que reprend chaque nouveau morceau. Les instruments 0A à 1E sont les échantillons de la banque, prêts à jouer."))
A(table(['Touches', 'Effet'], [
  [k('Ctrl', 'C') + ' ' + k('Ctrl', 'V'), "copie l'instrument, colle sur l'instrument affiché"],
  [k('Ctrl', 'X'), "copie l'instrument et le vide"]]))

# ---------------------------------------------------------------- 7
A(chapter(7, "Organiser le morceau", "L'ordre des motifs, le tempo, le nom et la boucle.", 7))
A(screen('tracker_morceau.png', "La page du morceau : l'ordre des six positions de la démo, les réglages, et la NVRAM avec le morceau sauvé dans l'emplacement 1."))
A(p("Un morceau est une liste de <b>positions</b>. Chacune joue un motif ; le même motif peut revenir plusieurs fois, comme un refrain. La page du morceau (" + k('F3') + ") a trois cadres, que " + k('Tab') + " parcourt : ORDRE, RÉGLAGES et NVRAM."))
A(h2('L\'ordre'))
A(table(['Touches', 'Effet'], [
  [k('↑') + ' ' + k('↓') + ' ' + k('Pg.préc') + ' ' + k('Pg.suiv'), "choisit une position"],
  [k('←') + ' ' + k('→'), "change le motif de la position (" + k('Maj') + " : par 10)"],
  [k('Inser'), "ajoute une position après celle-ci, avec le même motif"],
  [k('Suppr'), "retire la position"],
  [k('Entrée'), "ouvre ce motif sur la page des motifs"]]))
A(p("La position de reprise est écrite en bleu ciel : après la dernière position, la lecture repart d'elle. Une introduction jouée une seule fois se met donc avant la reprise."))
A(h2('Les réglages'))
A(table(['Réglage', 'Rôle'], [
  ['NOM', "seize caractères au plus"],
  ['BPM', "le tempo, de 32 à 255 : à 125 BPM, la console joue 50 ticks par seconde"],
  ['VITESSE', "les ticks par ligne, de 1 à 31 ; à 6, une ligne dure une double croche et quatre lignes un temps"],
  ['VOLUME', "le volume général, de 0 à 64"],
  ['VELOCITE', "OUI : avec un clavier MIDI, la force de frappe s'écrit dans la colonne du volume (chapitre 9)"],
  ['REPRISE', "la position rejouée après la dernière"],
  ['CLAVIER', "AZERTY ou QWERTY : la disposition utilisée pour les lettres (noms, chiffres A à F, raccourcis). Le piano, lui, dépend de la position des touches et ne change pas."]]))
A(box('know', 'Le calcul du tempo', p("Une ligne dure <i>vitesse × 2,5 / BPM</i> secondes. À 132 BPM et vitesse 6, comme la démo : 6 × 2,5 / 132 = 0,114 s par ligne, soit 132 temps par minute si un temps fait quatre lignes. Les effets " + c('Fxx') + " changent la vitesse et le BPM pendant le morceau.")))

# ---------------------------------------------------------------- 8
A(chapter(8, "Sauvegarder et échanger", "La NVRAM, la carte SD de la MiSTer, et les allers-retours avec le simulateur.", 8))
A(p("Les morceaux se rangent dans la <b>NVRAM</b>, la mémoire sauvegardée de 32 Ko, en huit emplacements ; un morceau occupe de un à six Ko et l'espace libre s'affiche en vert."))
A(table(['Raccourci', 'Bouton', 'Effet'], [
  [k('Ctrl', 'S'), 'SAUVER', "sauve dans l'emplacement choisi ; ailleurs que sur la page du morceau, dans l'emplacement du morceau, ou le premier libre"],
  [k('Ctrl', 'L'), 'CHARGER', "charge l'emplacement choisi (aussi " + k('Entrée') + " dans le cadre NVRAM)"],
  [k('Ctrl', 'D'), 'EFFACER', "vide l'emplacement choisi"],
  [k('Ctrl', 'E'), 'NOUVEAU', "commence un morceau vide, avec les instruments de la démo"],
  [k('Ctrl', 'R'), 'DEMO', "recharge le morceau de démonstration"]]))
A(p("Ce qui ferait perdre quelque chose (charger par-dessus des modifications, effacer, remplacer) demande de refaire le même geste une seconde fois ; le bouton reste enfoncé en attendant."))
A(h2('Sur la MiSTer'))
A(p("La sauvegarde est le fichier " + c('saves/MONOCORE/monotracker.sav') + " de la carte SD. Après " + k('Ctrl', 'S') + ", ouvrez le menu du core : il écrit alors la NVRAM sur la carte. Au démarrage, MONOTRACKER rouvre le dernier emplacement chargé ou sauvé."))
A(h2('Dans le simulateur'))
A(p("La NVRAM du simulateur est gardée par le navigateur. Les raccourcis que le navigateur garde pour lui (" + k('Ctrl', 'N') + ", " + k('Ctrl', 'T') + ", " + k('Ctrl', 'W') + ") ne parviennent jamais au tracker : c'est pourquoi le nouveau morceau est sur " + k('Ctrl', 'E') + ". Sous l'écran, trois boutons font le pont avec la MiSTer et les jeux :"))
A(table(['Bouton', 'Effet'], [
  ['Exporter .sav', "télécharge la NVRAM sous le nom " + c('monotracker.sav') + ", à copier dans " + c('saves/MONOCORE') + " de la carte SD"],
  ['Importer .sav…', "charge un " + c('.sav') + " de la MiSTer et redémarre la ROM : on reprend le travail là où on l'avait laissé"],
  ['Morceaux .mts', "liste les morceaux sauvés ; un clic télécharge le fichier " + c('.mts') + " d'un morceau, prêt pour un jeu"]]))

# ---------------------------------------------------------------- 9
A(chapter(9, "Le clavier MIDI", "Jouer et écrire sur un vrai clavier, régler les sons aux potentiomètres.", 9))
A(p("Un clavier MIDI USB, comme un Novation Launchkey, remplace avantageusement le piano des touches de lettres : il a de vraies touches, toute l'étendue des octaves, la force de frappe et des potentiomètres. MONOTRACKER le prend dès qu'il reçoit une première note ; le bandeau affiche alors <i>CLAVIER MIDI BRANCHÉ</i>."))
A(h2('Le brancher'))
A(table(['Machine', 'Comment'], [
  ['MiSTer', "branchez le clavier sur un port USB de la MiSTer. Dans le menu du core MONOCORE (" + k('F12') + "), réglez le <b>mode UART</b> sur <b>MIDI</b>, connexion <b>USB</b> : MidiLink, le service MIDI de la MiSTer, transmet alors le clavier au core. Il faut le core en version 4."],
  ['Simulateur', "branchez le clavier sur le PC et cochez <b>clavier MIDI</b> sous l'écran, dans l'encadré « Système et manettes ». Le navigateur demande l'autorisation la première fois ; le nom du clavier s'affiche à côté de la case. Chrome et Edge le permettent ; Firefox seulement si la page vient d'un serveur (encadré ci-dessous) ; Safari non. La case reste cochée d'une visite à l'autre."]]))
A(box('tip', 'Avec Firefox', p("Firefox ne donne pas le MIDI à une page ouverte directement depuis un fichier. Lancez plutôt le simulateur par " + c('simulateur.bat') + " (Windows, double-clic) ou " + c('simulateur.py') + ", rangés à côté de " + c('monocore.html') + " dans " + c('dist/') + " : ils servent la page en local et l'ouvrent dans votre navigateur. Firefox demande alors l'autorisation, et le clavier MIDI fonctionne. Il faut Python, déjà nécessaire pour construire les ROM.")))
A(h2('Jouer et écrire'))
A(table(['Page', 'Ce que font les touches du clavier MIDI'], [
  ['Motifs', "jouent la note sur la piste du curseur, avec l'instrument courant ; en mode édition, l'écrivent et descendent du pas, comme le piano des lettres. La force de frappe donne le volume de la case."],
  ['Sons', "jouent l'instrument affiché, à plusieurs voix : on essaie un son en jouant des accords."],
  ['Morceau', "jouent la note sur la piste du curseur, pour essayer une mélodie."]]))
A(p("Le do du milieu du clavier MIDI (note 60) est le " + c('C-4') + " du tracker ; toutes les octaves de " + c('C-0') + " à " + c('B-7') + " sont accessibles, sans passer par le compteur OCT. Le réglage <b>VELOCITE</b> de la page du morceau décide si la force de frappe s'écrit dans la colonne du volume : à NON, les notes s'écrivent sans volume, et la piste garde le sien."))
A(h2('Les potentiomètres'))
A(p("Sur la page des sons, les huit potentiomètres du clavier règlent huit réglages de l'instrument affiché, que l'on entend changer en jouant. Le bandeau indique chaque fois le réglage et sa nouvelle valeur."))
A(table(['Bouton', 'CC', 'Réglage', 'Étendue'], [
  ['1', 'CC 21', 'ATTAQUE', '0 à 64 ticks'],
  ['2', 'CC 22', 'DÉCLIN', '0 à 64 ticks'],
  ['3', 'CC 23', 'MAINTIEN', '0 à 64'],
  ['4', 'CC 24', 'RELÂCHE', '0 à 64 ticks'],
  ['5', 'CC 25', 'VOLUME', '0 à 64'],
  ['6', 'CC 26', 'VIB VITESSE', '0 à 32'],
  ['7', 'CC 27', 'VIB AMPLEUR', '0 à 32'],
  ['8', 'CC 28', 'RAPPORT (PSG) ou PANORAMIQUE (PCM)', '1 à 255, ou de gauche à droite']], 'ref'))
A(box('know', 'Si les potentiomètres ne font rien', p("MONOTRACKER attend les contrôleurs 21 à 28, ceux que les claviers Launchkey envoient d'origine. Si votre clavier en envoie d'autres, réglez-le pour que ses potentiomètres envoient ces numéros ; l'outil de configuration du fabricant le permet en général.")))

A(chapter(10, "La musique dans vos jeux", "Le lecteur de MONOTRACKER est une bibliothèque du SDK mc.", 9))
A(p("Un jeu écrit avec le SDK bas niveau <b>mc</b> joue les morceaux du tracker avec le module <b>mtrack</b>. Il lui faut deux fichiers : la banque d'échantillons " + c('mtbank.bin') + ", fournie avec le tracker, et le morceau " + c('.mts') + ", exporté depuis le simulateur ou depuis le " + c('.sav') + "."))
A(shell('''python3 tools/mtrack/sav2song.py monotracker.sav            # liste les 8 emplacements
python3 tools/mtrack/sav2song.py monotracker.sav 1 -o theme.mts   # emplacement 1 -> theme.mts''', 'exporter un morceau'))
A(listing('''name    = monjeu
library = mc
modules = mtrack
sources = main.c
data    = ../tracker/mtbank.bin theme.mts''', 'project.txt', lang='text'))
A(p("Le programme prépare le lecteur avec la banque, lance le morceau, et peut jouer des bruitages avec les instruments du morceau. Voici l'exemple complet " + c('examples/test_mtrack') + " :"))
src = open(os.path.join(REPO, 'examples/test_mtrack/main.c'), encoding='utf-8').read()
A(listing(src, 'examples/test_mtrack/main.c'))
A(table(['Fonction', 'Rôle'], [
  [c('mt_init(mtbank_file)'), "prépare le lecteur et ses 16 pistes, une fois"],
  [c('mt_play(morceau, position)'), "joue un morceau ; le lecteur tourne tout seul, sur un timer"],
  [c('mt_stop() / mt_pause(oui)'), "arrête ou met en pause"],
  [c('mt_sfx(piste, note, instrument, volume)'), "joue un bruitage avec un instrument du morceau ; le morceau reprend la piste ensuite"],
  [c('mt_reserve(piste, oui)'), "laisse une piste aux seuls bruitages"],
  [c('mt_order() / mt_row()'), "position et ligne jouées, pour caler le jeu sur la musique"],
  [c('mt_level(piste)'), "niveau d'une piste, pour faire danser l'écran au rythme"]], 'ref'))
A(p("La référence complète, avec le format des fichiers, est dans " + c('sdk/MTRACK.md') + "."))

# ---------------------------------------------------------------- appendices
A(chapter('A', "Toutes les touches", "Le clavier de MONOTRACKER, page par page.", 11))
A(h3('Partout'))
A(table(['Touches', 'Effet'], [
  [k('F1') + ' ' + k('F2') + ' ' + k('F3') + ' ' + k('F4'), "motifs, sons, morceau, aide"],
  [k('Entrée') + ' / ' + k('Maj', 'Entrée'), "joue le morceau / le motif en boucle, ou arrête"],
  [k('Échap'), "arrête tout"],
  [k('F5') + ' ' + k('F6'), "octave −/+"],
  [k('F7') + ' ' + k('F8'), "pas −/+"],
  [k('F11') + ' ' + k('F12') + ', ' + k('pavé −') + ' ' + k('pavé +'), "instrument courant −/+ (sur la MiSTer, " + k('F12') + " ouvre son menu : prenez " + k('pavé +') + ")"],
  [k('Ctrl', 'S'), "sauve dans la NVRAM"]], 'ref'))
A(h3('Motifs'))
A(table(['Touches', 'Effet'], [
  [k('Espace'), "mode édition"],
  ['piano', "deux rangées de lettres (figure du chapitre 4) ; " + k('&') + " : note coupée"],
  [k('0') + '-' + k('9') + ' ' + k('A') + '-' + k('F'), "instrument, volume, effet"],
  [k('Suppr') + ' / ' + k('Maj', 'Suppr'), "efface la colonne / la case"],
  [k('Inser') + ' / ' + k('Retour arrière'), "insère / retire une ligne"],
  [k('Tab') + ' / ' + k('Maj', 'Tab'), "piste suivante / précédente"],
  ['flèches, ' + k('Pg.préc') + ' ' + k('Pg.suiv') + ' ' + k('Origine') + ' ' + k('Fin'), "déplacements"],
  [k('Ctrl', '↑') + ' ' + k('Ctrl', '↓'), "position −/+"],
  [k('Ctrl', '←') + ' ' + k('Ctrl', '→'), "motif de la position −/+"],
  [k('Ctrl', 'C') + ' ' + k('Ctrl', 'X') + ' ' + k('Ctrl', 'V'), "piste : copier, couper, coller"],
  [k('Alt', 'C') + ' ' + k('Alt', 'X') + ' ' + k('Alt', 'V'), "motif : copier, couper, coller"],
  [k('Ctrl', 'M') + ' / ' + k('Ctrl', 'O') + ' / ' + k('Ctrl', 'F'), "muet / solo / suivi de la lecture"]], 'ref'))
A(h3('Sons'))
A(table(['Touches', 'Effet'], [
  [k('↑') + ' ' + k('↓'), "réglage"],
  [k('←') + ' ' + k('→') + ', avec ' + k('Maj'), "valeur −/+ 1, ou par 10"],
  [k('Pg.préc') + ' ' + k('Pg.suiv'), "instrument −/+"],
  [k('Entrée'), "sur NOM : écrire le nom (" + k('Entrée') + " ou " + k('Échap') + " pour finir) ; sur TYPE : PSG ou PCM"],
  ['piano', "écouter l'instrument, à plusieurs voix"],
  [k('Ctrl', 'C') + ' ' + k('Ctrl', 'V') + ' ' + k('Ctrl', 'X'), "copier, coller, vider l'instrument"]], 'ref'))
A(h3('Morceau'))
A(table(['Touches', 'Effet'], [
  [k('Tab'), "cadre suivant : ordre, réglages, NVRAM"],
  ['ordre', "flèches : position et motif ; " + k('Inser') + ' ' + k('Suppr') + " : ajouter, retirer ; " + k('Entrée') + " : éditer le motif"],
  ['réglages', "flèches : réglage et valeur ; " + k('Entrée') + " sur NOM : écrire le nom"],
  ['NVRAM', "flèches : emplacement ; " + k('Entrée') + " : charger"],
  [k('Ctrl', 'S') + ' ' + k('Ctrl', 'L') + ' ' + k('Ctrl', 'D'), "sauver, charger, effacer l'emplacement"],
  [k('Ctrl', 'E') + ' ' + k('Ctrl', 'R'), "nouveau morceau, recharger la démo"]], 'ref'))

A(chapter('B', "La souris", "Tous les gestes, zone par zone.", 12))
A(table(['Où', 'Clic', 'Clic droit', 'Molette'], [
  ['flèches ▲▼ des compteurs', "+1 / −1 (" + k('Maj') + " : ×10) ; maintenu, répète", "sens inverse", "sur la valeur : +/−"],
  ['boutons', "action", '', ''],
  ['vumètres', "choisit la piste", "rend la piste muette", "change de piste"],
  ['nom d\'une piste', "rend la piste muette", "solo", ''],
  ['case d\'un motif', "y place le curseur, sur la colonne visée", "fait entendre la note", "fait défiler les lignes"],
  ['numéros de ligne', '', '', "fait défiler les lignes"],
  ['réglage d\'un son', "sur le nom : le choisit ; sur la valeur : +1 (" + k('Maj') + " : +10), maintenu, répète", "−1", "+/−"],
  ['réglage du morceau', "+1, ou écrire le nom", "−1", "+/−"],
  ['ordre du morceau', "choisit la position ; sur le numéro de motif : +1", "−1", "fait défiler"],
  ['emplacement NVRAM', "le choisit", '', "change d'emplacement"],
  ['page d\'aide', "la referme", '', '']], 'ref'))
A(p("Le pointeur avance d'un pixel par pas de la souris. Sur la MiSTer, la souris USB de la carte est prise en charge par le core à partir de la version 3 du matériel ; dans le simulateur, le pointeur du tracker suit celui du navigateur sur l'écran."))

A('<section class="colophon">' + p("MONOTRACKER · Manuel de l'utilisateur · édition 2, octobre 2026.") +
  p("Titres en Press Start 2P (Cody Boisclair), texte en Archivo (Omnibus-Type), listings en IBM Plex Mono : polices sous licence SIL Open Font License.") +
  p("Les images d'écran ont été prises dans le simulateur avec " + c('manual/tools/tracker_shots.py') + ". Ce manuel se régénère avec " + c('python3 manual/tools/tracker_manual.py') + ".") + '</section>')

# ---------------------------------------------------------------- table of contents and PDF (as in manual.py)
toc = ['<section class="toc-page"><h2>Sommaire</h2><ol class="toc">']
for num, title in CHAPTERS:
    toc.append('<li><a href="#ch%s"><span class="tn">%s</span><span class="tt">%s</span><span class="tp"></span><span class="pg">@@PG%s@@</span></a></li>' % (num, num, title, num))
toc.append('</ol></section>')
body = '\n'.join(parts).replace('@@TOC@@', '\n'.join(toc))

EXTRA_CSS = '''
kbd { font: 600 8pt 'IBM Plex Mono', monospace; display: inline-block; padding: 0 1.4mm; border: 1px solid #2c2a25; border-bottom-width: 2px;
  border-radius: 1.2mm; background: #fbf6e8; white-space: nowrap; line-height: 1.35; }
.c10 .chap-num { background: var(--blue); } .c11 .chap-num { background: var(--red); } .c12 .chap-num { background: var(--orange); }
.cover-title { font-size: 34pt; }
'''
CSS = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'manual.css'), encoding='utf-8').read() + EXTRA_CSS
def page(content):
    return """<!doctype html><html lang="fr"><head><meta charset="utf-8"><title>MONOTRACKER — Manuel de l'utilisateur</title>
<style>%s</style></head><body>%s</body></html>""" % (CSS, content)

from playwright.sync_api import sync_playwright
FOOT = """<div style="width:100%;font-family:Archivo,sans-serif;font-size:8px;color:#777;padding:0 20mm;display:flex;justify-content:space-between;align-items:center">
<span style="letter-spacing:.15em">MONOTRACKER · MANUEL DE L'UTILISATEUR</span>
<span style="display:flex;gap:2px;align-items:center"><i style="display:inline-block;width:10px;height:3px;background:#e23b2e"></i><i style="display:inline-block;width:10px;height:3px;background:#f28c1b"></i><i style="display:inline-block;width:10px;height:3px;background:#f2c81b"></i><i style="display:inline-block;width:10px;height:3px;background:#3aa655"></i><i style="display:inline-block;width:10px;height:3px;background:#2f6fd6"></i>
<b style="margin-left:6px;color:#222;font-size:9px" class="pageNumber"></b></span></div>"""

def render(html_text, path, footer=True):
    open(OUT_HTML, 'w', encoding='utf-8').write(html_text)
    with sync_playwright() as pw:
        b = pw.chromium.launch()
        pg = b.new_page()
        pg.goto('file://' + OUT_HTML)
        pg.wait_for_timeout(500)
        pg.pdf(path=path, print_background=True, prefer_css_page_size=True, display_header_footer=footer,
               header_template='<div></div>', footer_template=FOOT)
        b.close()

from pypdf import PdfReader, PdfWriter
TMP_BODY = OUT_PDF + '.body.pdf'
TMP_COVER = OUT_PDF + '.cover.pdf'
render(page(body), TMP_BODY)
pages = {}
for i, pgo in enumerate(PdfReader(TMP_BODY).pages):
    for m in re.finditer(r'QQCH(\w+)QQ', pgo.extract_text() or ''):
        pages.setdefault(m.group(1), i + 1)
final = body
for num, _ in CHAPTERS:
    final = final.replace('@@PG%s@@' % num, str(pages.get(str(num), '?')))
final = re.sub(r'<span class="mark">QQCH\w+QQ</span>', '', final)
render(page(final), TMP_BODY)
render(page(COVER).replace('<body>', '<body class="cover-page">').replace('</head>', '<style>@page { margin: 0 }</style></head>'), TMP_COVER, footer=False)
w = PdfWriter()
for f in (TMP_COVER, TMP_BODY):
    for pgo in PdfReader(f).pages: w.add_page(pgo)
w.add_metadata({'/Title': "MONOTRACKER - Manuel de l'utilisateur", '/Author': 'MONOCORE-1', '/Subject': 'Composer avec le tracker de la MONOCORE-1'})
with open(OUT_PDF, 'wb') as fo: w.write(fo)
os.remove(TMP_BODY); os.remove(TMP_COVER)
open(OUT_HTML, 'w', encoding='utf-8').write(page(COVER + final))
print('PDF:', OUT_PDF, '-', len(PdfReader(OUT_PDF).pages), 'pages', pages)
