# MONOTRACKER

En haute résolution : 640 × 240, soit 80 colonnes de texte (il faut le core MONOCORE en version 5 ;
un core plus ancien affiche un message). La police est rendue pour ces pixels deux fois plus hauts que
larges par `make_font.py` (DejaVu Sans Mono Bold, licence Bitstream Vera), lissée sur quatre niveaux.

Le mode d'emploi complet, illustré, est le manuel `manual/MONOTRACKER_Manuel.pdf`
(régénéré par `python3 manual/tools/tracker_manual.py`).

Un tracker de musique pour la MONOCORE-1, qui se joue au clavier et à la souris (USB, sur le DE10-Nano). Il pilote
les 16 canaux de la console : 8 pistes PSG (oscillateurs) et 8 pistes PCM (échantillons d'une
banque de 21 sons originaux). Les morceaux se gardent dans la NVRAM (8 emplacements) et s'exportent
pour les jeux, qui les jouent avec le module `mtrack` du SDK (voir `sdk/MTRACK.md`).

## Construire

    python3 ../../tools/mtrack/make_bank.py      # mtbank.bin (déjà fourni ; numpy et scipy)
    python3 ../../tools/mtrack/make_demo.py      # demo.mts (déjà fourni)
    python3 ../../sdk/build.py .                 # -> build/monotracker.mnc

Sur MiSTer : `monotracker.mnc` dans `games/MONOCORE`, un clavier et une souris branchés (la souris demande le core à jour, matériel version 3).

Dans le simulateur (`dist/monocore.html`), MONOTRACKER est dans la liste des ROM intégrées ; le
« clavier de la console » et la « souris de la console » s'activent tout seuls quand on le choisit (F9 coupe le clavier). Sous l'écran,
**Morceaux .mts** télécharge les morceaux sauvés, **Exporter .sav** / **Importer .sav…** échangent
la NVRAM avec la MiSTer : on peut composer dans le navigateur et finir sur la borne, ou l'inverse.
Les raccourcis du tracker évitent ceux que le navigateur ne laisse pas à la page (Ctrl+N, Ctrl+T,
Ctrl+W).

## L'écran

L'interface reprend l'esprit des trackers de l'Amiga (ProTracker) : des panneaux gris en relief, un
panneau du haut toujours présent et, dessous, la page en cours.

| Zone | Contenu |
|---|---|
| en haut à gauche | compteurs POS, MOTIF, LONG (longueur du morceau), BPM, VIT, VOL, OCT, PAS, INSTR, avec leurs flèches ▲▼ |
| en haut au centre | boutons JOUER, MOTIF (le motif en boucle), STOP, EDIT, SUIVI, SAUVER et les quatre pages : MOTIFS, SONS, MORCEAU, AIDE |
| en haut à droite | les vumètres des 16 pistes (1-8 PSG, A-H PCM), avec maintien de crête |
| bandeau | nom du morceau, instrument courant, durée de lecture ; messages et aide contextuelle |
| dessous | la page : **motifs** (64 lignes, 6 pistes à l'écran), **sons** (réglages en deux colonnes, enveloppe et onde en graphe), **morceau** (ordre, réglages, emplacements de la NVRAM et leurs boutons) ; l'**aide** occupe tout l'écran |

## La souris

Sur MiSTer, une souris USB branchée sur la carte (le core gère la souris depuis la version 3 du
matériel) ; dans le simulateur, la case « souris de la console », cochée d'office pour MONOTRACKER.

| Où | Clic | Clic droit | Molette |
|---|---|---|---|
| flèches ▲▼ des compteurs | +1 / −1 (Maj : ×10), maintenu : répète | sens inverse | sur la valeur : +/− |
| boutons | action | | |
| vumètres | choisit la piste | piste muette | change de piste |
| nom d'une piste (motifs) | muette | solo | |
| case d'un motif | y place le curseur (note, instrument, volume, effet) | fait entendre la note | fait défiler les lignes |
| réglage d'un son ou du morceau | sur le nom : le choisit ; sur la valeur : +1 (Maj : +10), maintenu : répète | −1 | +/− |
| ordre du morceau | choisit la position ; sur le numéro de motif : +1 | −1 | fait défiler |
| emplacement de la NVRAM | le choisit, puis SAUVER, CHARGER, EFFACER, NOUVEAU ou DEMO | | |

Les actions qui feraient perdre quelque chose (charger par-dessus des modifications, effacer,
remplacer) demandent un second clic, ou une seconde fois le même raccourci.

## Touches principales

| Touches | Rôle |
|---|---|
| Entrée / Maj+Entrée / Échap | joue le morceau / le motif en boucle / arrête tout |
| Espace | mode édition (bande rouge) ; sans lui, le piano ne fait que jouer |
| rangée du bas (W S X D C…) et du haut (A é Z " E…) | piano sur deux octaves ; « & » : note coupée `===` |
| 0-9, A-F | instrument, volume, effet (les chiffres d'une colonne se suivent, puis le curseur descend) |
| F5/F6, F7/F8, F11/F12 (ou pavé −/+) | octave, pas d'avancement, instrument courant |
| Suppr, Inser, Retour arrière | efface la case, insère une ligne, retire une ligne |
| Ctrl+flèches | position précédente/suivante, motif de la position −/+ |
| Ctrl+C/X/V, Alt+C/X/V | copie/coupe/colle la piste, le motif entier |
| Ctrl+M, Ctrl+O, Ctrl+F | piste muette, solo, suivi de la lecture |
| Ctrl+S | sauve dans la NVRAM (puis ouvrir l'OSD pour l'écrire sur la carte SD) |

Le piano dépend de la *position* des touches, donc il est le même sur tous les claviers ; les
lettres (noms, chiffres hexadécimaux, raccourcis Ctrl) suivent la disposition choisie en F3.

Sur la page du morceau : Ctrl+S sauve, Ctrl+L charge, Ctrl+D efface un emplacement, Ctrl+E
commence un nouveau morceau, Ctrl+R recharge la démo (chacun demande une confirmation s'il ferait
perdre des modifications).

Sur la page des sons, le piano joue l'instrument affiché en polyphonie ; flèches haut/bas pour
choisir un réglage, gauche/droite pour le changer (Maj : par 10), Page préc./suiv. ou le compteur
INSTR pour changer d'instrument, Entrée sur NOM pour l'écrire. Un nouveau morceau (Ctrl+E en F3) reprend les
instruments du morceau de démonstration, qui sont définis dans `tools/mtrack/make_demo.py`.

## Clavier MIDI

Le branchement pas à pas, sur la MiSTer et dans le simulateur, est dans `manual/MONOCORE-1_Guide_MIDI.pdf`.

Un clavier MIDI USB (Novation Launchkey par exemple) joue et écrit les notes comme le piano du
clavier : la force de frappe donne le volume de la case (réglage VELOCITE de la page du morceau).
Sur la page des sons, les notes jouent l'instrument à plusieurs voix et les huit potentiomètres
(CC 21 à 28) règlent l'attaque, le déclin, le maintien, le relâché, le volume, la vitesse et
l'ampleur du vibrato, et le rapport cyclique (le panoramique pour un son PCM).

Sur MiSTer, il faut le core en version 4 et le menu du core réglé sur *UART : MIDI*, connexion USB
(MidiLink). Dans le simulateur, cochez « clavier MIDI » sous l'écran : Chrome et Edge l'acceptent depuis le
fichier ; Firefox seulement si la page est servie : lancez-la par `dist/simulateur.bat` (Windows) ou
`dist/simulateur.py`, qui l'ouvrent par `http://localhost`.

## Mémoire

Les 24 motifs (4 Ko chacun) sont rangés dans la CHR RAM, la police, les pièces des panneaux, le
pointeur, les graphes et les vumètres dans ses 32 derniers Ko. Trois plans de tuiles : le texte,
les bandes des lignes du motif, les panneaux (décalés de 4 pixels vers le haut, ce qui laisse une
marge de 4 pixels autour du texte) ; le pointeur est un sprite. La WRAM garde les instruments, l'écran et le tampon de sauvegarde. Un morceau
compact typique fait de 1 à 6 Ko : la NVRAM de 32 Ko en garde plusieurs.

## Pour un jeu

    python3 ../../tools/mtrack/sav2song.py monotracker.sav 1 -o ../monjeu/theme.mts

Pour trouver les nappes qui se superposent : `sav2song.py monotracker.sav 1 --tenues` liste les sons tenus
qui sonnent encore au début d'une position, sur une piste qui n'y joue rien (ils continuent par-dessus la
section suivante). Une note coupée `===` à la ligne 00 de cette position les arrête.

puis `modules = mtrack` et `data = ../monotracker/mtbank.bin theme.mts` dans le `project.txt` du jeu.
Exemple : `examples/test_mtrack`.

## Limites actuelles

Pas d'annulation ; motifs de 64 lignes (l'effet `Dxx` les raccourcit) ; les échantillons PCM sont
ceux de la banque (le matériel ne lit les échantillons qu'en ROM, on ne peut pas en enregistrer
depuis la console).
