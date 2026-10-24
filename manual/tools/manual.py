#!/usr/bin/env python3
"""Builds the MONOCORE-1 programmer's manual: printable HTML, then PDF through Chromium.

usage: python3 manual/tools/manual.py      -> manual/MONOCORE-1_Manuel_du_programmeur.pdf
Listings are read from manual/examples/*/main.c, so the manual always shows the code that runs.
The prose is in French; code, file and setting names are in English.
"""
import html, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EX = os.path.join(ROOT, 'examples')
OUT_HTML = os.path.join(ROOT, 'manual.html')
OUT_PDF = os.path.join(ROOT, 'MONOCORE-1_Manuel_du_programmeur.pdf')

# ---------------------------------------------------------------- building blocks
def esc(s): return html.escape(s, quote=False)

KW = r'\b(int|void|static|const|char|for|while|if|else|return|break|continue|typedef|struct|uint8_t|uint16_t|uint32_t|int8_t|int16_t|volatile|extern|sizeof|unsigned|bool|size_t|true|false)\b'
def highlight(code):
    """C colouring: comments, strings, directives, keywords, numbers, constants"""
    out = []
    pat = re.compile(r'(//[^\n]*)|("(?:[^"\\]|\\.)*")|(^#\w+[^\n]*)|' + KW + r'|(\b\d+\b|\b0x[0-9A-Fa-f]+\b)|([A-Z][A-Z0-9_]{2,})', re.M)
    pos = 0
    for m in pat.finditer(code):
        out.append(esc(code[pos:m.start()]))
        t = esc(m.group(0))
        if m.group(1): out.append('<span class="c-com">%s</span>' % t)
        elif m.group(2): out.append('<span class="c-str">%s</span>' % t)
        elif m.group(3): out.append('<span class="c-pre">%s</span>' % t)
        elif m.group(4): out.append('<span class="c-kw">%s</span>' % t)
        elif m.group(5): out.append('<span class="c-num">%s</span>' % t)
        else: out.append('<span class="c-cst">%s</span>' % t)
        pos = m.end()
    out.append(esc(code[pos:]))
    return ''.join(out)

def listing(code, caption=None, numbers=True, lang='c'):
    code = code.strip('\n')
    body = highlight(code) if lang == 'c' else esc(code)
    rows = []
    for i, l in enumerate(body.split('\n')):
        num = '<span class="ln">%3d</span>' % (i + 1) if numbers else ''
        rows.append(num + (l if l else ' '))
    cap = '<div class="lst-cap"><span>LISTING</span> %s</div>' % esc(caption) if caption else ''
    return '<div class="listing %s">%s<pre>%s</pre></div>' % ('' if numbers else 'nonum', cap, '\n'.join(rows))

def shell(code, caption=None):
    cap = '<div class="lst-cap sh"><span>TERMINAL</span> %s</div>' % esc(caption) if caption else ''
    return '<div class="terminal">%s<pre>%s</pre></div>' % (cap, esc(code.strip('\n')))

def source(folder, name='main.c'):
    return open(os.path.join(EX, folder, name), encoding='utf-8').read()

def screen(img, caption, small=False):
    return ('<figure class="tv%s"><div class="bezel"><img src="images/%s"></div>'
            '<figcaption>%s</figcaption></figure>') % (' small' if small else '', img, caption)

def box(kind, title, text):
    return '<div class="box %s"><div class="box-t">%s</div>%s</div>' % (kind, title, text)

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

# ---------------------------------------------------------------- figures
FIG_LAYERS = '''
<figure class="fig"><svg viewBox="0 0 560 250" xmlns="http://www.w3.org/2000/svg" font-family="Archivo" font-size="13">
 <defs><linearGradient id="sk" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#0b0b30"/><stop offset="1" stop-color="#3a3f9a"/></linearGradient></defs>
 <g transform="translate(40,150) skewX(-30)"><rect x="0" y="0" width="190" height="70" fill="#111" stroke="#222"/></g>
 <g transform="translate(40,105) skewX(-30)">
  <rect x="0" y="0" width="190" height="70" fill="url(#sk)" stroke="#223" opacity=".95"/>
  <path d="M0 50 L30 35 L60 48 L95 30 L130 46 L160 33 L190 45 L190 70 L0 70Z" fill="#5a4f8a"/>
  <rect x="0" y="62" width="190" height="8" fill="#3cb85a"/>
 </g>
 <g transform="translate(40,60) skewX(-30)">
  <rect x="0" y="0" width="190" height="70" fill="none" stroke="#999" stroke-dasharray="4 3"/>
  <polygon points="90,22 96,34 108,35 99,43 102,55 90,48 78,55 81,43 72,35 84,34" fill="#ffd43b" stroke="#222"/>
  <polygon points="140,40 150,58 130,58" fill="#3f7cff" stroke="#222"/>
 </g>
 <g transform="translate(40,15) skewX(-30)">
  <rect x="0" y="0" width="190" height="70" fill="none" stroke="#999" stroke-dasharray="4 3"/>
  <text x="18" y="22" fill="#e8b400" font-family="IBM Plex Mono" font-size="11" font-weight="600">SCORE 000120</text>
 </g>
 <g font-weight="600" fill="#222">
  <text x="300" y="45">4. le texte (plan 0)</text><text x="300" y="61" font-weight="400" fill="#555">toujours devant</text>
  <text x="300" y="92">3. les sprites</text><text x="300" y="108" font-weight="400" fill="#555">64 objets mobiles de 16 x 16 à 64 x 64</text>
  <text x="300" y="139">2. le décor (plan 1)</text><text x="300" y="155" font-weight="400" fill="#555">tuiles de 16 x 16, il défile</text>
  <text x="300" y="186">1. la couleur de fond</text><text x="300" y="202" font-weight="400" fill="#555">là où rien n'est dessiné</text>
 </g>
 <g stroke="#bbb"><line x1="268" y1="40" x2="295" y2="40"/><line x1="268" y1="87" x2="295" y2="87"/><line x1="268" y1="134" x2="295" y2="134"/><line x1="268" y1="181" x2="295" y2="181"/></g>
</svg><figcaption>L'image est un empilement : ce qui est dessiné au-dessus cache ce qui est dessous.</figcaption></figure>'''

FIG_SCREEN = '''
<figure class="fig"><svg viewBox="0 0 520 300" xmlns="http://www.w3.org/2000/svg" font-family="IBM Plex Mono" font-size="11">
 <rect x="60" y="30" width="400" height="240" fill="#0d1240" stroke="#222" stroke-width="2"/>
 <g stroke="#2b3270" stroke-width="1">
 ''' + ''.join('<line x1="%d" y1="30" x2="%d" y2="270"/>' % (60 + i * 10, 60 + i * 10) for i in range(1, 40)) + \
 ''.join('<line x1="60" y1="%d" x2="460" y2="%d"/>' % (30 + j * 8, 30 + j * 8) for j in range(1, 30)) + '''
 </g>
 <rect x="60" y="30" width="10" height="8" fill="#ffd43b"/><rect x="450" y="262" width="10" height="8" fill="#ffd43b"/>
 <text x="60" y="22" fill="#222">col 0</text><text x="420" y="22" fill="#222">col 39</text>
 <text x="4" y="37" fill="#222">row 0</text><text x="4" y="270" fill="#222">row 29</text>
 <text x="150" y="292" fill="#555">x de 0 à 319 pixels, y de 0 à 239 pixels</text>
 <rect x="280" y="150" width="20" height="16" fill="#3f7cff" stroke="#fff"/><text x="272" y="162" fill="#fff" text-anchor="end">sprite à x = 176, y = 120</text>
</svg><figcaption>Le texte se place en colonnes et lignes (une case = 8 x 8 pixels), les sprites au pixel près.</figcaption></figure>'''

FIG_JOYPAD = '''
<figure class="fig"><svg viewBox="0 0 520 210" xmlns="http://www.w3.org/2000/svg" font-family="Archivo" font-size="13">
 <rect x="20" y="40" width="480" height="140" rx="60" fill="#2a2a33"/>
 <g fill="#55555f"><rect x="80" y="95" width="70" height="26" rx="4"/><rect x="102" y="73" width="26" height="70" rx="4"/></g>
 <g fill="#d33"><circle cx="340" cy="90" r="15"/><circle cx="380" cy="90" r="15"/><circle cx="420" cy="90" r="15"/></g>
 <g fill="#e8b400"><circle cx="340" cy="132" r="15"/><circle cx="380" cy="132" r="15"/><circle cx="420" cy="132" r="15"/></g>
 <g fill="#888"><rect x="210" y="130" width="34" height="12" rx="6"/><rect x="262" y="130" width="34" height="12" rx="6"/></g>
 <g fill="#fff" font-weight="700" font-size="11" text-anchor="middle">
  <text x="340" y="94">B1</text><text x="380" y="94">B2</text><text x="420" y="94">B3</text>
  <text x="340" y="136">B4</text><text x="380" y="136">B5</text><text x="420" y="136">B6</text>
 </g>
 <g fill="#222" font-weight="600" text-anchor="middle" font-family="IBM Plex Mono">
  <text x="115" y="28">UP DOWN LEFT RIGHT</text><text x="227" y="165">COIN</text><text x="279" y="165">START</text>
  <text x="380" y="28">B1 ... B6</text>
 </g>
</svg><figcaption>Les noms des commandes, tels qu'on les écrit dans un programme.</figcaption></figure>'''

# ---------------------------------------------------------------- content
parts = []
A = parts.append

# cover (rendered separately, without page footer)
COVER = '''<section class="cover">
 <div class="cover-stripes"><i></i><i></i><i></i><i></i><i></i></div>
 <div class="cover-top">CONSOLE VIRTUELLE POUR MiSTer · DE10-NANO</div>
 <div class="cover-title">MONOCORE<span>-1</span></div>
 <div class="cover-sub">MANUEL DU PROGRAMMEUR</div>
 <div class="cover-shot"><img src="images/screen_07_title.png"></div>
 <div class="cover-bottom">Écrivez vos propres jeux · Tutoriels pas à pas · Référence complète</div>
 <div class="cover-ed">ÉDITION 3 · OCTOBRE 2026</div>
</section>'''

A('@@TOC@@')

A('<section class="foreword">' + h2('Avant-propos') +
p("Bienvenue ! Vous avez entre les mains le manuel de la <b>MONOCORE-1</b>, une console de jeu qui n'a jamais existé dans le commerce : elle vit dans le circuit programmable (FPGA) de votre MiSTer. Elle a été conçue pour accueillir des adaptations de jeux d'arcade des années 1978 à 1995, mais aussi, et surtout, <b>vos propres jeux</b>.") +
p("Comme les ordinateurs familiaux de notre enfance, elle s'allume et attend qu'on lui dise quoi faire. Ce manuel vous apprend à le lui dire. <b>Aucune connaissance préalable n'est nécessaire</b> : nous partirons d'un écran vide pour arriver, au chapitre 9, à un jeu complet avec un titre, un score, des vies, de la musique et des explosions.") +
p("Chaque exemple de ce manuel est un vrai programme, fourni dans le dossier <code>manual/examples</code>. Les images d'écran ont été prises en le faisant tourner : ce que vous voyez est ce que vous obtiendrez.") +
box('know', 'Une langue pour le code', p("Comme dans presque tous les langages de programmation, les noms utilisés dans le code (fonctions, variables, fichiers, réglages) sont en <b>anglais</b> : <code>print</code> pour écrire, <code>wait_frame</code> pour attendre l'image suivante… Chaque nom est expliqué la première fois qu'il apparaît, et l'annexe A les traduit tous. Les textes affichés à l'écran, eux, sont en français.")) +
box('tip', 'Comment lire ce manuel', ul([
  "Les chapitres 1 à 3 installent l'atelier et font tourner un premier programme. Lisez-les dans l'ordre.",
  "Les chapitres 4 à 8 présentent une notion chacun : le langage C, la manette, les sprites, le son, le décor.",
  "Le chapitre 9 assemble tout dans un jeu. Le chapitre 10 ouvre le capot pour les curieux.",
  "Les chapitres 11 et 12 présentent le SDK bas niveau mc, pour les jeux plus ambitieux, avec un shoot'em up complet.",
  "Les annexes servent de référence : gardez-les sous la main pendant que vous programmez."])) +
p("Bon courage, et amusez-vous bien !") + '</section>')

# ---------------------------------------------------------------- 1
A(chapter(1, "La MONOCORE-1 en un coup d'oeil", "Ce qu'il y a dans la boîte, et comment l'image et le son sont fabriqués.", 1))
A(p("La MONOCORE-1 est une console complète : un processeur, une mémoire, un circuit vidéo, un circuit sonore, et jusqu'à quatre manettes et un clavier. Un programme de jeu, une <b>ROM</b>, est un fichier <code>.mnc</code> que l'on charge depuis le menu du MiSTer ou que l'on glisse dans le simulateur."))
A(table(['Élément', 'Caractéristiques'], [
  ['Processeur', "RISC-V 32 bits à 50 MHz, environ 47 millions d'instructions par seconde"],
  ['Mémoire', "128 Ko de mémoire de travail, 32 Ko de mémoire sauvegardée, ROM jusqu'à 32 Mo"],
  ['Image', "320 x 240 pixels par défaut (de 224 x 192 à 384 x 288), 60 images par seconde, 4096 couleurs à l'écran parmi 32 768"],
  ['Plans', '4 plans de tuiles qui défilent, chacun dans sa direction'],
  ['Sprites', "512 objets mobiles, agrandis ou réduits à volonté, retournés, jusqu'à 64 par ligne"],
  ['Son', "8 voix d'échantillons (dont le format ADPCM des bornes d'arcade), 8 générateurs de notes et de bruit, stéréo 48 kHz"],
  ['Commandes', "4 manettes : 4 directions, 6 boutons, START et COIN (pièce), sticks analogiques, paddles et spinners ; un clavier USB"],
  ['Cartouche', "sur la MiSTer, une ou deux mémoires Flash de 8 Mo branchées sur le port USER"],
], 'specs'))
A(h2("Comment se forme l'image"))
A(p("L'écran est construit 60 fois par seconde, ligne par ligne, en superposant plusieurs couches. Avec la bibliothèque <b>easy</b> (« facile ») que nous utiliserons dans ce manuel, il y en a quatre :"))
A(FIG_LAYERS)
A(p("Pour placer les choses à l'écran, on utilise deux façons de compter. Le texte se range dans une grille de cases de 8 x 8 pixels : 40 colonnes et 30 lignes. Les sprites, eux, se placent au pixel près : x va de 0 (à gauche) à 319 (à droite), y de 0 (en haut) à 239 (en bas). Attention, y grandit vers le <b>bas</b>, comme sur toutes les consoles."))
A(FIG_SCREEN)
A(box('know', 'Le saviez-vous ?', p("Une image de la MONOCORE-1 compte 264 lignes, dont 240 visibles. Pendant les 24 lignes invisibles, la <b>pause verticale</b>, rien n'est dessiné : c'est le bon moment pour préparer l'image suivante. La fonction <code>wait_frame()</code> (« attendre l'image ») attend précisément ce moment.")))

# ---------------------------------------------------------------- 2
A(chapter(2, "Installer l'atelier", "Trois outils gratuits, un simulateur, et la console est prête à recevoir vos programmes.", 2))
A(p("Un programme MONOCORE-1 s'écrit en <b>langage C</b> dans un simple éditeur de texte, puis il est transformé en ROM par l'outil <code>build.py</code> (« construire »). Il faut pour cela :"))
A(ol(["<b>LLVM</b> (le compilateur <code>clang</code> et ses amis) : il sait produire du code pour le processeur RISC-V de la console ;",
      "<b>Python 3</b> avec la bibliothèque <b>Pillow</b>, pour convertir vos dessins et vos sons ;",
      "un <b>éditeur de texte</b> : n'importe lequel convient (Bloc-notes, TextEdit, Geany, VS Code…)."]))
A(h3('Sous Linux (Debian, Ubuntu, Raspberry Pi OS…)'))
A(shell("sudo apt install clang lld llvm python3 python3-pil"))
A(h3('Sous macOS'))
A(shell("brew install llvm lld python\npip3 install pillow\nexport PATH=\"$(brew --prefix llvm)/bin:$PATH\""))
A(p("La dernière ligne ajoute les outils LLVM au chemin de recherche des commandes ; ajoutez-la à votre fichier <code>~/.zshrc</code> pour ne pas avoir à la retaper."))
A(h3('Sous Windows'))
A(p("Le plus simple est d'installer <b>WSL</b> (le Linux intégré à Windows : dans une invite de commandes, tapez <code>wsl --install</code>, redémarrez) puis de suivre les instructions Linux ci-dessus. On peut aussi installer LLVM (llvm.org) et Python (python.org) directement, puis <code>pip install pillow</code>."))
A(p("Dans WSL, les disques de Windows se trouvent sous <code>/mnt</code> : <code>D:</code> devient <code>/mnt/d</code>, et les chemins s'écrivent avec des <code>/</code>. Ne recopiez pas un chemin Windows tel quel : bash prend les <code>\\</code> pour des caractères spéciaux et les supprime. Pour un programme rangé dans <code>D:\\Mes_Documents\\Jeux\\01_myhello</code> :"))
A(shell("cd /mnt/d/Mes_Documents/Jeux/01_myhello"))
A(p("Si ce programme n'est pas rangé dans le dossier de la console (voir ci-dessous), appelez <code>build.py</code> par son chemin complet. Avec le dossier de la console dans <code>~/monocore</code>, comme ci-dessous, la commande ne crée rien d'autre que la ROM :"))
A(shell("python3 ~/monocore/sdk/build.py"))
A(p("La ROM est créée dans le sous-dossier <code>build</code> du programme, visible directement depuis Windows. La commande <code>wslpath 'D:\\Mes_Documents'</code> convertit un chemin Windows en chemin WSL (gardez les apostrophes), et la touche <b>Tab</b> complète les noms de dossiers pendant la frappe."))
A(h3("Vérifier que tout est prêt"))
A(shell("clang -print-targets | grep riscv32\npython3 -c \"import PIL; print('Pillow OK')\"", "deux lignes doivent s'afficher"))
A(h2('Le dossier de la console'))
A(p("Le projet MONOCORE-1 est un dossier, que l'on récupère une fois pour toutes avec <b>git</b> (<code>sudo apt install git</code> s'il manque). La commande suivante crée le dossier <code>monocore</code> dans votre dossier personnel, que bash note <code>~</code> :"))
A(shell("cd ~\ngit clone https://github.com/jadiema/monocore.git"))
A(p("Plus tard, <code>git pull</code> lancé dans ce dossier le met à jour. Dans la suite du manuel, le <b>dossier de la console</b> est ce dossier <code>~/monocore</code>. Il contient, entre autres :"))
A(table(['Dossier', 'Contenu'], [
  [c('sdk/'), "le kit de développement : <code>easy.h</code>, <code>monocore.h</code>, l'outil <code>build.py</code>, la police de caractères"],
  [c('manual/examples/'), "tous les programmes de ce manuel, prêts à construire"],
  [c('dist/monocore.html'), "le simulateur : une page web qui fait tourner les ROM sur votre ordinateur"],
  [c('mister/'), "le core pour le MiSTer"]]))
A(h2('Le simulateur'))
A(p("Ouvrez <code>dist/monocore.html</code> dans votre navigateur (Firefox, Chrome, Edge ou Safari). Cliquez sur <b>Mettre sous tension</b> : la ROM de test démarre. Pour essayer vos programmes, cliquez sur <b>Charger une ROM…</b> ou glissez un fichier <code>.mnc</code> sur l'écran. Le simulateur se joue au clavier ou avec une manette USB :"))
A(table(['Commande', 'Joueur 1', 'Joueur 2', 'Joueur 3', 'Joueur 4'], [
  ['Directions', 'flèches', 'J I K L', 'E R F G', 'pavé 4 8 5 6'], ['Boutons 1 à 3', 'Z X C', 'N M ,', 'Q W V', 'pavé 0 . Entrée'], ['Boutons 4 à 6', 'A S D', 'H U O', '—', '—'],
  ['START', '1 ou Entrée', '2', '3', '4 ou pavé +'], ['COIN (pièce)', '5', '6', '—', '—']]))
A(p("Les directions se lisent gauche, haut, bas, droite. Les lettres sont celles marquées sur votre clavier, qu'il soit QWERTY ou AZERTY. Jusqu'à quatre manettes USB ou Bluetooth sont reconnues, une par joueur, sticks analogiques compris. La case <b>clavier de la console</b> (touche F9) envoie le clavier au jeu au lieu des joueurs, et le bouton <b>16:9</b> étire l'image comme une MiSTer en plein écran sur un écran large."))
A(p("Sur le côté, le <b>moniteur</b> montre la charge du processeur, le nombre de sprites par ligne, les voix sonores actives, et une <b>console de debug</b> qui affiche ce que votre programme écrit dans <code>DEBUG_OUT</code>, bien pratique pour chercher une erreur."))
A(h2('La console MiSTer'))
A(p("Pour jouer sur la vraie console, copiez le core <code>MONOCORE.rbf</code> dans le dossier <code>_Console</code> de la carte SD du MiSTer, et vos ROM dans <code>games/MONOCORE</code>. Dans le menu du core (touche F12), choisissez <b>Charger une ROM</b>. Une ROM nommée <code>boot.rom</code> dans ce dossier se lance toute seule : c'est là que l'on place l'écran d'accueil de la console, AUBE (<code>examples/aube</code>)."))
A(p("Une ROM peut aussi venir d'une <b>cartouche</b> : une ou deux mémoires Flash SPI W25Q64 (16 Mo au total) branchées sur le port USER du MiSTer, qui contiennent le fichier <code>.mnc</code> tel quel. L'outil <code>tools/cart/cart_split.py</code> prépare les images à graver, et <code>mister/LISEZMOI.md</code> donne le câblage et les réglages du menu. Pour le jeu, rien ne change : la ROM est copiée dans la SDRAM comme si elle venait de la carte SD."))
A(box('warn', 'Attention', p("Le MiSTer doit être équipé d'une <b>barrette SDRAM</b> : la ROM du jeu est chargée dedans.")))

# ---------------------------------------------------------------- 3
A(chapter(3, 'Premier programme : BONJOUR', "Six lignes de C, une commande, et la console vous salue.", 3))
A(p("Tout programme vit dans son propre dossier. Celui-ci s'appelle <code>01_hello</code> et contient deux fichiers. Le premier, <code>project.txt</code>, décrit le projet : le nom du fichier à produire (<code>name</code>), le titre inscrit dans la ROM (<code>title</code>) et les fichiers du programme (<code>sources</code>)."))
A(listing(source('01_hello', 'project.txt'), 'project.txt', numbers=False, lang='txt'))
A(p("Le second, <code>main.c</code>, est le programme lui-même :"))
A(listing(source('01_hello'), 'main.c'))
A(h2('Construire la ROM'))
A(p("Ouvrez un terminal, placez-vous dans le dossier du programme, et lancez <code>build.py</code> en indiquant où il se trouve :"))
A(shell("cd ~/monocore/manual/examples/01_hello\npython3 ../../../sdk/build.py\n\nROM ready: /home/.../monocore/manual/examples/01_hello/build/hello.mnc"))
A(p("<code>../../../</code> remonte de trois dossiers, de <code>01_hello</code> à <code>monocore</code> : <code>../../../sdk/build.py</code> est donc <code>~/monocore/sdk/build.py</code>, et l'on peut écrire l'un ou l'autre. Le chemin complet a l'avantage de marcher depuis n'importe quel dossier, y compris pour vos propres programmes rangés ailleurs. <code>build.py</code> crée le sous-dossier <code>build</code> dans le dossier du programme et y range la ROM."))
A(p("Le fichier <code>build/hello.mnc</code> est votre ROM. Chargez-la dans le simulateur :"))
A(screen('screen_01.png', 'BONJOUR en 320 x 240 : le texte blanc sur le fond bleu.'))
A(h2('Ligne par ligne'))
A(table(['Ligne', "Ce qu'elle fait"], [
  ['1', "<code>#include \"easy.h\"</code> donne accès à la bibliothèque easy : toutes les fonctions de ce manuel."],
  ['3', "<code>int main(void)</code> : c'est ici que le programme commence. Tout ce qui est entre les accolades <code>{ }</code> s'exécute dans l'ordre."],
  ['4', "<code>init()</code> (« initialiser ») prépare la console : police, couleurs, écran allumé. À appeler une fois, en premier."],
  ['5', "<code>background(0, 0, 120)</code> choisit la couleur du fond : rouge 0, vert 0, bleu 120 (sur 255)."],
  ['6 et 7', "<code>print(colonne, ligne, \"texte\")</code> (« imprimer ») écrit un texte dans la grille de 40 x 30 cases."],
  ['8', "<code>for (;;)</code> répète sans fin ce qui suit. Un programme de console ne s'arrête jamais : ici, il attend image après image avec <code>wait_frame()</code>."]]))
A(box('tip', 'À vous de jouer', ul([
  "Changez le texte, sa position, la couleur du fond, puis reconstruisez la ROM.",
  "Les accents sont acceptés mais affichés sans accent : la police est celle des années 80, en majuscules de préférence.",
  "Une erreur ? <code>build.py</code> indique le fichier et la ligne. Le plus souvent, il manque un point-virgule <code>;</code> en fin de ligne."])))

# ---------------------------------------------------------------- 4
A(chapter(4, 'Petit cours de C', "Variables, conditions et boucles : de quoi donner vie à l'écran.", 4))
A(p("Le C est le langage dans lequel ont été écrits une grande partie des jeux et des systèmes depuis les années 80. Nous n'en utiliserons qu'une petite partie, présentée ici sur un exemple qui affiche les huit encres de texte, compte les secondes et fait clignoter un message."))
A(listing(source('02_colors'), '02_colors/main.c'))
A(screen('screen_02.png', 'Les huit encres, le compteur de secondes et le message qui clignote.'))
A(h2('Les variables'))
A(p("Une <b>variable</b> est une case de mémoire qui porte un nom et contient un nombre. <code>int seconds = 0;</code> crée la variable <code>seconds</code> (« secondes ») et y range 0. Ensuite, <code>seconds = seconds + 1;</code> lit la valeur, ajoute 1 et range le résultat. <code>int</code> veut dire « nombre entier » (de -2 milliards à +2 milliards)."))
A(p("La bibliothèque fournit une variable toute prête : <code>frame_count</code> (« nombre d'images »), qui compte les images depuis le démarrage. Comme il y en a 60 par seconde, <code>frame_count % 60 == 0</code> (le reste de la division par 60 vaut zéro) est vrai une fois par seconde."))
A(h2('Les conditions'))
A(p("<code>if (condition) action;</code> n'exécute l'action que si la condition est vraie. On peut ajouter <code>else autre_action;</code> pour le cas contraire. Les comparaisons s'écrivent <code>==</code> (égal, avec deux signes !), <code>!=</code> (différent), <code>&lt;</code>, <code>&gt;</code>, <code>&lt;=</code>, <code>&gt;=</code>. On combine avec <code>&amp;&amp;</code> (et) et <code>||</code> (ou)."))
A(h2('Les boucles'))
A(p("<code>for (int n = 0; n &lt; 8; n++)</code> répète ce qui suit en faisant prendre à <code>n</code> les valeurs 0, 1, 2… jusqu'à 7. <code>n++</code> veut dire « ajouter 1 à n ». La forme <code>for (;;)</code>, sans rien, répète pour toujours : c'est la <b>boucle principale</b> de tout jeu, qui tourne une fois par image."))
A(h2('Les fonctions'))
A(p("<code>print_ink(...)</code>, <code>background(...)</code> ou <code>random(256)</code> sont des <b>fonctions</b> : des morceaux de programme qui ont un nom, reçoivent des valeurs entre parenthèses et parfois en rendent une. <code>random(256)</code> (« au hasard ») rend un nombre entre 0 et 255. Vous écrirez vos propres fonctions au chapitre 9."))
A(box('know', 'La recette de tout jeu', listing("""int main(void) {
  init();
  // 1. set up: colours, pictures, map, variables
  for (;;) {
    wait_frame();
    // 2. read the joypad
    // 3. move the world, check collisions
    // 4. draw: sprites, score
  }
}""", numbers=False) + p("Préparer, puis pour chaque image : lire la manette, faire bouger le monde, afficher.")))

# ---------------------------------------------------------------- 5
A(chapter(5, 'La manette', "Savoir ce que fait le joueur, image après image.", 5))
A(FIG_JOYPAD)
A(p("Deux fonctions suffisent pour tout savoir des manettes :"))
A(table(['Fonction', 'Rend vrai quand…', 'Pour…'], [
  [c('held(1, LEFT)'), "le bouton est enfoncé en ce moment (<i>held</i> : tenu)", "les déplacements, le tir automatique"],
  [c('pressed(1, B1)'), "le bouton vient d'être enfoncé, une seule fois par appui (<i>pressed</i> : appuyé)", "sauter, valider un menu, compter"]]))
A(p("Le premier nombre est le joueur, de 1 à 4. Les manettes sont lues une fois par image, par <code>wait_frame()</code>. Voici un programme d'essai qui affiche l'état de chaque commande des quatre manettes :"))
A(listing(source('03_joypad'), '03_joypad/main.c'))
A(screen('screen_03.png', "Le joueur 1 tient HAUT, DROITE et le bouton 2, le joueur 3 GAUCHE et le bouton 1, le joueur 4 BAS et START."))
A(p("Ce programme utilise deux <b>tableaux</b> : <code>labels[12]</code> contient douze textes, <code>buttons[12]</code> douze boutons. <code>labels[0]</code> est le premier, <code>labels[11]</code> le dernier. La boucle <code>for</code> les parcourt tous avec la même variable <code>i</code> ; une seconde boucle, à l'intérieur, passe en revue les joueurs. Le tableau <code>presses[5]</code> garde un compteur par joueur : on lui a donné une case de plus pour écrire <code>presses[1]</code> à <code>presses[4]</code>, la case 0 reste inutilisée."))
A(box('know', 'Joueurs 3 et 4', p("Les manettes 3 et 4 existent depuis la version 2 de la console. Sur un core MiSTer plus ancien, <code>held(3, …)</code> et <code>held(4, …)</code> répondent toujours faux : le jeu reste jouable à un ou deux.")))

# ---------------------------------------------------------------- 6
A(chapter(6, 'Les sprites', "Des objets qui bougent : votre héros, les ennemis, les bonus.", 6))
A(h2('Dessiner une planche'))
A(p("Les dessins des sprites se rangent dans une <b>planche</b> (<i>sprite sheet</i>) : une image PNG de <b>16 pixels de large</b>, où les dessins de 16 x 16 sont posés les uns sous les autres. Le premier porte le numéro 0, le suivant le numéro 1, et ainsi de suite. Dessinez-la avec n'importe quel logiciel de dessin (Aseprite, LibreSprite, GIMP, Paint…) en respectant trois règles :"))
A(ul(["une <b>seule colonne</b> de dessins, de haut en bas : la largeur de l'image est celle d'un dessin ;",
      "<b>15 couleurs au plus</b> pour toute la planche ;",
      "le <b>transparent</b> (les pixels effacés de l'image) laisse voir ce qui est derrière le sprite."]))
A('<div class="sheet-row"><img class="sheet" src="images/sheet_x8.png"><div>' +
  table(['N°', 'Nom dans le code', 'Dessin'], [['0', c('SHIP'), 'vaisseau'], ['1', c('STAR'), 'étoile'], ['2', c('STAR2'), 'étoile qui scintille'], ['3', c('BOMB'), 'bombe'], ['4', c('EXPLOSION'), 'explosion'], ['5', c('HEART'), 'cœur']]) +
  p("La planche des exemples, agrandie huit fois. Elle est dans <code>04_sprites/sheet.png</code>.") + '</div></div>')
A(p("Il suffit de la nommer dans <code>project.txt</code> (<code>sprites = sheet.png</code>) : <code>build.py</code> la convertit, et <code>init()</code> la charge avec ses couleurs. Dans le programme, <code>sheet</code> (le nom du fichier sans <code>.png</code>) est le numéro de son premier dessin : <code>sheet + 2</code> est son troisième dessin. Ce nom est prêt à l'emploi, il n'y a rien à déclarer."))
A(p("Un jeu peut avoir plusieurs planches : <code>sprites = hero.png enemies.png</code>. Leurs dessins se numérotent à la suite, et <code>hero + 1</code> ou <code>enemies + 3</code> les désignent sans avoir à compter. Chaque planche garde ses propres 15 couleurs."))
A(box('tip', 'De plus grands sprites', p("Pour un héros plus détaillé, une planche peut faire <b>32 ou 64 pixels de large</b>, avec des dessins de 32 x 32 ou de 64 x 64 posés les uns sous les autres. Chaque dessin garde la taille de sa planche : un héros de 64 x 64 et des bonus de 16 x 16 se côtoient sans rien changer au programme.")))
A(h2('Afficher et déplacer'))
A(listing(source('04_sprites'), '04_sprites/main.c'))
A(screen('screen_04.png', 'Le vaisseau agrandi deux fois (bouton 2), et les étoiles qui scintillent.'))
A(table(['Fonction', 'Effet'], [
  [c('sprite(n, x, y, picture)'), "affiche le sprite n (0 à 63) à la position x, y avec le dessin choisi"],
  [c('hide_sprite(n)'), "fait disparaître le sprite n"],
  [c('flip_sprite(n, h, v)'), "le retourne horizontalement (h = 1) et/ou verticalement (v = 1)"],
  [c('scale_sprite(n, 200)'), "l'agrandit (200 = double, 50 = moitié)"],
  [c('collide(a, b)'), "vrai si les sprites a et b se touchent"]]))
A(box('tip', 'Animer', p("Pour animer un sprite, on change de dessin régulièrement. <code>(frame_count / 10) % 2</code> vaut 0 pendant 10 images, puis 1 pendant 10 images, etc. : de quoi alterner deux dessins six fois par seconde. Avec <code>% 4</code>, on fait tourner quatre dessins.")))
A(box('warn', 'Les limites', p("On peut afficher jusqu'à <b>64 sprites sur une même ligne</b> de l'écran, et 1280 pixels de sprites en tout sur cette ligne : c'est 80 sprites de 16 x 16 mais seulement <b>20 sprites de 64 x 64</b> côte à côte. Au-delà, les derniers ne sont pas dessinés sur cette ligne : le moniteur du simulateur l'indique en rouge. Un jeu a au plus <b>8 planches et décors</b> en tout, car chacun prend l'une des 8 palettes de couleurs.")))

# ---------------------------------------------------------------- 7
A(chapter(7, 'Le son', "Des bips, des bruits, des mélodies et des échantillons.", 7))
A(p("La MONOCORE-1 a <b>8 canaux</b> de notes (numérotés de 0 à 7), qui jouent ensemble, et <b>8 voix d'échantillons</b> pour les sons enregistrés."))
A(table(['Fonction', 'Effet'], [
  [c('tone(channel, 440, 150)'), "joue un son carré de 440 hertz (le LA du diapason) au volume 150 sur 255"],
  [c('noise(channel, 120)'), "un souffle : explosions, vagues, caisse claire"],
  [c('quiet(channel)'), "fait taire le canal"],
  [c('play(channel, tune, 15, 100)'), "joue une mélodie en boucle, toute seule"],
  [c('stop(channel)'), "arrête la mélodie"],
  [c('play_sample(voice, sound, volume)'), "joue un son enregistré (fichier WAV)"]]))
A(listing(source('05_sound'), '05_sound/main.c'))
A(screen('screen_05.png', 'Le petit piano : chaque bouton joue une note.'))
A(h2('Écrire une mélodie'))
A(p("Une mélodie est un texte où chaque mot est un <b>temps</b>. Les notes s'écrivent avec les lettres de la notation anglaise :"))
A(table(['DO', 'RÉ', 'MI', 'FA', 'SOL', 'LA', 'SI'], [[c('C'), c('D'), c('E'), c('F'), c('G'), c('A'), c('B')]], 'notes'))
A(table(['Écriture', 'Signification'], [
  [c('C D E F G A B'), "les notes, dans l'octave 4 (celle du LA à 440 Hz)"],
  [c('C5  G3'), "un chiffre après la note change d'octave : C5 est le DO aigu suivant"],
  [c('F#  C#5'), "un # (dièse) monte la note d'un demi-ton"],
  [c('-'), "prolonge la note précédente d'un temps"],
  [c('.'), "un temps de silence"]]))
A(p("Le troisième nombre de <code>play()</code> règle le tempo : le nombre d'images par temps. 15 images, c'est 4 temps par seconde (240 à la minute) ; 30, deux fois plus lent. Pour jouer plusieurs voix en même temps (une mélodie et une basse), lancez deux mélodies sur deux canaux."))
A(h2('Les sons enregistrés'))
A(p("Un fichier WAV nommé dans <code>project.txt</code> (<code>sounds = boom.wav</code>) devient le nom <code>boom</code>. On le joue sur la voix 0, au volume 230, avec :"))
A(listing("""play_sample(0, boom, 230);""", numbers=False))
A(box('tip', 'Bonne pratique', p("Gardez des sons courts et en 8 000 à 22 050 Hz : ils occupent moins de place dans la ROM et sonnent très « arcade ». Un son de 16 000 Hz pendant une demi-seconde occupe 8 Ko.")))

# ---------------------------------------------------------------- 8
A(chapter(8, 'Le décor', "Un grand paysage en tuiles, qui défile derrière les sprites.", 8))
A(p("Le décor (<i>map</i>) est une grande image découpée en <b>tuiles de 16 x 16 pixels</b>. Les tuiles identiques ne sont stockées qu'une fois : un ciel uni ou un sol répétitif ne coûtent presque rien. Le décor peut mesurer jusqu'à <b>1024 x 1024 pixels</b> et il boucle : en défilant au-delà du bord droit, on retrouve le bord gauche."))
A(ul(["Dessinez une image dont la largeur et la hauteur sont des multiples de 16, en <b>15 couleurs</b> au plus ;",
      "nommez-la dans <code>project.txt</code> : <code>maps = landscape.png</code> ;",
      "<code>init()</code> l'affiche, avec ses couleurs ; s'il y a plusieurs décors (<code>maps = level1.png level2.png</code>), c'est le premier, et <code>load_map(level2)</code> passe au suivant."]))
A(listing(source('06_scroll'), '06_scroll/main.c'))
A(screen('screen_06.png', 'Le paysage de 1024 pixels défile derrière le vaisseau.'))
A(p("<code>scroll_map(x, y)</code> (« faire défiler le décor ») indique quel point du décor apparaît dans le coin en haut à gauche de l'écran. En augmentant <code>x</code> à chaque image, le décor glisse vers la gauche : on a l'impression d'avancer vers la droite."))
A(box('know', 'Le saviez-vous ?', p("Les jeux des années 80 donnaient de la profondeur en faisant défiler plusieurs plans à des vitesses différentes : le lointain lentement, le premier plan vite. La MONOCORE-1 a quatre plans, et peut même faire défiler chaque ligne de l'écran différemment (chapitre 10).")))

# ---------------------------------------------------------------- 9
A(chapter(9, 'Projet : ATTRAPE-ÉTOILES', "Un jeu complet : écran titre, score, record, vies, musique et explosions.", 9))
A('<div class="pair">' + screen('screen_07_title.png', "L'écran titre.", True) + screen('screen_07_game.png', 'Une partie en cours.', True) + '</div>')
n_lines = len(source('07_stars').rstrip('\n').split('\n'))
A(p("Le principe : des étoiles et des bombes tombent du ciel. Déplacez le vaisseau avec LEFT et RIGHT pour attraper les étoiles (10 points chacune) et éviter les bombes (une vie perdue). Plus le score monte, plus tout tombe vite. Le jeu tient en %d lignes, rangées en <b>fonctions</b> qui ont chacune un rôle." % n_lines))
A(h2('Organisation du programme'))
A(table(['Fonction', 'Rôle'], [
  [c('main'), "prépare la console, puis enchaîne pour toujours : titre, partie, fin de partie"],
  [c('title_screen'), "écran titre : affiche le titre et les règles, joue la musique, attend START"],
  [c('game'), "la partie : manette, objets, explosion, score, jusqu'à ce qu'il n'y ait plus de vies"],
  [c('drop_item'), "« lâcher un objet » : le place en haut de l'écran, au hasard, étoile ou bombe"],
  [c('move_items'), "fait tomber les objets et teste s'ils touchent le vaisseau"],
  [c('show_score'), "écrit le score et le record, dessine les cœurs"],
  [c('game_over'), "affiche PARTIE TERMINEE pendant trois secondes"]]))
A(h2('Les structures'))
A(p("Chaque objet qui tombe a une position, une vitesse, et peut être une étoile ou une bombe. Plutôt que d'avoir quatre tableaux séparés, on regroupe ces informations dans une <b>structure</b> : <code>item_t</code> (« objet »). Le tableau <code>items[6]</code> contient six objets, et <code>it-&gt;y</code> désigne la position verticale de l'objet <code>it</code>."))
A(h2('Le listing complet'))
A(listing(source('07_stars', 'project.txt'), '07_stars/project.txt', numbers=False, lang='txt'))
A(listing(source('07_stars'), '07_stars/main.c'))
A(h2('Pour aller plus loin'))
A(p("Le jeu est à vous : modifiez-le ! Quelques idées, de la plus simple à la plus ambitieuse :"))
A(ol(["Changez les couleurs, la vitesse, le nombre de points par étoile.",
      "Ajoutez un objet rare (le cœur, dessin 5) qui rend une vie.",
      "Faites défiler le décor plus vite quand le score augmente.",
      "Ajoutez une deuxième voix à la musique (une basse sur le canal 3).",
      "Mode à plusieurs : jusqu'à quatre vaisseaux, un par manette (<code>held(2, LEFT)</code> pour le joueur 2…).",
      "Ajoutez le tir : le bouton 1 lance un projectile qui détruit les bombes."]))

# ---------------------------------------------------------------- 10
A(chapter(10, 'Sous le capot', "La bibliothèque easy n'est qu'un raccourci : voici ce que la console sait faire d'autre.", 10))
A(p("Toutes les fonctions de <code>easy</code> écrivent dans des <b>registres</b> : des adresses mémoire spéciales qui commandent les circuits de la console. <code>monocore.h</code> leur donne des noms. Vous pouvez les utiliser à côté de <code>easy</code>, à condition de respecter son rangement (plans 0 et 1, palettes 0 à 15 : les 8 encres puis les 8 palettes de couleurs, sprites 0 à 63) ou de ne pas l'utiliser du tout."))
A(h2("Changer la taille de l'écran"))
A(listing("""SCREEN_W = 256;   // from 224 to 384 pixels wide
SCREEN_H = 224;   // from 192 to 288 lines""", numbers=False))
A(p("La nouvelle taille prend effet à l'image suivante. Pour un jeu vertical (comme Pac-Man ou Galaga), on choisit 224 x 288 et on demande que l'image soit tournée d'un quart de tour avec <code>DISP_CTRL |= DISP_ROT(1)</code>. La ligne <code>rotation = 1</code> de <code>project.txt</code> inscrit cette orientation dans la ROM, pour que l'écran soit tourné avant même que le jeu démarre. Le simulateur le fait déjà, le core MiSTer le fera dans une prochaine version."))
A(h2('Les quatre plans'))
A(p("Les plans 2 et 3, inutilisés par <code>easy</code>, se règlent comme le décor : <code>LAYER_CTRL(n)</code> (taille des tuiles, 4 ou 8 bits par pixel, taille de la carte, priorité), <code>LAYER_CHR(n)</code> (adresse des dessins), <code>LAYER_SCROLLX(n)</code> et <code>LAYER_SCROLLY(n)</code>, et la carte <code>TILEMAP(n)</code>. Il faut ensuite les allumer : <code>DISP_CTRL |= DISP_LAYER(2)</code>."))
A(h2('Le défilement ligne par ligne'))
A(p("Avec l'option <code>LC_LINESCROLL</code>, chaque ligne de l'écran ajoute son propre décalage au défilement du plan : routes de jeux de course, vagues, effets de chaleur."))
A(listing("""LAYER_CTRL(1) |= LC_LINESCROLL;
for (int y = 0; y < 240; y++) {
  LINESCROLL(1)[y].dx = (y > 120) ? (y - 120) * (frame_count % 64) / 32 : 0;
  LINESCROLL(1)[y].dy = 0;
}""", numbers=False))
A(h2('Les sprites sans limites'))
A(p("La console gère <b>512 sprites</b> (la table <code>SPRITES[n]</code>), de 8 x 8 à 256 x 256 pixels (largeur et hauteur en cellules de 8 pixels), en 16 ou 256 couleurs, avec un zoom indépendant en largeur et en hauteur (<code>zoomx</code>, <code>zoomy</code> : 256 = taille normale). Le registre <code>SPR_COUNT</code> dit combien d'entrées la console parcourt."))
A(h2('Les interruptions'))
A(p("Une <b>interruption</b> arrête le programme un instant pour exécuter <code>irq_handler()</code>. <code>easy</code> utilise celle du début de la pause verticale (VBLANK). Il en existe d'autres : LINE (au début d'une ligne choisie par <code>LINE_CMP</code>, pour changer un registre en cours d'image), TIMER0 et TIMER1 (à intervalle régulier, en cycles du processeur : <code>T0_RELOAD = CPU_HZ / 240</code> pour 240 fois par seconde), DMA (fin de transfert)."))
A(h2('Le DMA'))
A(p("Le DMA copie ou remplit la mémoire bien plus vite que le processeur : <code>dma_copy(destination, source, octets)</code> et <code>dma_fill(destination, motif, octets)</code>. Idéal pour charger une palette, une carte de décor, ou des dessins dans la <b>CHR RAM</b> (128 Ko de graphismes modifiables en cours de jeu, à l'adresse <code>CHRRAM_BASE</code>)."))
A(h2('Le temps du processeur'))
A(p("Le processeur exécute environ <b>780 000 instructions par image</b>. C'est énorme : un 68000 de borne d'arcade en exécutait 25 000. Le moniteur du simulateur affiche la charge : si elle dépasse 100 %, le jeu ralentit. Gardez une marge, car le simulateur estime le temps à environ 10 % près."))
A(p("Pour utiliser tout cela sans écrire les registres un par un, le chapitre 11 présente le SDK <b>mc</b>."))
A(box('warn', "Règle d'or", p("Ne comptez jamais sur un nombre de cycles pour régler la vitesse d'un jeu : synchronisez-vous toujours sur les images (<code>wait_frame()</code>) ou sur un timer. C'est ce qui garantit que le jeu tourne pareil dans le simulateur et sur la console.")))


# ---------------------------------------------------------------- 11
A(chapter(11, 'Passer au SDK mc', "Quand easy devient trop petit : toute la console, toujours en C, sans assembleur.", 11))
A(p("La bibliothèque <code>easy</code> range la console d'une façon simple et fixe : un plan de texte, un décor, 64 sprites de 16 x 16, 32 x 32 ou 64 x 64. C'est parfait pour apprendre, mais un jeu plus ambitieux, ou l'adaptation d'un jeu existant, a besoin de tout le reste : les quatre plans, les 512 sprites, les interruptions, la mémoire. Le chapitre 10 a montré qu'on peut tout faire avec les registres ; le SDK <b>mc</b> (pour <i>MONOCORE</i>) donne à ces registres des fonctions pratiques, sans rien cacher."))
A(table(['', 'easy', 'mc'], [
  ['Pour qui', 'débuter, petits jeux', 'jeux complexes, adaptations'],
  ['Plans', 'texte (0) et décor (1)', 'les quatre, réglés librement'],
  ['Sprites', '64, de 16 x 16, 32 x 32 ou 64 x 64','512, de 8 x 8 à 256 x 256, zoom'],
  ['Interruptions', 'cachées', 'VBLANK, deux timers, effets raster, en C'],
  ['Mémoire', 'variables seulement', '<code>malloc</code>, <code>free</code>, CHR RAM'],
  ['Bibliothèque C', '—', '<code>string.h</code>, <code>stdlib.h</code>, <code>stdio.h</code>…']]))
A(h2('Choisir mc'))
A(p("Une seule ligne dans <code>project.txt</code> suffit, et le programme inclut <code>mc.h</code> au lieu de <code>easy.h</code>. Toutes les fonctions commencent par <code>mc_</code>. On ne mélange pas les deux bibliothèques dans un même programme."))
A(listing(source('11_mc', 'project.txt'), '11_mc/project.txt', numbers=False, lang='txt'))
A(p("Le squelette ressemble à celui d'<code>easy</code> : <code>mc_init()</code> au début, puis une boucle qui commence par <code>mc_frame()</code>. Cette fonction attend l'image suivante, puis envoie les sprites à la console, lit les manettes et fait avancer les mélodies."))
A(h2('Un premier programme'))
A(screen('screen_11.png', "Un arc-en-ciel fait de 30 effets raster, un titre qui ondule et 48 balles dessinées par le programme."))
A(listing(source('11_mc'), '11_mc/main.c'))
A(h2('Les effets raster'))
A(p("La console fabrique l'image ligne par ligne. <code>mc_raster_add(ligne, fonction)</code> demande d'appeler une fonction juste avant qu'une ligne soit dessinée : ce qu'elle change (une couleur, un défilement) s'applique à partir de cette ligne, au pixel près. Ici, 30 effets, un toutes les 8 lignes, changent la couleur de fond : c'est l'arc-en-ciel. Dans la fonction, <code>mc_raster_line</code> dit quelle ligne est en cours. On peut en poser 64 par image ; ils restent en place d'une image à l'autre."))
A(box('know', 'Le saviez-vous ?', p("L'Amiga avait un coprocesseur, le <i>Copper</i>, qui ne faisait que cela : changer des registres à des lignes précises. Les fameuses « barres copper » des démos en viennent. Le premier écran d'accueil de la console (<code>examples/boot</code>) en est un hommage.")))
A(h2('Le défilement ligne par ligne'))
A(p("<code>mc_layer_linescroll(plan, true)</code> autorise un décalage différent pour chaque ligne d'un plan ; <code>mc_linescroll(plan)[y].dx</code> est le décalage horizontal de la ligne <code>y</code>. En le faisant varier en sinus, le titre ondule comme un drapeau. Le même procédé donne les routes des jeux de course, les vagues ou les sols en perspective."))
A(h2('Dessiner dans la CHR RAM'))
A(p("Les dessins ne viennent pas forcément d'une image PNG. La <b>CHR RAM</b>, 128 Ko de dessins modifiables, peut recevoir ce que le programme calcule : <code>make_ball()</code> trace un disque pixel par pixel, l'envoie avec <code>mc_chr_upload</code>, et <code>MC_CHR(0)</code> donne l'adresse à utiliser comme planche de sprites. Un pixel occupe 4 bits : deux pixels par octet, celui de gauche dans les 4 bits de poids faible, 32 octets par case de 8 x 8."))
A(h2('Les sprites'))
A(p("<code>mc_sprite(n, x, y, case, largeur, hauteur, palette, priorité)</code> place le sprite <code>n</code> (0 à 511). La largeur et la hauteur se comptent en cases de 8 pixels ; <code>mc_sprite_zoom</code> l'agrandit ou le réduit (256 = taille normale). Les sprites sont d'abord rangés dans une copie en mémoire, que <code>mc_frame()</code> envoie d'un coup par DMA : on ne voit jamais un sprite à moitié déplacé. <code>mc_sprites_used(48)</code> dit à la console de ne parcourir que les 48 premiers."))
A(h2('La mémoire et la bibliothèque C'))
A(p("Avec <code>mc</code>, les fichiers d'en-tête habituels du C sont disponibles. <code>calloc</code> réserve ici la place des 48 balles dans le <b>tas</b>, la mémoire libre entre les variables et la pile (environ 100 Ko) ; <code>free</code> la rend. <code>snprintf</code> fabrique une ligne de texte avec des nombres, et <code>printf</code> écrit sur la console de debug du simulateur."))
A(table(['En-tête', 'Ce qu\'il apporte'], [
  [c('<string.h>'), "copier, comparer, chercher : <code>strcpy</code>, <code>strcmp</code>, <code>strlen</code>, <code>memcpy</code>…"],
  [c('<stdlib.h>'), "<code>malloc</code>, <code>calloc</code>, <code>realloc</code>, <code>free</code>, <code>abs</code>, <code>atoi</code>, <code>rand</code>, <code>qsort</code>"],
  [c('<stdio.h>'), "<code>snprintf</code>, <code>sprintf</code>, et <code>printf</code> vers la console de debug"],
  [c('<ctype.h>'), "<code>isdigit</code>, <code>toupper</code>…"],
  [c('<assert.h>'), "<code>assert</code> : arrête le programme avec un message si une condition est fausse"]]))
A(box('warn', 'Pas de nombres à virgule', p("Le processeur de la console ne calcule qu'en nombres entiers. Le compilateur accepte <code>float</code>, mais l'imite très lentement. <code>mc</code> fournit la <b>virgule fixe</b> : un <code>fix16</code> est un entier qui compte en 65 536<sup>e</sup>, <code>FIX(1.5)</code> vaut 1,5, et <code>mc_sin</code>, <code>mc_cos</code>, <code>mc_atan2</code> travaillent avec des angles de 1024 pas par tour. Dans l'exemple, les positions des balles comptent en 256<sup>e</sup> de pixel, ce qui permet des vitesses fines.")))
A(h2('Quatre manettes, l\'analogique et le clavier'))
A(p("Avec <code>mc</code>, <code>mc_pad(j)</code> lit les quatre joueurs, comme <code>held</code> et <code>pressed</code> d'<code>easy</code>. La console lit aussi les sticks analogiques (<code>mc_stick_x</code>, <code>mc_stick_y</code> : stick 0 à gauche, 1 à droite, de −127 à +127, y positif vers le bas), les paddles (<code>mc_paddle</code>, de 0 à 255) et les spinners (<code>mc_spinner_delta</code> : le déplacement depuis l'image précédente), ainsi qu'un clavier USB. Ces entrées sont arrivées avec la <b>version 2</b> du matériel : un jeu qui en a besoin vérifie <code>mc_hw_version()</code>, qui vaut 1 sur un core plus ancien, où ces fonctions renvoient 0."))
A(listing("""if (mc_hw_version() >= 2) {
  for (int j = 1; j <= 4; j++) {
    ship[j - 1].x += mc_stick_x(j, 0) / 32;        // left stick, -127 to +127
    if (mc_pad_pressed(j) & IN_B(1)) fire(j - 1);
  }
  if (mc_key_pressed(KEY_ESC)) paused = !paused;    // state of a key
  int e;
  while ((e = mc_key_event()) >= 0)                 // events, in order
    if (e & 0x200) type_key(e & 0x1FF);             // 0x200: key down
}""", numbers=False))
A(p("Une touche se désigne par son code PS/2, <code>KEY_xxx</code> dans <code>monocore.h</code>. Ce code repère une <b>position</b> sur le clavier et porte le nom de la touche d'un clavier QWERTY : sur un clavier AZERTY, la touche A est <code>KEY_Q</code>. <code>mc_key</code> et <code>mc_key_pressed</code> donnent l'état relevé par <code>mc_frame()</code>, pratique pour un jeu ; <code>mc_key_event()</code> rend les appuis et relâchements un par un, dans l'ordre, ce qui convient à la saisie de texte. L'exemple <code>examples/test_inputs</code> affiche tout ce que la console reçoit."))
A(h2('D\'autres exemples'))
A(p("Le dossier <code>examples</code> du projet contient d'autres programmes écrits avec <code>mc</code> : <code>horizon</code>, une course en fausse 3D à la OutRun faite d'une seule texture de route décalée ligne par ligne ; <code>aube</code>, l'écran d'accueil, dont le lever de soleil n'est qu'une animation de palette, accompagné d'une musique orchestrale jouée par deux voix PCM ; <code>boot</code>, le premier écran d'accueil, avec ses barres copper."))
A(p("Toutes les fonctions sont rassemblées dans l'annexe D ; le fichier <code>sdk/SDK_MC.md</code> donne en plus les règles du matériel à connaître pour adapter un jeu existant."))

# ---------------------------------------------------------------- 12
A(chapter(12, 'Projet : NEBULA', "Un shoot'em up complet, dont tous les dessins sont calculés au démarrage.", 12))
A('<div class="pair">' + screen('screen_12_title.png', "L'écran titre : des lettres de la police agrandies quatre fois.", True) + screen('screen_12_game.png', 'Une partie : soucoupes, tirs et traînée du réacteur.', True) + '</div>')
n_neb = len(source('../../examples/nebula').rstrip('\n').split('\n'))
A(p("Le vaisseau traverse un paysage de montagnes et de collines ; des vagues de soucoupes arrivent en ondulant, et des orbes tirent vers le joueur. Le bouton 1 tire, la croix pilote. Le programme (%d lignes, dans <code>examples/nebula</code>) n'utilise <b>aucun fichier image ni son</b> : les sprites sont dessinés en caractères dans le code, et les décors sont calculés." % n_neb))
A(h2('Organisation du programme'))
A(table(['Fonction', 'Rôle'], [
  [c('make_sprites'), "convertit les dessins en caractères (<code>art_ship</code>…) en cases de sprites, dans la CHR RAM"],
  [c('build_layer'), "découpe une image calculée de 512 x 256 pixels en tuiles, en partageant les tuiles identiques"],
  [c('make_backgrounds'), "calcule les profils des montagnes et des collines, construit les trois plans de décor"],
  [c('scroll_backgrounds'), "fait défiler les trois plans à trois vitesses, et le sol en perspective"],
  [c('sky'), "effet raster : une couleur de ciel toutes les 4 lignes"],
  [c('play_frame'), "une image de jeu : manette, tirs, vagues d'ennemis, collisions"],
  [c('fire_at_player'), "tir d'un orbe, dirigé vers le joueur avec <code>mc_atan2</code>"],
  [c('explosion'), "lance des dizaines de particules dans toutes les directions"],
  [c('update_fx'), "fait vivre les particules : elles ralentissent, refroidissent et rétrécissent"],
  [c('title_screen'), "écran titre"],
  [c('game_over'), "fin de partie, avec le record gardé en mémoire sauvegardée (NVRAM)"]]))
A(h2('Des dessins en caractères'))
A(p("Chaque sprite est dessiné dans le code, une chaîne de 16 caractères par ligne : un point est transparent, une lettre de <code>A</code> à <code>O</code> désigne une couleur de 1 à 15. <code>pack_art</code> range ces pixels en cases de 8 x 8, dans l'ordre que la console attend pour un sprite de 2 x 2 cases : en haut à gauche, en haut à droite, en bas à gauche, en bas à droite."))
_neb = source('../../examples/nebula').split('\n')
_i = next(i for i, l in enumerate(_neb) if 'art_ship[16]' in l)
A(listing('\n'.join(_neb[_i:_i + 5]) + '\n  ...', 'examples/nebula/main.c : le dessin du vaisseau', numbers=False))
A(h2('Des décors calculés'))
A(p("Les montagnes sont une somme de trois sinus : <code>far_top[x]</code> donne la hauteur du sommet pour chaque colonne. La fonction <code>far_pixel(x, y)</code> dit la couleur de chaque point d'une image de 512 x 256 pixels : rien au-dessus du sommet, une ligne claire sur la crête, puis des bandes de plus en plus sombres. <code>build_layer</code> découpe cette image en 2 048 tuiles de 8 x 8, mais n'en garde qu'une centaine : les tuiles identiques (le ciel vide, les bandes pleines) sont partagées. Pour les reconnaître vite, chaque tuile reçoit une empreinte (un <i>hachage</i>), et on ne compare en détail que les tuiles de même empreinte."))
A(box('tip', 'Pourquoi des bandes de 16 lignes ?', p("Les bandes de couleur commencent sur des multiples de 16 lignes, donc sur des bords de tuiles. Toutes les tuiles à l'intérieur d'une bande sont alors identiques : une seule suffit pour toute la bande.")))
A(h2('Trois plans et un sol'))
A(p("Les étoiles (plan 2) défilent d'un pixel toutes les 8 images, les montagnes (plan 1) toutes les 4 images, les collines (plan 3) d'un pixel par image : c'est la <b>parallaxe</b>. Les 24 dernières lignes, le damier du sol, défilent de plus en plus vite vers le bas grâce au défilement ligne par ligne : on croit voir un sol qui fuit vers l'horizon. Les priorités rangent le tout : le texte (plan 0, priorité 3) devant les sprites (priorité 4), devant les collines (priorité 2), devant les montagnes (priorité 1), devant les étoiles (priorité 0)."))
A(box('know', 'La règle des priorités', p("Chaque pixel d'un plan a une priorité <i>effective</i> égale à deux fois celle du plan, plus 1 si la tuile porte la marque <code>T_PRIO</code>. Un sprite de priorité <i>p</i> passe devant les pixels de priorité effective inférieure ou égale à <i>p</i>. Le texte, de priorité effective 6, cache donc les sprites de priorité 4.")))
A(h2('Viser le joueur'))
A(p("<code>mc_atan2(dy, dx)</code> donne la direction qui va d'un orbe au vaisseau, en 1024 pas par tour. <code>mc_cos</code> et <code>mc_sin</code> la transforment en une vitesse horizontale et verticale. Les positions comptent en 256<sup>e</sup> de pixel (<code>FP</code> vaut 8) : le tir avance d'environ 1,6 pixel par image, dans n'importe quelle direction."))
A(h2('Des particules'))
A(p("Une explosion lance jusqu'à 48 particules. Leur réserve de 160 places est prise dans le tas avec <code>calloc</code>. À chaque image, une particule ralentit (sa vitesse est multipliée par 15/16), change de palette en refroidissant (blanc, jaune, orange, rouge sombre) et rétrécit grâce au zoom. Avec les tirs et les ennemis, le jeu peut afficher 233 sprites à la fois."))
A(h2('Le listing complet'))
A(listing(source('../../examples/nebula', 'project.txt'), 'examples/nebula/project.txt', numbers=False, lang='txt'))
A(listing(source('../../examples/nebula'), 'examples/nebula/main.c'))
A(h2('Pour aller plus loin'))
A(ol(["Changez les dessins du vaisseau et des ennemis, ou les couleurs du ciel.",
      "Ajoutez un bonus qui donne un tir double pendant quelques secondes.",
      "Faites apparaître un « boss » : un grand sprite de 64 x 64 (8 x 8 cases) qui encaisse 30 tirs.",
      "Remplacez les décors calculés par un paysage dessiné, converti avec <code>maps = …</code> dans <code>project.txt</code>.",
      "Passez l'écran en 384 x 224 avec <code>mc_video_mode</code>, comme une borne d'arcade."]))

# ---------------------------------------------------------------- appendices
A(chapter('A', 'La bibliothèque easy', "Toutes les fonctions, d'un coup d'oeil, avec la traduction de leur nom.", 1))
A(table(['Fonction', 'Traduction', 'Description'], [
  [c('init()'), 'initialiser', "prépare la console : à appeler une fois, au début"],
  [c('wait_frame()'), "attendre l'image", "attend l'image suivante (1/60 de seconde), lit les manettes, fait avancer les mélodies"],
  [c('frame_count'), "nombre d'images", "variable : images depuis le démarrage"],
  [c('random(n)'), 'au hasard', "nombre au hasard de 0 à n - 1"],
  [c('background(r, g, b)'), 'fond', "couleur du fond (rouge, vert, bleu de 0 à 255)"],
  [c('ink(n, r, g, b)'), 'encre', "couleur de l'encre de texte n (0 à 7)"],
  [c('load_palette(n, pal)'), 'charger une palette', "range les couleurs d'une image en palette n (0 à 7), par exemple <code>load_palette(3, hero_pal)</code>"],
  [c('cls()'), "effacer l'écran", "efface tout le texte (de <i>CLear Screen</i>)"],
  [c('print(col, row, text)'), 'imprimer', "écrit en blanc, colonne <i>col</i>, ligne <i>row</i>"],
  [c('print_ink(col, row, text, ink)'), 'imprimer en couleur', "écrit avec l'encre choisie"],
  [c('print_num(col, row, n, digits, ink)'), 'imprimer un nombre', "écrit un nombre sur <i>digits</i> chiffres, complété par des zéros"],
  [c('held(player, button)'), 'tenu', "vrai si le bouton du joueur (1 à 4) est enfoncé"],
  [c('pressed(player, button)'), 'appuyé', "vrai si le bouton du joueur (1 à 4) vient d'être enfoncé"],
  [c('sprite(n, x, y, picture)'), 'sprite', "affiche le sprite n (0 à 63) avec le dessin <i>picture</i>"],
  [c('hide_sprite(n)'), 'cacher', "cache le sprite n"],
  [c('flip_sprite(n, h, v)'), 'retourner', "retourne le sprite n"],
  [c('scale_sprite(n, percent)'), 'mettre à l\'échelle', "agrandit ou réduit le sprite n"],
  [c('sprite_palette(n, palette)'), 'palette du sprite', "change la palette du sprite n (-1 : celle de sa planche)"],
  [c('collide(a, b)'), 'se heurter', "vrai si les sprites a et b se touchent"],
  [c('load_map(map)'), 'charger le décor', "remplace le décor affiché par <code>init()</code>"],
  [c('scroll_map(x, y)'), 'faire défiler', "fait défiler le décor"],
  [c('tone(channel, freq, volume)'), 'note', "son carré sur un canal"],
  [c('noise(channel, volume)'), 'bruit', "bruit sur un canal"],
  [c('quiet(channel)'), 'silence', "fait taire un canal"],
  [c('play(channel, tune, frames, volume)'), 'jouer', "joue une mélodie en boucle"],
  [c('stop(channel)'), 'arrêter', "arrête la mélodie du canal"],
  [c('play_sample(voice, sound, volume)'), 'jouer un échantillon', "joue un son enregistré"]], 'ref'))
A(p("Boutons : <code>UP</code> (haut), <code>DOWN</code> (bas), <code>LEFT</code> (gauche), <code>RIGHT</code> (droite), <code>B1</code> à <code>B6</code>, <code>START</code>, <code>COIN</code> (pièce). Encres par défaut : 0 blanc, 1 jaune, 2 cyan, 3 vert, 4 rouge, 5 violet, 6 orange, 7 gris."))
A(h3('Le fichier project.txt'))
A(table(['Réglage', 'Contenu', 'Noms créés pour le programme'], [
  [c('name'), "nom du fichier produit (<code>build/name.mnc</code>)", ''],
  [c('title'), "titre inscrit dans la ROM", ''],
  [c('sources'), "fichiers C du programme", ''],
  [c('sprites'), "planches de sprites (PNG de 16, 32 ou 64 pixels de large)", c('x') + " : numéro de son premier dessin"],
  [c('maps'), "décors (PNG, tuiles de 16 x 16)", c('x') + " : le décor, pour <code>load_map</code>"],
  [c('sounds'), "sons (WAV)", c('x') + " : le son, pour <code>play_sample</code>"],
  [c('origin'), "origine inscrite dans la ROM (facultatif)", ''],
  [c('rotation'), "orientation de l'écran au démarrage : 0 à 3 quarts de tour (facultatif)", ''],
  [c('dip'), "DIP switches par défaut, par exemple <code>0x01</code> (facultatif)", '']], 'ref'))
A(p("<code>x</code> est le nom du fichier sans son extension : <code>sheet.png</code> donne <code>sheet</code>. Les planches puis les décors reçoivent chacun une palette de couleurs, dans l'ordre de <code>project.txt</code> (0, 1, 2…). Les données brutes existent aussi sous les noms <code>x_chr</code>, <code>x_pal</code>, <code>x_map</code>, <code>x_data</code>… (utiles avec <code>load_palette</code> ou le SDK <b>mc</b>)."))

A(chapter('B', 'Carte mémoire et registres', "Pour programmer la console directement (monocore.h).", 2))
A(table(['Adresse', 'Taille', 'Contenu'], [
  [c('0x0000_0000'), '32 Mo max', 'ROM : programme, dessins, sons (lecture seule)'],
  [c('0x1000_0000'), '128 Ko', 'mémoire de travail : variables, pile'],
  [c('0x1800_0000'), '32 Ko', 'mémoire sauvegardée (scores, réglages)'],
  [c('0x2000_0000'), '4 Ko', 'registres vidéo'],
  [c('0x2001_0000'), '8 Ko', 'palette : 4096 couleurs RGB555'],
  [c('0x2002_0000'), '8 Ko', 'table des 512 sprites, 16 octets chacun'],
  [c('0x2004_0000'), '64 Ko', 'cartes des 4 plans, 16 Ko chacune'],
  [c('0x2008_0000'), '8 Ko', 'défilement par ligne des 4 plans'],
  [c('0x200C_0000'), '128 Ko', 'CHR RAM : dessins modifiables'],
  [c('0x3000_0000'), '4 Ko', 'registres son'],
  [c('0x4000_0000'), '4 Ko', 'manettes, interruptions, timers, DMA']], 'ref'))
A(h3('Vidéo'))
A(table(['Registre', 'Rôle'], [
  [c('DISP_CTRL'), 'bit 0 affichage, bits 1 à 4 plans 0 à 3, bit 5 sprites, bits 8-9 rotation'],
  [c('SCREEN_W / SCREEN_H'), "taille de l'image (224-384 x 192-288)"],
  [c('BG_COLOR'), 'numéro de la couleur de fond dans la palette'],
  [c('LINE_CMP'), "ligne de l'interruption LINE"],
  [c('VCOUNT / FRAME'), "ligne en cours, numéro d'image (lecture)"],
  [c('SPR_CHR / SPR_COUNT'), "adresse des dessins de sprites, nombre d'entrées parcourues"],
  [c('LAYER_CTRL(n)'), 'plan n : tuiles 16 (bit 0), 8 bpp (bit 1), largeur et hauteur de carte, défilement par ligne (bit 6), priorité (bits 8-9)'],
  [c('LAYER_CHR(n)'), 'adresse des dessins du plan n'],
  [c('LAYER_SCROLLX/Y(n)'), 'défilement du plan n']], 'ref'))
A(h3('Son'))
A(table(['Registre', 'Rôle'], [
  [c('PCM[v].start / length / loop / step'), "voix d'échantillons v (0 à 7) : adresse, longueur, boucle, pas (0x10000 = 48 kHz)"],
  [c('PCM[v].vol_l / vol_r / format'), 'volumes, format (0 : 8 bits, 1 : 16 bits, 2 : ADPCM)'],
  [c('PCM[v].ctrl'), '1 lance, 0 arrête'],
  [c('PSG[c].freq'), 'fréquence du canal c en 1/16 de hertz'],
  [c('PSG[c].wave / duty'), 'forme (carré, triangle, dent de scie, bruit), rapport cyclique'],
  [c('PSG[c].vol_l / vol_r'), 'volumes'],
  [c('MASTER_VOL'), 'volume général']], 'ref'))
A(h3('Entrées, interruptions, timers, DMA'))
A(table(['Registre', 'Rôle'], [
  [c('IN_P1 / IN_P2 / IN_SYS / DIPSW'), 'manettes, boutons système, DIP switches'],
  [c('HW_VERSION'), 'version du matériel : 2 et plus, les entrées ci-dessous existent'],
  [c('IN_P3 / IN_P4'), 'joueurs 3 et 4, même format que <code>IN_P1</code>'],
  [c('ANALOG_P(n)'), 'manette n (0 à 3) : sticks gauche et droit, X et Y de −127 à +127'],
  [c('PADDLE_P(n)'), 'paddle de 0 à 255 (bits 0-7), position du spinner (bits 16-31)'],
  [c('KEY_STATE(k)'), 'clavier : 8 mots de 32 bits, un bit par touche enfoncée'],
  [c('KEY_EVENT'), 'plus ancien événement clavier (bit 31 valide, bit 9 appui) ; écrire pour le retirer'],
  [c('IRQ_STATUS / IRQ_ENABLE'), 'interruptions en attente (écrire 1 pour acquitter) et autorisées'],
  [c('T0_RELOAD / T0_CTRL'), 'timer 0 : période en cycles, marche'],
  [c('CYCLE_LO / CYCLE_HI'), 'compteur de cycles du processeur (50 MHz)'],
  [c('DMA_SRC / DST / LEN / CTRL'), 'copie (CTRL = 1) ou remplissage (CTRL = 3)'],
  [c('DEBUG_OUT'), 'un caractère vers la console de debug du simulateur']], 'ref'))

A(chapter('C', 'Limites et dépannage', "Ce que la console ne peut pas faire, et que faire quand rien ne marche.", 3))
A(table(['Limite', 'Valeur'], [
  ['Sprites par ligne', '64, et 1280 pixels de sprites'],
  ['Couleurs', '15 par planche ou par décor (4 bits par pixel), 255 en mode 8 bits'],
  ['Décor', '1024 x 1024 pixels (64 x 64 tuiles de 16)'],
  ['Mémoire de travail', '128 Ko pour les variables et la pile'],
  ['ROM', '32 Mo'],
  ['Son', "8 voix d'échantillons + 8 canaux de notes"]]))
A(h3('Messages et problèmes fréquents'))
A(table(['Symptôme', 'Cause probable et remède'], [
  ["<code>error: expected ';'</code>", "il manque un point-virgule à la fin de la ligne indiquée (ou de la précédente)"],
  ["<code>use of undeclared identifier 'sheet'</code>", "l'image n'est pas nommée dans <code>project.txt</code>, ou porte un autre nom"],
  ['<code>ERROR: … 8 at most</code>', "plus de 8 planches et décors : regroupez des dessins dans une même planche"],
  ['<code>ERROR: tool not found: clang</code>', "LLVM n'est pas installé ou pas dans le chemin (chapitre 2)"],
  ['<code>ERROR: no project.txt file</code>', "la commande n'est pas lancée dans le dossier du programme"],
  ['écran noir', "<code>init()</code> oublié, ou le programme n'appelle jamais <code>wait_frame()</code>"],
  ['sprite invisible', "numéro de dessin au-delà du dernier, sprite hors de l'écran, ou plus de 64 sprites sur la ligne"],
  ['couleurs bizarres', "plus de 15 couleurs dans l'image : réduisez la palette"],
  ['le jeu ralentit', "trop de calculs par image : regardez la charge dans le moniteur"],
  ['rien sur le MiSTer', "barrette SDRAM absente, ou essayer l'option « Lecture SDRAM : Retardée »"]]))

A(chapter('D', 'Le SDK mc', "Toutes les fonctions du SDK bas niveau, d'un coup d'oeil (mc.h).", 4))
A(table(['Fonction', 'Description'], [
  [c('mc_init()'), "prépare la console : à appeler une fois, au début"],
  [c('mc_frame()'), "attend l'image suivante, envoie les sprites, lit les manettes, fait avancer les mélodies"],
  [c('mc_frames'), "variable : images depuis le démarrage"],
  [c('mc_cycles() / mc_micros()'), "temps écoulé, en cycles du processeur ou en microsecondes"],
  [c('mc_on_vblank(f)'), "appelle <i>f</i> au début de chaque pause verticale"],
  [c('mc_on_timer(t, hz, f)'), "appelle <i>f</i> <i>hz</i> fois par seconde (timer 0 ou 1)"],
  [c('mc_raster_add(ligne, f)'), "effet raster : <i>f</i> s'applique à partir de cette ligne (64 au plus)"],
  [c('mc_raster_line / mc_raster_clear()'), "ligne de l'effet en cours ; efface tous les effets"],
  [c('mc_video_mode(w, h)'), "taille de l'écran, de 224 à 384 x 192 à 288"],
  [c('mc_display(flags)'), "allume l'affichage, les plans et les sprites (<code>DISP_ON | DISP_LAYER(n) | DISP_SPRITES</code>)"],
  [c('mc_color(i, rgb) / MC_RGB(r, g, b)'), "change la couleur <i>i</i> (0 à 4095)"],
  [c('mc_palette(p, couleurs, n)'), "charge <i>n</i> couleurs à partir de la palette <i>p</i>"],
  [c('mc_background(i)'), "choisit la couleur de fond"],
  [c('mc_layer(n, dessins, flags, w, h, prio)'), "règle le plan <i>n</i> : tuiles 8 ou 16, 4 ou 8 bits, taille de carte, priorité"],
  [c('mc_layer_scroll(n, x, y)'), "fait défiler le plan <i>n</i>"],
  [c('mc_layer_linescroll(n, on) / mc_linescroll(n)'), "défilement ligne par ligne et sa table"],
  [c('mc_tile / mc_tilemap_fill / mc_tilemap_load'), "écrit une case, remplit ou charge la carte d'un plan"],
  [c('MC_TILE(tuile, palette, flags)'), "une case de carte, avec <code>T_HFLIP</code>, <code>T_VFLIP</code>, <code>T_PRIO</code>"],
  [c('mc_chr_upload(décalage, source, octets)'), "copie des dessins dans la CHR RAM"],
  [c('MC_CHR(décalage)'), "adresse de dessins dans la CHR RAM, pour un plan ou les sprites"],
  [c('mc_sprite_sheet(dessins)'), "choisit la planche des sprites (ROM ou CHR RAM)"],
  [c('mc_sprite(n, x, y, case, w, h, pal, prio)'), "affiche le sprite <i>n</i> (0 à 511), taille en cases de 8 pixels"],
  [c('mc_sprite_zoom / mc_sprite_flip / mc_sprite_8bpp'), "agrandit, retourne, passe en 256 couleurs"],
  [c('mc_sprite_hide / mc_sprites_hide_all'), "cache un sprite, ou tous"],
  [c('mc_sprites_used(n)'), "nombre d'entrées parcourues par la console"],
  [c('mc_text_layer(n, prio) / mc_text / mc_text_clear'), "texte avec la police du SDK sur le plan <i>n</i>"],
  [c('mc_psg(c, forme, hz, rapport, vol_g, vol_d)'), "note sur le canal <i>c</i> (<code>PSG_SQUARE</code>, <code>PSG_TRIANGLE</code>, <code>PSG_SAW</code>, <code>PSG_NOISE</code>)"],
  [c('mc_psg_volume / mc_psg_freq16'), "volume, fréquence en 1/16 de hertz"],
  [c('mc_pcm_play(v, données, n, format, hz, boucle, vol_g, vol_d)'), "joue un échantillon sur la voix <i>v</i>"],
  [c('mc_pcm_stop / mc_pcm_pos / mc_master_volume'), "arrête une voix, sa position, volume général"],
  [c('mc_tune(c, notes, images, volume) / mc_tune_stop'), "mélodie en boucle, même écriture que <code>play</code> d'easy"],
  [c('mc_pad(j) / mc_pad_pressed(j) / mc_pad_released(j)'), "boutons tenus, enfoncés ou relâchés à cette image (joueur 1 à 4)"],
  [c('mc_sys() / mc_dip()'), "boutons système, DIP switches"],
  [c('mc_hw_version()'), "version du matériel : 2 et plus pour les joueurs 3 et 4, l'analogique et le clavier"],
  [c('mc_stick_x(j, s) / mc_stick_y(j, s)'), "stick <i>s</i> (0 gauche, 1 droit) du joueur <i>j</i>, de −127 à +127"],
  [c('mc_paddle(j) / mc_spinner(j) / mc_spinner_delta(j)'), "paddle (0 à 255), position du spinner, son déplacement depuis l'image précédente"],
  [c('mc_key(k) / mc_key_pressed(k) / mc_key_released(k)'), "touche <code>KEY_xxx</code> tenue, enfoncée ou relâchée à cette image"],
  [c('mc_key_event()'), "événement clavier suivant (0x200 appui, 0x100 touche étendue, + code), −1 s'il n'y en a plus"],
  [c('malloc / calloc / realloc / free'), "mémoire du tas ; <code>mc_heap_free()</code> donne ce qui reste"],
  [c('MC_NVRAM'), "32 Ko de mémoire sauvegardée"],
  [c('mc_copy / mc_fill'), "copie ou remplissage par DMA"],
  [c('fix16, FIX(x), fix_mul, fix_div'), "nombres à virgule fixe (16 bits après la virgule)"],
  [c('mc_sin / mc_cos / mc_atan2 / mc_isqrt'), "trigonométrie (1024 pas par tour) et racine carrée"],
  [c('mc_rand() / mc_srand(n)'), "nombres au hasard"],
  [c('mc_debug / printf / puts'), "écriture sur la console de debug du simulateur"]], 'ref'))
A(h3('Le fichier project.txt'))
A(table(['Réglage', 'Contenu', 'Noms créés pour le programme'], [
  [c('library = mc'), "choisit le SDK mc (par défaut : easy)", ''],
  [c('data'), "fichiers bruts, copiés tels quels dans la ROM", c('x_file') + ' ' + c('x_size')],
  [c('maps8'), "décors en 256 couleurs (8 bits par pixel, tuiles de 16 x 16)", c('x_chr') + ' ' + c('x_pal') + ' ' + c('x_map') + '…'],
  [c('cflags'), "options de compilation en plus pour les fichiers du programme (chemins d'inclusion, avertissements)", '']], 'ref'))

A('<section class="colophon">' + p("MONOCORE-1 · Manuel du programmeur · édition 3, octobre 2026.") +
  p("Titres en Press Start 2P (Cody Boisclair), texte en Archivo (Omnibus-Type), listings en IBM Plex Mono : polices sous licence SIL Open Font License.") +
  p("Les exemples se trouvent dans <code>manual/examples</code>. Ce manuel se régénère avec <code>python3 manual/tools/manual.py</code>.") + '</section>')

# ---------------------------------------------------------------- table of contents
toc = ['<section class="toc-page"><h2>Sommaire</h2><ol class="toc">']
for num, title in CHAPTERS:
    toc.append('<li><a href="#ch%s"><span class="tn">%s</span><span class="tt">%s</span><span class="tp"></span><span class="pg">@@PG%s@@</span></a></li>' % (num, num, title, num))
toc.append('</ol></section>')
body = '\n'.join(parts).replace('@@TOC@@', '\n'.join(toc))

CSS = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'manual.css'), encoding='utf-8').read()
def page(content):
    return """<!doctype html><html lang="fr"><head><meta charset="utf-8"><title>MONOCORE-1 — Manuel du programmeur</title>
<style>%s</style></head><body>%s</body></html>""" % (CSS, content)

from playwright.sync_api import sync_playwright
FOOT = """<div style="width:100%;font-family:Archivo,sans-serif;font-size:8px;color:#777;padding:0 20mm;display:flex;justify-content:space-between;align-items:center">
<span style="letter-spacing:.15em">MONOCORE-1 · MANUEL DU PROGRAMMEUR</span>
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
# pass 1 finds the page of each chapter, pass 2 writes them into the table of contents
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
w.add_metadata({'/Title': 'MONOCORE-1 - Manuel du programmeur', '/Author': 'MONOCORE-1', '/Subject': 'Programmer la console virtuelle MONOCORE-1'})
with open(OUT_PDF, 'wb') as fo: w.write(fo)
os.remove(TMP_BODY); os.remove(TMP_COVER)
open(OUT_HTML, 'w', encoding='utf-8').write(page(COVER + final))
print('PDF:', OUT_PDF, '-', len(PdfReader(OUT_PDF).pages), 'pages', pages)
