#!/usr/bin/env python3
"""Builds the user's manual of MONOATELIER: printable HTML, then PDF through Chromium.

usage: python3 manual/tools/atelier_manual.py   -> manual/MONOATELIER_Manuel.pdf
Same look as the other manuals (manual.css, mlib.py). The screen shots come from
manual/tools/atelier_shots.py, which goes through the atelier in the simulator.
"""
import os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_HTML = os.path.join(ROOT, 'atelier.html')
OUT_PDF = os.path.join(ROOT, 'MONOATELIER_Manuel.pdf')
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mlib import *

parts = []
A = parts.append

COVER = '''<section class="cover">
 <div class="cover-stripes"><i></i><i></i><i></i><i></i><i></i></div>
 <div class="cover-top">MONOCORE-1 · IMAGES · SONS · MUSIQUE · PROGRAMMES</div>
 <div class="cover-title">MONO<span>ATELIER</span></div>
 <div class="cover-sub">MANUEL DE L'UTILISATEUR</div>
 <div class="cover-shot"><img src="images/atelier_basic_jeu.png"></div>
 <div class="cover-bottom">Dessinez · Faites les bruitages et la musique · Programmez en BASIC · Jouez — tout sur la console</div>
 <div class="cover-ed">ÉDITION 1 · OCTOBRE 2026</div>
</section>'''

A('@@TOC@@')

# ================================================================ 1
A(chapter(1, "L'atelier en un coup d'œil", "Une seule cartouche pour fabriquer un petit jeu de A à Z sur la MONOCORE-1 : "
  "les images, les bruitages, la musique, le programme, et le jeu pour y jouer aussitôt.", 1))
A(p("<b>MONOATELIER</b> réunit dans une seule ROM cinq programmes qui travaillent ensemble. Comme c'est une seule "
    "ROM, la MiSTer leur donne <b>une seule sauvegarde</b> : ce qu'on fabrique dans un outil est aussitôt "
    "disponible pour les autres et pour le jeu. Aucun ordinateur n'est nécessaire entre les étapes."))
A(table(['Touche', 'Programme', 'Pour faire'], [
    [k('F1'), '<b>MONOPIXEL</b>', 'les images : les tuiles et les personnages pixel par pixel, les palettes, les animations, les décors, ce que sont les tuiles (murs, plateformes, eau…) et où sont les objets'],
    [k('F2'), '<b>MONOSFX</b>', 'les bruitages : saut, pièce, explosion… 32 sons par banque, avec des générateurs'],
    [k('F3'), '<b>MONOTRACKER</b>', 'la musique : des morceaux de 16 pistes, au clavier, à la souris ou au clavier MIDI'],
    [k('F4'), '<b>MONOBASIC</b>', 'le programme : un BASIC avec des instructions faites pour les jeux'],
    [k('F5'), '<b>PLATEFORMES</b>', 'un jeu tout fait, qui prend vos images, vos sons et votre musique'],
]))
A(screen('atelier_lanceur_plein.png', "Le lanceur de l'atelier. Chaque colonne montre les 8 emplacements d'un outil ; la flèche rouge marque celui que le jeu et le BASIC prendront."))
A(h2("Le matériel"))
A(ul(["une MiSTer (carte DE10-Nano) avec le core <b>MONOCORE-1 en version 5</b> ou plus récente (" + c('releases/v5_haute_resolution/') + ") : les outils sont en 640 × 480 entrelacé, et un core plus ancien affiche un message ;",
      "une <b>souris USB</b> et un <b>clavier USB</b> : les éditeurs s'utilisent à la souris et au clavier, en AZERTY comme en QWERTY ;",
      "une manette pour jouer (le clavier convient aussi : flèches et Espace) ;",
      "en option, un clavier MIDI USB pour MONOTRACKER."]))
A(h2("Installation"))
A(ol(["Copiez le core " + c('MONOCORE_*.rbf') + " dans " + c('_Console') + " sur la carte SD, et le " + c('boot.rom') + " du même dossier dans " + c('games/MONOCORE') + ".",
      "Copiez " + c('outils/MONOATELIER.mnc') + " dans " + c('games/MONOCORE') + ".",
      "Branchez la souris et le clavier, lancez le core MONOCORE, puis chargez MONOATELIER depuis le menu de la MiSTer (OSD)."]))
A(p("Le dossier " + c('outils/') + " contient aussi chaque programme <b>seul</b> : " + c('MONOPIXEL.mnc') + ", " + c('MONOSFX.mnc') + ", "
    + c('MONOTRACKER.mnc') + ", " + c('MONOBASIC.mnc') + " et " + c('PLATEFORMES.mnc') + ". Ils fonctionnent exactement de la même façon, "
    "mais chacun a sa propre sauvegarde. L'atelier, lui, les partage. Pour reprendre dans l'atelier un travail fait avec un outil seul, "
    "voyez le chapitre 8."))
A(h2("Aller d'un programme à l'autre"))
A(p("Depuis le lanceur, " + k('F1') + " à " + k('F5') + " (ou un clic sur les boutons) ouvrent les programmes. Dans chacun, " + k('F12')
    + " ramène au lanceur. S'il reste des modifications non sauvées, le programme le dit, et il faut appuyer une seconde fois sur "
    + k('F12') + " pour partir quand même. Dans le jeu, " + k('START') + " + " + k('COIN') + " sur la manette fait la même chose."))
A(box('warn', 'Sauver, puis ouvrir le menu', p("Chaque outil sauve dans la NVRAM avec " + k('Ctrl', 'S') + ". La MiSTer n'écrit la NVRAM sur la carte SD "
    "(" + c('saves/MONOCORE/MONOATELIER.sav') + ") que lorsqu'on <b>ouvre le menu (OSD)</b>. Prenez l'habitude : "
    + k('Ctrl', 'S') + ", puis ouvrir et refermer le menu, avant d'éteindre.")))
A(h2("Choisir ce que le jeu prend"))
A(p("Le jeu PLATEFORMES et vos programmes BASIC prennent les images, les bruitages et la musique de l'atelier. Par défaut, c'est le "
    "<b>dernier emplacement sauvé</b> par chaque outil. Un clic sur un emplacement du lanceur le choisit à la place, et la flèche rouge "
    "se déplace. Un second clic revient au choix automatique. Le cadre du bas résume ce que le jeu prendra. S'il manque quelque chose, "
    "le jeu utilise la démonstration qu'il contient."))

# ================================================================ 2
A(chapter(2, "Votre premier jeu en une heure", "Un parcours complet, pas à pas : on part de la démonstration, on la transforme "
  "avec chaque outil, puis on écrit son propre programme.", 2))
A(p("Ce chapitre suppose l'atelier tout neuf : les colonnes du lanceur sont vides et chaque outil démarre sur sa démonstration."))
A(h2("Étape 1 — Jouer à la démo"))
A(p("Dans le lanceur, appuyez sur " + k('F5') + ". PLATEFORMES démarre avec ses propres images. " + k('B1') + " (ou Espace) commence la "
    "partie : gauche et droite pour courir, " + k('B1') + " pour sauter (plus haut en le tenant), bas + " + k('B1') + " pour descendre "
    "d'une plateforme de pierre. Ramassez les pièces, sautez sur les ennemis violets, évitez l'eau, et allez jusqu'au drapeau. "
    + k('F12') + " ramène au lanceur."))
A(screen('atelier_jeu_partie.png', "PLATEFORMES, le jeu tout fait : tout ce qu'on y voit vient d'une banque d'images que vous pouvez refaire.", small=True))
A(h2("Étape 2 — Redessiner le héros"))
A(ol(["Dans le lanceur, " + k('F1') + " ouvre MONOPIXEL sur la page DESSIN, avec la banque de démonstration.",
      "À droite, la <b>planche</b> montre toutes les images. Cliquez sur le petit robot (sous les tuiles) : la zone de 16 × 16 pixels qui le contient s'agrandit à gauche.",
      "Choisissez une couleur dans le nuancier (clic : couleur A, clic droit : couleur B), puis dessinez sur l'image agrandie : clic pour A, clic droit pour B. "
      + k('Ctrl', 'Z') + " annule.",
      "Le robot a quatre images (il marche) : elles sont côte à côte dans la planche. Cliquez sur la suivante pour la retoucher aussi.",
      "Sauvez avec " + k('Ctrl', 'S') + "."]))
A(screen('atelier_pixel_dessin.png', "MONOPIXEL, page DESSIN : la zone agrandie à gauche, les outils au milieu, la planche à droite, le nuancier en bas."))
A(h2("Étape 3 — Changer le niveau"))
A(ol(["Appuyez sur " + k('F3') + " : la page DECORS montre le niveau (à gauche) et la planche (à droite).",
      "Cliquez sur la tuile de briques dans la planche, puis cliquez et glissez sur le niveau pour en poser. Clic droit : effacer.",
      "Les boutons SOLIDE, PLATEF., EAU… disent ce que sont les tuiles. Les briques sont déjà « solides » : on ne les traverse pas.",
      "Appuyez sur " + k('O') + " (outil OBJETS). Choisissez « PIECE » avec les flèches à droite du bouton OBJETS, puis cliquez sur le niveau pour poser des pièces. "
      "Un clic sur un objet le prend pour le déplacer ; un clic droit l'enlève.",
      "Sauvez avec " + k('Ctrl', 'S') + ", puis " + k('F12') + "."]))
A(screen('atelier_pixel_decors.png', "La page DECORS avec l'outil OBJETS : le héros, les pièces, les ennemis et le drapeau sont des objets posés dans le niveau."))
A(h2("Étape 4 — Un nouveau bruit de saut"))
A(ol(["Dans le lanceur, " + k('F2') + " ouvre MONOSFX. La liste à gauche montre les bruitages de la démo : SAUT, PIECE, BOUM…",
      "Cliquez sur SAUT : on l'entend. Cliquez plusieurs fois sur le générateur SAUT en bas : à chaque clic, un nouveau son de saut, tiré au hasard. "
      "Le nom SAUT est gardé.",
      "Affinez avec MUTER ou les compteurs (clic : plus, clic droit : moins), puis " + k('Ctrl', 'S') + " et " + k('F12') + "."]))
A(h2("Étape 5 — La musique"))
A(p(k('F3') + " ouvre MONOTRACKER, qui joue sa démonstration. Pour ce premier parcours, sauvez-la simplement avec " + k('Ctrl', 'S')
    + " : elle sera la musique du jeu. Le chapitre 5 et le manuel de MONOTRACKER expliquent comment composer la vôtre."))
A(h2("Étape 6 — Jouer à votre version"))
A(p("De retour dans le lanceur, les trois colonnes montrent vos sauvegardes, et le cadre du bas indique que le jeu les prendra. "
    + k('F5') + " : votre héros, votre niveau, votre saut, votre musique."))
A(h2("Étape 7 — Écrire le jeu en BASIC"))
A(ol(["Dans le lanceur, " + k('F4') + " ouvre MONOBASIC. Tapez " + c('EXEMPLE') + " puis Entrée : un petit jeu de plateformes de 26 lignes est chargé.",
      "Tapez " + c('RUN') + " (ou " + k('F5') + ") : il utilise vos images et vos sons. " + k('Échap') + " l'arrête.",
      "Tapez " + c('LIST') + " pour le lire. Remontez avec les flèches jusqu'à la ligne 150, remplacez " + c('-76') + " par " + c('-100') + ", puis Entrée : la ligne est corrigée. "
      + c('RUN') + " : le héros saute beaucoup plus haut.",
      "Sauvez votre programme : " + c('SAVE "MONJEU"') + ". La prochaine fois, " + c('LOAD "MONJEU"') + " le reprendra (il est aussi rechargé tout seul au démarrage)."]))
A(screen('atelier_basic_liste.png', "MONOBASIC après EXEMPLE et LIST : le jeu entier tient sur un écran."))
A(box('tip', 'Et maintenant ?', p("Changez la vitesse des ennemis (ligne 180), le nombre de vies (ligne 60), ajoutez un deuxième niveau dans MONOPIXEL "
    "(DECOR 1) ou une nouvelle sorte d'objet. Les chapitres 3 à 6 décrivent chaque outil en détail.")))

# ================================================================ 3
A(chapter(3, "MONOPIXEL : les images", "La planche de tuiles et de personnages, les palettes, les lutins animés, les décors, "
  "ce que sont les tuiles et où sont les objets.", 3))
A(h2("Les mots de MONOPIXEL"))
A(table(['Mot', 'Ce que c\'est'], [
    ['<b>planche</b>', 'toutes les images d\'une banque, en <b>cellules</b> de 8 × 8 pixels : 4 pages de 16 × 16 cellules (128 × 128 pixels chacune)'],
    ['<b>palette</b>', '16 couleurs ; la couleur 0 est toujours transparente. Une banque a 16 palettes'],
    ['<b>tuile</b>', 'un morceau de décor : une cellule (8 pixels) ou un carré de 2 × 2 cellules (16 pixels)'],
    ['<b>lutin</b>', 'un personnage ou un objet : un rectangle de la planche (1 à 8 cellules de côté), une palette, une animation, un <b>nom</b>'],
    ['<b>décor</b>', 'un niveau : une grille de tuiles de 32, 64 ou 128 cases de côté (4096 cases au plus)'],
    ['<b>attribut</b>', 'ce qu\'est une tuile pour le jeu : solide, plateforme, échelle, eau, danger'],
    ['<b>objet</b>', 'un lutin posé dans un décor : le départ du héros, une pièce, un ennemi… avec un paramètre de 0 à 255'],
    ['<b>banque</b>', 'tout cela ensemble, sauvé dans un emplacement'],
]))
A(p("Les pages s'ouvrent par " + k('F1') + " DESSIN, " + k('F2') + " LUTINS, " + k('F3') + " DECORS, " + k('F4') + " BANQUE et "
    + k('F5') + " AIDE, ou par les boutons du haut. " + k('Ctrl', 'S') + " sauve et " + k('Ctrl', 'Z') + " annule la dernière modification "
    "(une seconde fois : la refait)."))
A(h2("La page DESSIN"))
A(p("À gauche, la <b>zone</b> en cours, agrandie (8 à 64 pixels de côté). À droite, la <b>page de la planche</b> : un clic y choisit la zone, "
    "un clic droit y recopie la zone en cours (pratique pour l'image suivante d'une animation). En bas, le nuancier de la palette en cours "
    "et les compteurs R, V, B (0 à 31) de la couleur A."))
A(table(['Souris / touche', 'Rôle'], [
    ['clic, clic droit', 'dessine avec la couleur A, B ; sur le nuancier : choisit A, B ; molette : couleur A ; ' + k('Tab') + ' : couleur suivante'],
    [k('C') + ' ' + k('T') + ' ' + k('R') + ' ' + k('P') + ' ' + k('S') + ' ' + k('I'), 'crayon, trait, cadre, pavé (rectangle plein), seau (remplir), pipette (prendre une couleur)'],
    [k('X') + ' ' + k('Y') + ' ' + k('O'), 'miroir horizontal, vertical, pivoter d\'un quart de tour (zone carrée)'],
    ['flèches', 'déplacent la zone dans la planche ; avec ' + k('Maj') + ' : décalent ses pixels (en boucle)'],
    [k('1') + ' ' + k('2') + ' ' + k('3') + ' ' + k('4'), 'zone de 8, 16, 32 ou 64 pixels (ou les compteurs L et H)'],
    [k('Page préc.') + ' ' + k('Page suiv.'), 'page de la planche (aussi les boutons 1 à 4)'],
    [k('Suppr') + ', ' + k('Ctrl', 'C') + ', ' + k('Ctrl', 'V'), 'efface, copie, colle la zone'],
    [k('G'), 'grille des pixels et des cellules'],
]))
A(h2("La page LUTINS"))
A(screen('atelier_pixel_lutins.png', "La page LUTINS : la liste des 64 lutins, les réglages de celui choisi, son aperçu animé.", small=True))
A(p("Un lutin est ce que le jeu affiche et déplace. On le crée sur un emplacement vide avec NOUVEAU (ou " + k('Entrée') + ") : il prend la zone "
    "de la page DESSIN. PRENDRE lui redonne la zone en cours, DESSINER ramène à son image 0. Les compteurs règlent sa place dans la planche "
    "(COLONNE, LIGNE), sa taille, sa PALETTE, le nombre d'IMAGES de son animation (côte à côte dans la planche) et sa VITESSE (images vidéo par "
    "image ; 0 : immobile). ALLER-RETOUR joue l'animation dans les deux sens. Un clic sur le nom, ou " + k('Entrée') + ", permet de le changer."))
A(box('warn', 'Les noms comptent', p("Le jeu et le BASIC reconnaissent les lutins <b>par leur nom</b> : " + c('HEROS') + ", " + c('PIECE') + ", "
    + c('BLOB') + ", " + c('DRAPEAU') + " pour PLATEFORMES. Gardez ces noms quand vous redessinez, et donnez des noms clairs à vos nouveaux lutins.")))
A(h2("La page DECORS"))
A(p("À gauche, la vue du décor (on le fait défiler au bouton du milieu, aux flèches — " + k('Maj') + " : par 8 tuiles — ou à la molette). "
    "À droite, la planche, où l'on choisit la tuile à poser, ou un bloc de tuiles en glissant."))
A(table(['Outil / réglage', 'Rôle'], [
    ['POSER (' + k('C') + ')', 'clic : pose la tuile ou le bloc ; clic droit : efface'],
    ['PAVE (' + k('P') + '), SEAU (' + k('S') + '), PIPETTE (' + k('I') + ')', 'un rectangle rempli, remplir une zone, prendre la tuile d\'une case'],
    ['H, V, PAL', 'miroirs de la tuile posée, sa palette'],
    ['DECOR, L, H, TUILES', 'le décor 1 à 4, sa largeur et sa hauteur (32, 64, 128 ; 4096 cases au plus), des tuiles de 8 ou 16 pixels'],
    ['SOLIDE, PLATEF., ECHELLE, EAU, DANGER', 'ce que sont les tuiles du pinceau : un mur, une plateforme qu\'on traverse par-dessous, une échelle, de l\'eau, un danger (piques, lave…)'],
    [k('A'), 'montre ou cache les attributs sur la planche : ' + c('#') + ' solide, ' + c('=') + ' plateforme, ' + c('H') + ' échelle, ' + c('~') + ' eau, ' + c('!') + ' danger'],
    ['OBJETS (' + k('O') + ')', 'pose le lutin choisi (flèches à droite du bouton) sur la grille ; un clic sur un objet le prend (on le glisse) ; clic droit ou ' + k('Suppr') + ' : l\'enlève ; P : son paramètre'],
    ['TOUT EFFACER', 'vide le décor et ses objets (après confirmation)'],
]))
A(p("On peut poser 64 objets par décor. Le paramètre P sert à varier les objets d'une même sorte : dans PLATEFORMES, c'est la vitesse de chaque ennemi."))
A(h2("La page BANQUE"))
A(screen('atelier_pixel_banque.png', "La page BANQUE : les 8 emplacements de MONOPIXEL dans l'atelier.", small=True))
A(p("Elle montre les 8 emplacements, le nom de la banque (un clic ou " + k('Entrée') + " pour le changer), sa taille compacte et la place libre. "
    "Les boutons sont SAUVER (" + k('Ctrl', 'S') + "), CHARGER (" + k('Ctrl', 'L') + "), EFFACER l'emplacement (" + k('Ctrl', 'D') + "), NOUVELLE banque, "
    "DEMO et la disposition du CLAVIER (AZERTY ou QWERTY, commune à tout l'atelier). Les actions qui feraient perdre du travail demandent un second clic."))

# ================================================================ 4
A(chapter(4, "MONOSFX : les bruitages", "Des sons courts calculés soixante fois par seconde : un saut, une pièce, une explosion. "
  "Des générateurs les fabriquent en un clic.", 4))
A(screen('atelier_sfx_effets.png', "MONOSFX : la liste des bruitages, la courbe du son choisi (la note en bleu, le volume en orange), ses réglages et les générateurs."))
A(p("Une banque contient 32 bruitages. Un clic dans la liste choisit un bruitage et le joue, et " + k('Espace') + " le rejoue. Avec ÉCOUTE AUTO, "
    "chaque changement s'entend aussitôt. " + k('Entrée') + " ou un clic sur le nom permet de le renommer : le jeu et le BASIC jouent les sons <b>par "
    "leur nom</b> (" + c('SON "SAUT"') + ")."))
A(table(['Réglage', 'Effet'], [
    ['CARRE, TRIANGLE, DENTS, BRUIT', 'l\'onde : franche, douce, brillante, ou un souffle (explosions, coups)'],
    ['FREQ HZ, GLISSE, ACCEL', 'la note de départ, ce qu\'elle monte ou descend à chaque image (en 1/16 de Hz), et la courbure de cette pente'],
    ['ATTAQUE, TENUE, CHUTE', 'le volume monte, reste, puis descend : la durée est leur somme, en images (60 par seconde)'],
    ['VOLUME', '0 à 255'],
    ['RAPPORT, BALAYAGE', 'la forme du carré (128 : moitié) et son changement à chaque image'],
    ['VIBR. HZ, VIBR. IMG', 'un vibrato : son écart et sa période'],
    ['ARPEGE, A L\'IMAGE', 'un saut de quelques demi-tons à une image donnée : le « ding-ding » d\'une pièce'],
    ['REPETE', 'la note repart toutes les n images (bonus, alarmes)'],
]))
A(p("Les générateurs SAUT, PIECE, TIR, BOUM, BONUS et AIE fabriquent un son de ce genre au hasard. HASARD fait n'importe quoi, et MUTER "
    "change un peu le son en cours. La bonne méthode : cliquer plusieurs fois sur un générateur jusqu'à un son proche de l'idée, l'affiner, puis le nommer. "
    "Les compteurs se règlent au clic (plus), au clic droit (moins), avec " + k('Maj') + " par 10, ou à la molette. "
    + k('Ctrl', 'C') + " et " + k('Ctrl', 'V') + " copient et collent un bruitage, " + k('Suppr') + " l'efface. La page " + k('F2') + " BANQUE fonctionne comme celle de MONOPIXEL."))

# ================================================================ 5
A(chapter(5, "MONOTRACKER : la musique", "Le tracker de la MONOCORE-1 : seize pistes, des sons synthétiques et des échantillons, "
  "au clavier, à la souris ou au clavier MIDI.", 5))
A(screen('tracker_motifs.png', "MONOTRACKER, page des motifs, pendant la lecture de la démonstration.", small=True))
A(p("MONOTRACKER a son propre manuel complet, " + c('manual/MONOTRACKER_Manuel.pdf') + ". Dans l'atelier, il fonctionne de la même façon, "
    "avec quatre pages : " + k('F1') + " MOTIFS (les notes), " + k('F2') + " SONS (les instruments), " + k('F3') + " MORCEAU (l'ordre des motifs, "
    "le tempo et les 8 emplacements) et " + k('F4') + " AIDE. Le morceau sauvé (" + k('Ctrl', 'S') + ") devient la musique du jeu et du BASIC "
    "(instruction " + c('MUSIQUE') + ")."))
A(box('tip', 'Une seule différence', p("Seul, MONOTRACKER utilise " + k('F12') + " pour passer à l'instrument suivant. Dans l'atelier, " + k('F12')
    + " ramène au lanceur, et l'instrument suivant se choisit avec le " + k('+') + " du pavé numérique (" + k('F11') + " ou le " + k('−') + " du pavé : le précédent).")))
A(p("Le jeu laisse les canaux PSG 6 et 7 aux bruitages : la musique de l'atelier se joue sur les autres pistes. Une musique qui en a besoin "
    "perd ces deux pistes pendant le jeu."))

# ================================================================ 6
A(chapter(6, "MONOBASIC : programmer", "Un BASIC comme sur les ordinateurs des années 80, avec des instructions en français "
  "pour les objets, les collisions, les sons et la musique.", 6))
A(h2("L'éditeur"))
A(p("L'écran est un éditeur plein écran, à la manière du MO5 ou du Commodore 64. On tape une ligne puis " + k('Entrée') + ". Une ligne qui commence "
    "par un <b>numéro</b> (" + c('120 PRINT "BONJOUR"') + ") est rangée dans le programme, à sa place ; un numéro seul efface sa ligne. "
    "Une ligne sans numéro est exécutée tout de suite. Les flèches déplacent le curseur partout : on remonte sur une ligne listée, on la corrige, "
    "puis " + k('Entrée') + " la range à nouveau. " + k('Échap') + " efface la ligne du curseur et arrête un programme en cours."))
A(table(['Commande', 'Rôle'], [
    [c('RUN') + ' [ligne], ' + k('F5'), 'lance le programme (depuis le début, ou cette ligne)'],
    [c('LIST') + ' [a][-b], ' + k('F2'), 'montre le programme, ou les lignes a à b'],
    [c('NEW'), 'efface le programme'],
    [c('SAVE "NOM"') + ', ' + c('LOAD "NOM"') + ', ' + c('LOAD 3') + ', ' + c('DIR'), 'les 8 programmes de l\'atelier ; le dernier sauvé est rechargé au démarrage'],
    [c('AIDE'), 'le résumé de toutes les instructions, à l\'écran'],
    [c('EXEMPLE'), 'le petit jeu de plateformes, à lire et à modifier'],
    [k('F12'), 'retour à l\'atelier'],
]))
A(screen('atelier_basic_aide.png', "L'aide-mémoire de MONOBASIC (commande AIDE), après l'arrêt d'un programme par Échap.", small=True))
A(h2("Le langage"))
A(p("Les nombres sont des <b>entiers</b>. Un nom de variable fait jusqu'à 8 lettres et chiffres (" + c('A') + ", " + c('SCORE') + ", " + c('VX')
    + ") ; " + c('NOM$') + " est une chaîne (80 caractères au plus). Plusieurs instructions vont sur une ligne, séparées par " + c(':') + ". "
    "Les mots clés ne sont reconnus que comme mots entiers : " + c('TOTAL') + " est une variable."))
A(table(['Instruction', 'Rôle'], [
    [c('PRINT a; b, c'), c(';') + ' colle, ' + c(',') + ' va à la colonne suivante, un ' + c(';') + ' final garde la ligne'],
    [c('A = 3') + ', ' + c('A$ = "TEXTE" + B$') + ', ' + c('T(I) = 5'), 'affectations (' + c('LET') + ' est facultatif)'],
    [c('IF c THEN … ELSE …'), 'sur une seule ligne ; ' + c('THEN 120') + ' saute à la ligne 120'],
    [c('FOR I = 1 TO 10 STEP 2') + ' … ' + c('NEXT I'), 'une boucle comptée'],
    [c('WHILE c') + ' … ' + c('WEND'), 'une boucle tant que c est vrai'],
    [c('GOTO n') + ', ' + c('GOSUB n') + ' … ' + c('RETURN'), 'sauter, appeler un sous-programme'],
    [c('DIM T(100)'), 'un tableau de 0 à 100 (sans DIM : 0 à 10)'],
    [c('END') + ', ' + c('STOP') + ', ' + c('REM') + ', ' + c('CLS'), 'fin, arrêt avec son numéro de ligne, commentaire, effacer le texte'],
    [c('+ - * / MOD') + ', ' + c('= <> < > <= >=') + ', ' + c('AND OR NOT'), 'calculs, comparaisons et logique (vrai vaut 1, faux 0)'],
    [c('RND(n)') + ', ' + c('ABS(n)') + ', ' + c('LEN(A$)') + ', ' + c('STR$(n)') + ', ' + c('CHR$(n)'), 'hasard de 0 à n−1, valeur absolue, longueur, nombre en texte, caractère'],
]))
A(h2("Les instructions de jeu"))
A(p(c('DECOR n') + " affiche le décor n de la banque d'images de l'atelier et crée ses <b>objets</b>, numérotés de 0 à " + c('OBJETS - 1')
    + " dans l'ordre où ils ont été posés dans MONOPIXEL. On reconnaît un objet au nom de son lutin, et on lui parle par son numéro."))
A(table(['Instruction / fonction', 'Rôle'], [
    [c('CHERCHE("HEROS")') + ', ' + c('CHERCHE("PIECE", I)'), 'le premier objet de ce lutin, ou le suivant après I (−1 : aucun)'],
    [c('EST(I, "BLOB")') + ', ' + c('NOM$(I)') + ', ' + c('ACTIF(I)'), 'ce qu\'est l\'objet I, s\'il existe encore'],
    [c('CREE("TIR", X, Y)'), 'un nouvel objet ; donne son numéro'],
    [c('POSX(I)') + ', ' + c('POSY(I)') + ', ' + c('VITX(I)') + ', ' + c('VITY(I)'), 'sa position en pixels, sa vitesse en 1/16 de pixel par image'],
    [c('PLACE I, X, Y') + ', ' + c('VITESSE I, VX, VY'), 'les changer'],
    [c('GRAVITE I, 1') + ', ' + c('BOITE I, X, Y, L, H'), 'il tombe ; sa boîte de collision (depuis le coin de son lutin)'],
    [c('BOUGE I') + ' (' + c('BOUGE') + ' : tous)', 'le déplace selon sa vitesse, arrêté par les murs, porté par les plateformes'],
    [c('PATROUILLE I, V'), 'le fait marcher, avec demi-tour aux murs et au bord du vide'],
    [c('SOL(I)') + ', ' + c('PLAFOND(I)') + ', ' + c('MUR(I)'), 'ce que son dernier mouvement a touché'],
    [c('DANGER(I)') + ', ' + c('EAU(I)') + ', ' + c('ECHELLE(I)'), 'une tuile de cet attribut est sous lui'],
    [c('CHOC(I, J)'), 'les objets I et J se touchent'],
    [c('ENLEVE I') + ', ' + c('CACHE I, 1') + ', ' + c('MIROIR I, 1') + ', ' + c('ANIME I, 1') + ', ' + c('IMAGE I, N'), 'disparaître, ne plus être dessiné, regarder à gauche, s\'animer, montrer l\'image N'],
    [c('CAMERA I'), 'la caméra le suit dans le décor'],
    [c('GAUCHE') + ', ' + c('DROITE') + ', ' + c('HAUT') + ', ' + c('BAS') + ', ' + c('BOUTON(N)') + ', ' + c('APPUI(N)'), 'la manette 1 (et les flèches, Espace = bouton 1) ; APPUI : le bouton vient d\'être enfoncé'],
    [c('ATTENDS') + ' [n]', 'dessine les objets et attend l\'image suivante : une fois par tour de boucle'],
    [c('TEXTE X, Y, …') + ', ' + c('PRINT'), 'écrire sur l\'écran du jeu (40 colonnes, 30 lignes)'],
    [c('SON "SAUT"') + ', ' + c('MUSIQUE') + ', ' + c('SILENCE'), 'un bruitage par son nom, la musique de l\'atelier, le silence'],
    [c('FOND R, V, B') + ', ' + c('TEMPS'), 'la couleur du ciel ; les images écoulées depuis RUN'],
]))
A(h2("La forme d'un jeu"))
A(p("Presque tous les jeux ont la même forme : une préparation, puis une <b>boucle</b> qui lit la manette, déplace les objets, regarde ce qui se touche, "
    "puis dessine une image avec " + c('ATTENDS') + " et recommence. Voici le cœur de " + c('EXEMPLE') + " :"))
A(listing('''20 DECOR 0 : MUSIQUE : FOND 120, 200, 240
30 H = CHERCHE("HEROS") : GRAVITE H, 1 : BOITE H, 3, 2, 10, 14
110 VX = 0
120 IF GAUCHE THEN VX = -28 : MIROIR H, 1
130 IF DROITE THEN VX = 28 : MIROIR H, 0
140 VITESSE H, VX, VITY(H)
150 IF SOL(H) AND APPUI(1) THEN VITESSE H, VX, -76 : SON "SAUT"
160 BOUGE H : ANIME H, VX <> 0
170 FOR I = 0 TO OBJETS - 1
190 IF EST(I, "PIECE") THEN IF CHOC(H, I) THEN ENLEVE I : S = S + 10 : SON "PIECE"
210 NEXT I
230 CAMERA H : TEXTE 1, 1, "SCORE "; S
240 ATTENDS : GOTO 110''', 'les lignes essentielles du jeu EXEMPLE', numbers=False, lang='basic'))
A(screen('atelier_basic_jeu.png', "Le jeu EXEMPLE en marche, avec les images, les sons et la musique de l'atelier.", small=True))
A(h2("Les erreurs"))
A(p("Une erreur arrête le programme et s'affiche avec sa ligne, suivie de la ligne elle-même : " + c('? SYNTAXE EN 150') + ". Les plus fréquentes sont "
    + c('SYNTAXE') + " (un mot mal écrit, une virgule oubliée), " + c('LIGNE INEXISTANTE') + " (un GOTO vers une ligne absente), " + c('OBJET INEXISTANT')
    + " (un numéro d'objet trop grand : vérifiez " + c('CHERCHE') + ", qui donne −1 si le lutin n'est pas dans le décor), " + c('PAS DE DECOR')
    + " (une instruction de jeu avant " + c('DECOR') + ") et " + c('TYPE') + " (un nombre là où il faut une chaîne, ou l'inverse). " + c('ARRET')
    + " signale simplement un arrêt par " + k('Échap') + "."))
A(p("MONOBASIC n'a ni nombres à virgule, ni " + c('INPUT') + ", ni " + c('DATA') + " / " + c('READ') + ". Un programme fait au plus 20 Ko."))

# ================================================================ 7
A(chapter(7, "PLATEFORMES : le jeu tout fait", "Un vrai jeu dont tout le contenu vient de l'atelier : on le refait sans écrire "
  "une ligne, rien qu'en dessinant.", 7))
A(screen('atelier_jeu_titre.png', "L'écran titre de PLATEFORMES dans l'atelier.", small=True))
A(p("PLATEFORMES prend le <b>décor 0</b> de la banque d'images, ses attributs et ses objets, les bruitages et la musique choisis dans le lanceur. "
    "Il reconnaît les objets par le nom de leur lutin :"))
A(table(['Lutin', 'Rôle dans le jeu'], [
    [c('HEROS'), 'le joueur : il court, saute (plus haut en tenant ' + k('B1') + '), descend des plateformes (bas + ' + k('B1') + ') ; l\'eau, une tuile DANGER ou une chute lui font perdre une vie'],
    [c('PIECE'), '10 points'],
    [c('BLOB'), 'un ennemi qui marche et fait demi-tour au bord du vide ; sa vitesse est le paramètre P de l\'objet ; sauté dessus, il disparaît (50 points)'],
    [c('DRAPEAU'), 'la fin du niveau'],
]))
A(p("Les bruitages joués sont " + c('SAUT') + ", " + c('PIECE') + ", " + c('ECRASE') + ", " + c('AIE') + ", " + c('BRAVO') + ", " + c('PERDU') + " et " + c('MENU')
    + ". Un nom absent est simplement muet. Le jeu a 3 vies et garde le record dans la sauvegarde de l'atelier."))
A(box('tip', 'Un niveau à soi', p("Dans MONOPIXEL : dessinez le décor 0, réglez les attributs (SOLIDE pour les murs et le sol, PLATEF. pour ce qu'on traverse "
    "en sautant, EAU ou DANGER pour ce qui fait perdre une vie), posez un HEROS, des PIECE, des BLOB (P = vitesse) et un DRAPEAU. Sauvez, F12, F5.")))

# ================================================================ 8
A(chapter(8, "Sauvegardes et échanges", "Où vont les créations, comment les passer à l'ordinateur, les reprendre des outils seuls, "
  "et les mettre dans un jeu écrit en C.", 8))
A(h2("La sauvegarde de l'atelier"))
A(p("Tout est dans la NVRAM de 32 Ko, écrite sur la carte SD sous " + c('saves/MONOCORE/MONOATELIER.sav') + " (le nom suit celui du fichier de la ROM) "
    "quand on ouvre le menu de la MiSTer. Chaque outil y a 8 emplacements, et le lanceur affiche la place libre. Pour se faire une idée, la banque d'images de la "
    "démonstration fait 6 Ko, son morceau 4 Ko, ses bruitages 352 octets et le programme EXEMPLE un peu plus d'un Ko."))
A(h2("Sur l'ordinateur"))
A(p("Les outils Python du dépôt lisent et écrivent cette sauvegarde :"))
A(shell('''python3 tools/atelier/atelier.py MONOATELIER.sav                  # ce qu'elle contient
python3 tools/atelier/atelier.py MONOATELIER.sav --export jeu/      # images_1.mgf, bruits_1.mfx, musique_1.mts, programme_1.mbs
python3 tools/atelier/atelier.py MONOATELIER.sav --import MONOPIXEL.sav MONOSFX.sav MONOTRACKER.sav
python3 tools/basic/basic.py MONOATELIER.sav 1 -o jeu.bas            # un programme BASIC en texte
python3 tools/basic/basic.py jeu.bas --to-sav MONOATELIER.sav 2      # et dans l'autre sens
python3 tools/mgfx/mgfx.py MONOATELIER.sav 1 --png images/            # la planche, les lutins (GIF animés), les décors''', 'les outils de l\'atelier'))
A(p(c('--import') + " reprend dans l'atelier ce qui a été fait avec les outils seuls (" + c('outils/MONOPIXEL.mnc') + "…), aux mêmes numéros d'emplacement. "
    "Les outils " + c('mgfx.py') + ", " + c('msfx.py') + ", " + c('sav2song.py') + " et " + c('basic.py') + " lisent aussi bien la sauvegarde de l'atelier que celle d'un outil seul."))
A(h2("Dans le simulateur"))
A(p("Le simulateur (" + c('dist/monocore.html') + ", à ouvrir dans un navigateur) contient MONOATELIER et chaque outil dans sa liste de ROM. "
    "La souris et le clavier de la console s'y activent tout seuls (" + k('F9') + " rend le clavier au navigateur). <b>Exporter .sav</b> et <b>Importer .sav…</b> "
    "échangent la sauvegarde avec la MiSTer, et <b>Morceaux .mts</b>, <b>Banques .mgf</b>, <b>Bruitages .mfx</b> téléchargent directement les créations."))
A(h2("Dans un jeu écrit en C"))
A(p("Les mêmes formats servent aux jeux écrits avec le SDK. Les modules sont " + c('mgfx') + " (les banques d'images, " + c('sdk/MGFX.md') + "), "
    + c('mjeu') + " (objets, collisions, caméra, " + c('sdk/MJEU.md') + "), " + c('msfx') + " (les bruitages, " + c('sdk/MSFX.md') + ") et " + c('mtrack')
    + " (la musique). " + c('examples/plateformes') + " est le modèle d'un tel jeu, et " + c('sdk/ATELIER.md') + " explique comment ajouter son jeu à l'atelier "
    "(" + c('tools/mkatelier.py') + ")."))

# ================================================================ 9
A(chapter(9, "Aide-mémoire et dépannage", "Les raccourcis de chaque programme sur une page, et que faire quand quelque chose "
  "ne marche pas.", 9))
A(table(['Programme', 'Raccourcis'], [
    ['<b>Partout</b>', k('F12') + ' : lanceur ; ' + k('Ctrl', 'S') + ' : sauver (puis ouvrir le menu de la MiSTer) ; ' + k('Ctrl', 'Z') + ' : annuler (MONOPIXEL)'],
    ['<b>Lanceur</b>', k('F1') + ' images, ' + k('F2') + ' bruitages, ' + k('F3') + ' musique, ' + k('F4') + ' BASIC, ' + k('F5') + ' ou ' + k('Entrée') + ' jouer ; clic sur un emplacement : le donner au jeu'],
    ['<b>MONOPIXEL</b>', k('F1') + '–' + k('F5') + ' pages ; dessin : ' + k('C') + ' ' + k('T') + ' ' + k('R') + ' ' + k('P') + ' ' + k('S') + ' ' + k('I') + ', ' + k('X') + ' ' + k('Y') + ' ' + k('O') + ', ' + k('1') + '–' + k('4') + ', ' + k('G') + ', flèches ; décors : ' + k('C') + ' ' + k('P') + ' ' + k('S') + ' ' + k('I') + ' ' + k('O') + ', ' + k('H') + ' ' + k('V') + ', ' + k('A')],
    ['<b>MONOSFX</b>', k('F1') + ' effets, ' + k('F2') + ' banque, ' + k('F3') + ' aide ; ' + k('Espace') + ' jouer ; ' + k('Entrée') + ' nommer ; ' + k('Ctrl', 'C') + ' ' + k('Ctrl', 'V') + ' ' + k('Suppr')],
    ['<b>MONOTRACKER</b>', k('F1') + ' motifs, ' + k('F2') + ' sons, ' + k('F3') + ' morceau, ' + k('F4') + ' aide ; instrument : ' + k('+') + ' et ' + k('−') + ' du pavé ; voir son manuel'],
    ['<b>MONOBASIC</b>', k('F5') + ' RUN, ' + k('F2') + ' LIST, ' + k('Échap') + ' arrêt ; ' + c('AIDE') + ', ' + c('EXEMPLE') + ', ' + c('SAVE') + ', ' + c('LOAD') + ', ' + c('DIR')],
    ['<b>PLATEFORMES</b>', 'gauche/droite, ' + k('B1') + ' sauter, bas + ' + k('B1') + ' descendre ; ' + k('START') + ' + ' + k('COIN') + ' : lanceur'],
]))
A(h2("Dépannage"))
A(table(['Ce qui se passe', 'Que faire'], [
    ['« demande le core version 5 »', 'mettre le core de ' + c('releases/v5_haute_resolution/') + ' (ou plus récent) dans ' + c('_Console')],
    ['les créations ont disparu après avoir éteint', 'la NVRAM n\'est écrite sur la carte SD qu\'à l\'ouverture du menu : ' + k('Ctrl', 'S') + ', puis ouvrir et fermer le menu'],
    ['les lettres A et Q (ou Z et W) sont inversées', 'changer CLAVIER (AZERTY / QWERTY) sur la page BANQUE de MONOPIXEL ou de MONOSFX : le réglage vaut pour tout l\'atelier'],
    ['pas de pointeur de souris', 'brancher la souris avant de lancer le core ; dans le simulateur, cocher « souris de la console »'],
    ['« NVRAM PLEINE »', 'effacer des emplacements inutiles (page BANQUE, ' + c('DIR') + ' du BASIC), ou exporter les créations sur l\'ordinateur'],
    ['le jeu n\'affiche pas mon héros', 'le lutin doit s\'appeler exactement ' + c('HEROS') + ' et un objet HEROS doit être posé dans le décor 0'],
    ['un son ne joue pas', 'son nom doit être exactement celui que le jeu attend (' + c('SAUT') + '…), et la banque de bruitages doit être sauvée'],
    ['le jeu prend la démo au lieu de mes créations', 'regarder le cadre « LE JEU PREND » du lanceur ; cliquer sur l\'emplacement voulu'],
    ['MONOBASIC : ' + c('OBJET INEXISTANT'), c('CHERCHE') + ' a donné −1 : le lutin n\'est pas dans le décor, ou son nom diffère'],
]))
A(p("Pour aller plus loin, la documentation technique se trouve dans le dépôt : " + c('sdk/ATELIER.md') + ", " + c('sdk/MGFX.md') + ", "
    + c('sdk/MJEU.md') + ", " + c('sdk/MSFX.md') + ", " + c('examples/monobasic/LISEZMOI.md') + ", ainsi que le manuel du programmeur "
    "(" + c('manual/MONOCORE-1_Manuel_du_programmeur.pdf') + ")."))

build(parts, COVER, OUT_PDF, OUT_HTML, "MONOATELIER - Manuel de l'utilisateur", "MONOATELIER · MANUEL DE L'UTILISATEUR",
      "L'atelier de la MONOCORE-1 : images, bruitages, musique, BASIC")
