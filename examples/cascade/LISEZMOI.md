# CASCADE

Le jeu des notes qui tombent de la MONOCORE-1, à jouer sur un clavier MIDI (ou sur le clavier de
l'ordinateur). Les notes d'une piste d'un morceau de MONOTRACKER descendent vers un clavier dessiné en bas
de l'écran : jouez chacune quand elle touche la ligne jaune. Le morceau joue l'accompagnement, et vous jouez
la mélodie avec le son du morceau.

## Jouer

| Où | Comment |
|---|---|
| MiSTer | `cascade.mnc` dans `games/MONOCORE`, le core version 4 et le clavier MIDI branché (menu du core : *UART Mode* → *MIDI*, *MidiLink : USB*), voir `manual/MONOCORE-1_Guide_MIDI.pdf` |
| Simulateur | CASCADE dans la liste des ROM intégrées ; cochez « clavier MIDI » sous l'écran (Firefox : page ouverte par `dist/simulateur.bat`) |

Sans clavier MIDI, les deux rangées de lettres jouent comme dans MONOTRACKER (W X C V… et A Z E R…), à
partir de l'octave de la note la plus grave du morceau.

Au menu : flèches (ou manette) pour choisir, Entrée (ou bouton 1) pour jouer, Échap (ou bouton 2) pour
revenir.

| Réglage | Choix |
|---|---|
| MORCEAU | les morceaux de la ROM, puis ceux de la NVRAM de MONOTRACKER (ci-dessous) |
| PARTIE | la piste à jouer : toutes celles qui ont des notes, avec leur instrument et leur nombre de notes |
| NIVEAU | FACILE : toutes les octaves comptent, fenêtre large ; NORMAL : la bonne note à la bonne octave, et les fausses notes cassent la série |
| TEMPO | 50 %, 75 % ou 100 % |

Chaque note jouée à temps rapporte des points : PARFAIT (300) à moins de 70 ms (110 ms en facile), BIEN
(100) à moins de 150 ms (240 ms). La série multiplie les points, jusqu'à ×4 à 30 notes. À la fin : la
précision, la note de S à D et le meilleur score de la séance.

## Les morceaux

La ROM contient trois airs du domaine public arrangés pour la console (Au clair de la lune, Frère Jacques,
l'Ode à la joie, faits par `make_songs.py`) et la démo de MONOTRACKER.

Vos propres morceaux : composez-les dans MONOTRACKER, sauvez-les, puis donnez à CASCADE la NVRAM du tracker.
Sur la MiSTer, copiez `saves/MONOCORE/monotracker.sav` en `saves/MONOCORE/cascade.sav` ; dans le simulateur,
**Exporter .sav** avec MONOTRACKER chargé, puis **Importer .sav…** avec CASCADE chargé. Les morceaux des
huit emplacements apparaissent dans la liste. La mélodie à jouer est de préférence sur une piste à elle,
une note à la fois.

## Comment c'est fait

Le jeu parcourt le morceau comme le lecteur `mtrack` le jouera, et relève les notes de la piste choisie
avec leur ligne de début et de fin. Le temps du jeu suit la ligne jouée par `mtrack` (`mt_order`,
`mt_row`), plus une fraction calculée à chaque image : les notes restent calées sur la musique, à tous les
tempos. Les notes sont dessinées dans une couche de tuiles qui sert d'anneau de 64 lignes (une ligne de
tuiles par ligne du morceau) et qui défile au pixel près ; la piste jouée est réservée (`mt_reserve`) et
la note du joueur est jouée avec l'instrument du morceau (`mt_sfx`).
