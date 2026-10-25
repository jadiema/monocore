# MONOTRACKER

Un tracker de musique pour la MONOCORE-1, qui se joue au clavier (USB, sur le DE10-Nano). Il pilote
les 16 canaux de la console : 8 pistes PSG (oscillateurs) et 8 pistes PCM (échantillons d'une
banque de 21 sons originaux). Les morceaux se gardent dans la NVRAM (8 emplacements) et s'exportent
pour les jeux, qui les jouent avec le module `mtrack` du SDK (voir `sdk/MTRACK.md`).

## Construire

    python3 ../../tools/mtrack/make_bank.py      # mtbank.bin (déjà fourni ; numpy et scipy)
    python3 ../../tools/mtrack/make_demo.py      # demo.mts (déjà fourni)
    python3 ../../sdk/build.py .                 # -> build/monotracker.mnc

Sur MiSTer : `monotracker.mnc` dans `games/MONOCORE`, et un clavier branché. Dans le simulateur,
la case « clavier de la console » (F9) envoie le clavier du PC à la console.

## Pages

| Touche | Page |
|---|---|
| F1 | **Motifs** : 64 lignes, 4 pistes à l'écran (Tab pour passer aux suivantes), vumètres des 16 pistes |
| F2 | **Sons** : les 31 instruments, leurs réglages, l'enveloppe et l'onde (ou l'échantillon) en graphe |
| F3 | **Morceau** : ordre des motifs, tempo, volume, clavier AZERTY/QWERTY, emplacements de la NVRAM |
| F4 | **Aide** : toutes les touches et les effets |

## Touches principales

| Touches | Rôle |
|---|---|
| Entrée / Maj+Entrée / Échap | joue le morceau / le motif en boucle / arrête tout |
| Espace | mode édition (bande rouge) ; sans lui, le piano ne fait que jouer |
| rangée du bas (W S X D C…) et du haut (A é Z " E…) | piano sur deux octaves ; « & » : note coupée `===` |
| 0-9, A-F | instrument, volume, effet (les chiffres d'une colonne se suivent, puis le curseur descend) |
| F5/F6, F7/F8, F11/F12 | octave, pas d'avancement, instrument courant |
| Suppr, Inser, Retour arrière | efface la case, insère une ligne, retire une ligne |
| Ctrl+flèches | position précédente/suivante, motif de la position −/+ |
| Ctrl+C/X/V, Alt+C/X/V | copie/coupe/colle la piste, le motif entier |
| Ctrl+M, Ctrl+O, Ctrl+F | piste muette, solo, suivi de la lecture |
| Ctrl+S | sauve dans la NVRAM (puis ouvrir l'OSD pour l'écrire sur la carte SD) |

Le piano dépend de la *position* des touches, donc il est le même sur tous les claviers ; les
lettres (noms, chiffres hexadécimaux, raccourcis Ctrl) suivent la disposition choisie en F3.

Sur la page des sons, le piano joue l'instrument affiché en polyphonie ; flèches haut/bas pour
choisir un réglage, gauche/droite pour le changer (Maj : par 10), Page préc./suiv. pour changer
d'instrument, Entrée sur NOM pour l'écrire. Un nouveau morceau (Ctrl+N en F3) reprend les
instruments du morceau de démonstration, qui sont définis dans `tools/mtrack/make_demo.py`.

## Mémoire

Les 24 motifs (4 Ko chacun) sont rangés dans la CHR RAM, la police et les graphes dans ses 32
derniers Ko ; la WRAM garde les instruments, l'écran et le tampon de sauvegarde. Un morceau
compact typique fait de 1 à 6 Ko : la NVRAM de 32 Ko en garde plusieurs.

## Pour un jeu

    python3 ../../tools/mtrack/sav2song.py monotracker.sav 1 -o ../monjeu/theme.mts

puis `modules = mtrack` et `data = ../tracker/mtbank.bin theme.mts` dans le `project.txt` du jeu.
Exemple : `examples/test_mtrack`.

## Limites actuelles

Pas d'annulation ; motifs de 64 lignes (l'effet `Dxx` les raccourcit) ; les échantillons PCM sont
ceux de la banque (le matériel ne lit les échantillons qu'en ROM, on ne peut pas en enregistrer
depuis la console).
