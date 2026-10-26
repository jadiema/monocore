#!/usr/bin/env python3
"""Builds the guide « Brancher un clavier MIDI sur la MONOCORE-1 »: manual/MONOCORE-1_Guide_MIDI.pdf.

usage: python3 manual/tools/midi_guide.py      (same look as the other manuals, see mlib.py)
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mlib import *

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_HTML = os.path.join(ROOT, 'midi.html')
OUT_PDF = os.path.join(ROOT, 'MONOCORE-1_Guide_MIDI.pdf')
parts = []
A = parts.append

def fig_chain(rows, caption):
    """boxes linked by arrows, one row per machine"""
    g, y = [], 10
    colours = ['#e23b2e', '#f28c1b', '#f2c81b', '#3aa655', '#2f6fd6']
    for label, items in rows:
        g.append('<text x="0" y="%d" font-size="12" font-weight="700" fill="#1d1b18">%s</text>' % (y + 26, esc(label)))
        x = 92
        for i, (title, sub) in enumerate(items):
            w = 104
            g.append('<rect x="%d" y="%d" width="%d" height="44" rx="5" fill="#fbf6e8" stroke="%s" stroke-width="2.5"/>' % (x, y, w, colours[i % 5]))
            g.append('<text x="%d" y="%d" font-size="11" font-weight="700" text-anchor="middle" fill="#1d1b18">%s</text>' % (x + w / 2, y + 19, esc(title)))
            g.append('<text x="%d" y="%d" font-size="8.5" text-anchor="middle" fill="#5d574c">%s</text>' % (x + w / 2, y + 34, esc(sub)))
            if i < len(items) - 1:
                g.append('<line x1="%d" y1="%d" x2="%d" y2="%d" stroke="#2c2a25" stroke-width="2" marker-end="url(#ar)"/>' % (x + w + 3, y + 22, x + w + 19, y + 22))
            x += w + 24
        y += 64
    return ('<figure class="fig"><svg viewBox="0 0 740 %d" xmlns="http://www.w3.org/2000/svg" font-family="Archivo" style="width:165mm">'
            '<defs><marker id="ar" markerWidth="8" markerHeight="8" refX="6" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="#2c2a25"/></marker></defs>%s</svg>'
            '<figcaption>%s</figcaption></figure>') % (y, ''.join(g), caption)

COVER = '''<section class="cover">
 <div class="cover-stripes"><i></i><i></i><i></i><i></i><i></i></div>
 <div class="cover-top">MONOCORE-1 · ENTRÉE MIDI · MATÉRIEL VERSION 4</div>
 <div class="cover-title">GUIDE<span> MIDI</span></div>
 <div class="cover-sub">BRANCHER UN CLAVIER MIDI SUR LA CONSOLE</div>
 <div class="cover-shot"><img src="images/midi_tracker.png"></div>
 <div class="cover-bottom">Sur la MiSTer avec MidiLink · Dans le simulateur avec Web MIDI · Dans vos propres ROM</div>
 <div class="cover-ed">ÉDITION 1 · OCTOBRE 2026</div>
</section>'''

A('@@TOC@@')
A('<section class="foreword">' + h2('En bref') +
  p("Depuis la version 4 de son matériel, la MONOCORE-1 a une <b>entrée MIDI</b>. Un clavier MIDI USB, comme un Novation Launchkey, peut ainsi jouer et écrire de la musique dans <b>MONOTRACKER</b>, et toute ROM peut lire ses notes. Ce guide explique comment le brancher, sur la MiSTer comme dans le simulateur, et quoi vérifier si rien ne se passe.") +
  table(['Il vous faut', 'Détail'], [
    ['Le core MONOCORE en version 4', c('releases/MONOCORE_20261026c_midi.rbf') + " ou plus récent, copié dans " + c('_Console')],
    ['Un clavier MIDI USB', "un clavier « class compliant », qui fonctionne sans pilote : c'est le cas de la plupart, dont les Launchkey"],
    ['Une MiSTer à jour', "avec MidiLink, le service MIDI de la MiSTer, installé par l'updater habituel ; rien à acheter"],
    ['Une ROM qui lit le MIDI', "MONOTRACKER à jour (" + c('monotracker.mnc') + "), ou votre propre ROM (chapitre 4)"]]) +
  box('know', 'Le chemin des notes', p("Sur la MiSTer, le clavier se branche en USB sur la carte. Linux le reconnaît, MidiLink lit ses notes et les envoie au core par sa liaison série, à 31 250 bauds comme une prise MIDI classique. Le core les range dans le registre " + c('MIDI_IN') + ". Dans le simulateur, c'est le navigateur qui lit le clavier (Web MIDI) et remplit ce même registre.")) +
  '</section>')

# ---------------------------------------------------------------- 1
A(chapter(1, "Sur la MiSTer", "Brancher le clavier, régler le menu du core, vérifier.", 1))
A(fig_chain([('MiSTer', [('Clavier MIDI', 'USB'), ('Linux', 'pilote ALSA'), ('MidiLink', 'mode USB'), ('Core', 'UART 31 250 bauds'), ('ROM', 'registre MIDI_IN')])],
            "Sur la MiSTer, les notes passent du clavier USB à la ROM par Linux, MidiLink et la liaison série du core."))
A(h2('Les étapes'))
A(ol([
  "Copiez le core version 4, " + c('MONOCORE_20261026c_midi.rbf') + ", dans " + c('_Console') + " sur la carte SD, et le " + c('monotracker.mnc') + " à jour dans " + c('games/MONOCORE') + ".",
  "Branchez le clavier MIDI sur un port USB <b>de la MiSTer</b> (directement ou par le hub de la carte), et attendez que ses voyants s'allument.",
  "Lancez le core MONOCORE depuis le menu de la MiSTer.",
  "Ouvrez le menu du core avec " + k('F12') + ", puis passez à la page générale de la MiSTer avec la flèche " + k('→') + " (ou " + k('←') + ") : c'est celle des réglages communs à tous les cores.",
  "Choisissez <b>UART Mode</b>, réglez <b>Connection</b> sur <b>MIDI</b>, puis <b>MidiLink</b> sur <b>USB</b>. Enregistrez si le menu le propose.",
  "Refermez le menu, lancez MONOTRACKER et jouez une note : le bandeau du tracker affiche <i>CLAVIER MIDI BRANCHE</i>, et la note sonne."]))
A(screen('midi_tracker.png', "MONOTRACKER reçoit le clavier : le bandeau l'annonce, et les trois notes jouées en mode édition se sont écrites avec leur volume."))
A(h2('Les choix de MidiLink'))
A(table(['Choix', 'À quoi il sert', 'Pour un clavier ?'], [
  ['USB', "échange les notes avec un appareil MIDI branché en USB sur la MiSTer", '<b>oui</b>'],
  ['Local', "fait jouer la musique par un synthétiseur logiciel de la MiSTer (MUNT, FluidSynth)", 'non'],
  ['UDP', "envoie et reçoit le MIDI par le réseau, vers un autre ordinateur", 'non']]))
A(box('warn', 'Le choix USB n\'apparaît pas', p("La MiSTer ne propose <b>USB</b> que si elle a reconnu un appareil MIDI. Si vous ne voyez que <i>Local</i> et <i>UDP</i>, elle ne voit pas encore le clavier : branchez-le sur la MiSTer (pas sur le PC), attendez quelques secondes et rouvrez le menu ; sinon, redémarrez la MiSTer avec le clavier déjà branché. Le chapitre 3 donne les vérifications suivantes.")))
A(p("Le réglage UART est gardé par la MiSTer pour le core MONOCORE : il n'y a pas à le refaire à chaque fois, tant que le clavier est branché."))

# ---------------------------------------------------------------- 2
A(chapter(2, "Dans le simulateur", "Le même clavier, branché sur le PC.", 2))
A(fig_chain([('PC', [('Clavier MIDI', 'USB'), ('Navigateur', 'Web MIDI'), ('Simulateur', 'monocore.html'), ('ROM', 'registre MIDI_IN')])],
            "Dans le simulateur, c'est le navigateur qui lit le clavier et le transmet à la console simulée."))
A(h2('Avec Chrome ou Edge'))
A(ol([
  "Branchez le clavier sur le PC et ouvrez le simulateur (" + c('dist/monocore.html') + ").",
  "Choisissez MONOTRACKER dans la liste des ROM intégrées et cliquez sur <b>Mettre sous tension</b>.",
  "Sous l'écran, dans l'encadré « Système et manettes », cochez <b>clavier MIDI</b>. Le navigateur demande l'autorisation la première fois.",
  "Le nom du clavier s'affiche à côté de la case. Quand vous jouez, un compteur et le dernier message reçu s'y ajoutent, et la console de débogage note la première note reçue."]))
A('<figure class="fig"><img src="images/midi_page.png" style="width:135mm;border:1px solid #2c2a25"><figcaption>La case « clavier MIDI » cochée : les deux ports du Launchkey, le nombre de messages reçus et le dernier (90 43 64 : un sol joué).</figcaption></figure>')
A(h2('Avec Firefox'))
A(p("Firefox ne donne pas le MIDI à une page ouverte directement depuis un fichier : la case affiche alors une explication dans la console de débogage. Ouvrez plutôt le simulateur par " + c('dist/simulateur.bat') + " (Windows, double-clic) ou " + c('dist/simulateur.py') + " : ils servent la page en local, à une adresse " + c('http://localhost:…') + ", et l'ouvrent dans le navigateur. Firefox demande alors l'autorisation, et le clavier fonctionne. Il faut Python, déjà nécessaire pour construire les ROM."))
A(p("Si le simulateur est publié en ligne (GitHub Pages), il suffit d'ouvrir son adresse : Chrome et Edge demandent l'autorisation, Firefox propose d'installer en un clic une petite extension de permission qu'il fabrique lui-même. Safari ne donne pas accès au MIDI."))
A(p("Une fois cochée, la case <b>clavier MIDI</b> le reste d'une visite à l'autre dans le même navigateur : il suffit ensuite de brancher le clavier et d'allumer la console."))

# ---------------------------------------------------------------- 3
A(chapter(3, "Si rien ne se passe", "Trouver l'étape qui bloque.", 3))
A(h2('Sur la MiSTer'))
A(table(['Ce que vous voyez', 'La cause probable', 'Que faire'], [
  ["pas de réglage <b>UART Mode</b> dans le menu", "c'est un ancien core (version 3 ou moins), qui ne déclare pas d'UART", "chargez " + c('MONOCORE_20261026c_midi.rbf') + " ou plus récent"],
  ["seulement <i>Local</i> et <i>UDP</i>, pas <i>USB</i>", "la MiSTer ne voit pas le clavier", "branchez-le sur la MiSTer, rouvrez le menu ; sinon redémarrez avec le clavier branché, puis vérifiez en SSH (ci-dessous)"],
  ["<i>USB</i> choisi, mais le tracker ne réagit pas", "une ancienne ROM, sans le MIDI", "copiez le " + c('monotracker.mnc') + " à jour dans " + c('games/MONOCORE')],
  ]))
A(p("Pour voir ce que Linux reconnaît, connectez-vous en SSH (" + c('root') + ", mot de passe " + c('1') + ") : tapez " + c('ls /dev/midi*') + " puis " + c('cat /proc/asound/cards') + " : un clavier reconnu apparaît comme une carte son et un " + c('/dev/midi') + "."))
A(h2('Dans le simulateur'))
A(table(['Ce que vous voyez', 'La cause probable', 'Que faire'], [
  ["un message sur l'accès au MIDI dans la console de débogage", "Safari, ou Firefox avec la page ouverte depuis un fichier", "prenez Chrome ou Edge ; sous Firefox, lancez la page par " + c('simulateur.bat') + " ou " + c('simulateur.py')],
  ["pas de nom de clavier à côté de la case", "le navigateur ne voit pas le clavier", "débranchez-le, rebranchez-le, rechargez la page"],
  ["le nom s'affiche, mais le compteur reste vide quand on joue", "le clavier envoie sur un autre port, ou est dans un mode spécial (DAW)", "remettez le clavier dans son mode normal"],
  ["le compteur avance, mais le tracker ne réagit pas", "la console est éteinte, ou la ROM est ancienne", "cliquez sur <b>Mettre sous tension</b> ; prenez MONOTRACKER dans la liste des ROM intégrées"]]))

# ---------------------------------------------------------------- 4
A(chapter(4, "Le MIDI dans vos ROM", "Lire les notes d'un clavier avec le SDK mc.", 4))
A(p("Une ROM écrite avec le SDK bas niveau <b>mc</b> lit le clavier MIDI avec " + c('mc_midi_event()') + ". La fonction rend le message complet suivant, ou −1 s'il n'y en a pas ; on l'appelle en boucle à chaque image. Elle gère le statut implicite (running status), donne une note jouée à vélocité nulle comme une note relâchée, et écarte l'horloge, le sysex et les autres messages système."))
A(listing(r'''#include "mc.h"

int main(void) {
  mc_init();
  for (;;) {
    mc_frame();
    int e;
    while ((e = mc_midi_event()) >= 0) {
      int note = MIDI_DATA1(e), velocity = MIDI_DATA2(e);
      switch (MIDI_STATUS(e)) {
        case MIDI_NOTE_ON:  /* note 60 = do du milieu, velocity 1-127 */ break;
        case MIDI_NOTE_OFF: /* touche relâchée */ break;
        case MIDI_CC:       /* potentiomètre : note = numéro, velocity = valeur 0-127 */ break;
      }
    }
  }
}''', 'lire le clavier MIDI'))
A(table(['Élément', 'Rôle'], [
  [c('mc_midi_event()'), "message suivant : statut, puis deux octets de données ; −1 s'il n'y a rien"],
  [c('MIDI_STATUS(e)'), c('MIDI_NOTE_ON') + ', ' + c('MIDI_NOTE_OFF') + ', ' + c('MIDI_CC') + ', ' + c('MIDI_PITCH') + ', ' + c('MIDI_PROGRAM') + '…'],
  [c('MIDI_CHANNEL(e)'), "canal MIDI, de 0 à 15"],
  [c('MIDI_DATA1(e)') + ' / ' + c('MIDI_DATA2(e)'), "note ou numéro de contrôleur / vélocité ou valeur"],
  [c('mc_hw_version()'), "4 ou plus : l'entrée MIDI existe"],
  [c('MIDI_IN'), "le registre (0x4000_00DC) : une file de 64 octets reçus tels quels ; détails dans " + c('SPEC.md')]], 'ref'))
A(p("Pour tester sans clavier, " + c('tools/run.js') + " accepte des octets MIDI en hexadécimal, par exemple " + c('60:mi=90,3C,64') + " pour jouer le do du milieu à l'image 60. Le module " + c('mtrack') + " permet ensuite de jouer ces notes avec les instruments d'un morceau de MONOTRACKER, comme le fait le tracker."))


build(parts, COVER, OUT_PDF, OUT_HTML, 'MONOCORE-1 - Guide MIDI', 'MONOCORE-1 · GUIDE MIDI', 'Brancher un clavier MIDI sur la MONOCORE-1')
