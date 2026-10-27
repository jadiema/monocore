# SOLFEGE

Apprendre à lire les notes sur la portée, avec un clavier MIDI (ou le clavier de l'ordinateur).

Des notes attendent sur la portée ; la première, en bleu sur la ligne de lecture jaune, est celle à jouer
sur le clavier dessiné en bas de l'écran. Il n'y a pas de chronomètre : la note attend qu'on la trouve.
Bien jouée, elle passe au vert et la suivante avance.

On commence avec trois notes (do, ré, mi). Quand chacune des notes rencontrées a été jouée juste trois
fois de suite, sans erreur, une nouvelle note arrive : le jeu l'annonce (« NOUVELLE NOTE : FA, DANS LE 1er
INTERLIGNE ») et allume sa touche en jaune. En haut de l'écran, les notes déjà rencontrées sont grises tant
qu'elles sont à apprendre, blanches en cours, vertes quand elles sont connues.

| Clé | Notes, dans l'ordre où elles arrivent |
|---|---|
| clé de sol | do 4 (petite ligne en dessous) à la 5 (petite ligne au-dessus), en montant depuis do 4 ; puis fa♯, si♭, do♯, mi♭, sol♯, la♭, do♯ 4, ré♭, fa♯ 5, sol♭, la♯, mi♭ 5 |
| clé de fa | do 3 à do 4, puis en descendant si 2 à mi 2 ; puis fa♯, si♭, do♯, mi♭, sol♯, la♭, do♯ 4, ré♭, fa♯ 2, sol♭, la♯, mi♭ 2 |

Chaque clé compte 25 notes : les 13 notes naturelles, puis 12 altérations. Les dièses (♯) et les bémols
(♭) arrivent une touche noire à la fois, et chaque touche noire sous ses deux noms (sol♯ et la♭, fa♯ et
sol♭…) : la même touche s'écrit de deux façons. À l'arrivée d'une altération, le jeu rappelle sa règle
(« LE DIESE MONTE D'UN DEMI-TON : TOUCHE DE DROITE ») ; après deux erreurs, il dit quelle touche jouer
(« C'EST UN SI BEMOL : LA TOUCHE A GAUCHE DU SI »).

Une erreur remet à zéro la série de la note ; à la deuxième erreur sur la même note, le jeu dit où elle
se trouve (« C'EST UN MI, DANS LE 3e INTERLIGNE ») et allume la bonne touche. La bonne note à une autre
octave est signalée comme telle. La progression de chaque clé est gardée dans la NVRAM.

## La lecture déroulante

Au menu, MODE → LECTURE DEROULANTE : 40 notes arrivent de la droite à un rythme régulier et traversent
la ligne de lecture ; jouez chacune quand elle passe sur la ligne. Rien n'attend : une fausse note ou
une note oubliée compte simplement contre le score, et la suivante arrive déjà. Un léger clic marque
chaque note sur la ligne.

| Ce qui compte | Points |
|---|---|
| la bonne note, sur la ligne (à 18 pixels près) | 10, plus la série (jusqu'à 20 de plus) |
| pile sur la ligne (à 5 pixels près) | 10 de plus |
| une fausse note | 0, la série repart à zéro, le jeu dit quelle note c'était |
| une note oubliée | 0, la série repart à zéro |

À la fin : les notes justes (et combien pile sur la ligne), les fausses, les oubliées, le score, la
meilleure série, et les notes à revoir, celles qui ont été le plus souvent manquées. Une note rejoue
une série, Entrée revient au menu. Trois vitesses : LENTE (une note toutes les 1,9 s), MOYENNE (1,3 s) et
RAPIDE (0,85 s). Les notes en jeu sont celles du réglage NOTES ; ce mode ne change pas la progression.

| Réglage du menu | Choix |
|---|---|
| CLE | clé de sol ou clé de fa |
| NOTES | le nombre de notes en jeu, de 3 à 25 : pour sauter ce qu'on sait déjà (14 et plus : avec les dièses et bémols) |
| MODE | LA NOTE ATTEND (apprendre, avec la progression) ou LECTURE DEROULANTE (lire en rythme, pour les points) |
| VITESSE | la vitesse de la lecture déroulante |
| NOMS | les noms sous les notes : toujours, le temps de les apprendre (les deux premières réussites), ou jamais |
| RECOMMENCER | efface la progression de la clé choisie (Entrée deux fois) |

## Jouer

| Où | Comment |
|---|---|
| MiSTer | `solfege.mnc` dans `games/MONOCORE`, le core version 5 (640 × 480) et le clavier MIDI branché (menu du core : *UART Mode* → *MIDI*, *MidiLink : USB*), voir `manual/MONOCORE-1_Guide_MIDI.pdf` |
| Simulateur | SOLFEGE dans la liste des ROM intégrées ; cochez « clavier MIDI » sous l'écran (Firefox : page ouverte par `dist/simulateur.bat`) |

Le clavier à l'écran couvre quatre octaves, de do 2 à si 5. Sans clavier MIDI : W X C V… joue l'octave
du do 4 en clé de sol (do 3 en clé de fa), A Z E R… l'octave au-dessus. Échap (ou bouton 2) revient au menu.

## Comment c'est fait

Le jeu tourne en **640 × 480 entrelacé** : il faut le core en version 5 du matériel (le menu le signale
sinon). Rien n'y est agrandi : la police et les symboles sont rendus à la taille de l'écran par
`make_assets.py`, qui produit `assets.h`.

| Élément | D'où il vient |
|---|---|
| texte | DejaVu Sans Mono Bold (licence Bitstream Vera), en cases de 16 × 16 : 40 × 30 caractères |
| noms sous les touches | la même police, en 8 × 16 |
| clés, ronde, dièse, bémol | les symboles musicaux Unicode de GNU FreeSerif (GPL avec l'exception pour les polices), à l'échelle d'une portée dont les lignes sont à 16 pixels |

Chaque pixel garde quatre niveaux de couverture (indices de couleur 1 à 4) : le jeu règle les palettes
en mélangeant l'encre avec ce qu'il y a derrière (l'écran sombre ou le papier). Les bords sont ainsi
lissés sans scintiller en entrelacé, comme les lignes de la portée, épaisses de deux pixels.
`make_assets.py` repère aussi où chaque symbole se pose : la ligne du sol passe au centre de la
spirale de la clé de sol, la ligne du fa entre les deux points de la clé de fa.

Le piano est une seule image en 8 bits par pixel, 28 touches blanches de 22 pixels et les touches
noires entre elles : chaque touche a son propre indice de couleur, si bien qu'en allumer une ne coûte
qu'une écriture dans la palette. Les notes jouées sonnent avec l'échantillon de piano électrique de la
banque de MONOTRACKER (`mt_note`, sans morceau). Le choix de la note suivante favorise les notes pas
encore connues et la dernière arrivée.
