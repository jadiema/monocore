# L'atelier : faire un jeu entièrement sur la console

**MONOATELIER** (`examples/atelier/build/monoatelier.mnc`) réunit dans **une seule ROM** un
lanceur, les trois éditeurs (MONOPIXEL pour les images, MONOSFX pour les bruitages, MONOTRACKER
pour la musique) et un jeu, PLATEFORMES. Comme c'est une seule ROM, MiSTer leur donne **une seule
NVRAM** (`saves/MONOCORE/monoatelier.sav`). Ce qu'un éditeur sauve est donc là pour les autres et
pour le jeu : on dessine un niveau, on fait ses bruitages et sa musique, puis on y joue, sans
ordinateur entre les étapes.

## Utilisation

| Touche | Rôle |
|---|---|
| F1, F2, F3 (ou clic) | ouvre MONOPIXEL, MONOSFX, MONOTRACKER |
| F5, Entrée | joue PLATEFORMES avec le niveau, les bruitages et la musique de la NVRAM |
| clic sur un emplacement | c'est celui que le jeu prendra (sinon : le dernier sauvé par l'éditeur) ; un second clic revient au choix automatique |
| F12, dans chaque programme | retour à l'atelier (après confirmation s'il reste des modifications non sauvées) ; dans le jeu aussi START + COIN |

Chaque éditeur garde ses 8 emplacements et fonctionne comme seul. On sauve avec Ctrl+S avant F12,
puis on ouvre l'OSD pour que MiSTer écrive la NVRAM sur la carte SD. Dans MONOTRACKER, F12 sert
seul à « instrument suivant » ; dans l'atelier, c'est le + du pavé numérique.

Le jeu cherche ses objets par le nom de leurs lutins (`HEROS`, `PIECE`, `BLOB`, `DRAPEAU`) et ses
sons par leur nom (`SAUT`, `PIECE`, `ECRASE`, `AIE`, `BRAVO`, `PERDU`, `MENU`). Il suffit de garder
ces noms pour qu'un niveau, des bruitages ou une musique refaits dans l'atelier soient pris tels
quels. Ce qui manque est remplacé par ce que le jeu porte en ROM (la démo).

## Comment c'est construit

    python3 tools/mkatelier.py      # -> examples/atelier/build/monoatelier.mnc

Chaque programme est construit normalement par `sdk/build.py`, puis lié une seconde fois à sa
propre adresse en ROM (`sdk/link.ld` avec une autre origine). Tous utilisent **la même WRAM** :
un seul tourne à la fois. Passer à un autre programme (`at_run`, module `atelier`) revient à le
démarrer, son code de démarrage remettant la WRAM et la vidéo à zéro. C'est pourquoi MONOPIXEL
(121 Ko de WRAM) et MONOTRACKER (101 Ko) peuvent tenir dans la même ROM.

| Adresse ROM | Contenu |
|---|---|
| 0x000000 | le lanceur (`examples/atelier`), avec l'en-tête de la ROM (titre `MONOATELIER`) |
| 0x1F0000 | la table des programmes : `MATL`, u32 nombre, puis {u32 adresse, titre (16 octets), u32 réservé} |
| 0x200000 | MONOPIXEL |
| 0x280000 | MONOSFX |
| 0x300000 | PLATEFORMES |
| 0x480000 | MONOTRACKER |

Les adresses sont dans `PROGRAMS`, en tête de `tools/mkatelier.py` ; on y ajoute un jeu comme les
autres. `at_present()` vérifie l'en-tête de la ROM en cours (titre `MONOATELIER`, taille), car la
mémoire après une ROM plus courte peut encore contenir la table d'un atelier chargé avant.

## La NVRAM partagée (module `nvslots`)

Les éditeurs passent par le module `nvslots` (`sdk/nvslots.h`). Seul (sa propre ROM), un éditeur
garde exactement son ancien format (`MTNV`, `MGNV`, `MSNV`), et les anciennes sauvegardes restent
lisibles. Dans l'atelier, la NVRAM commence par `ATNV` :

| Octets | Contenu |
|---|---|
| 0–3 | `ATNV` |
| 4 | disposition du clavier (AZERTY 0, QWERTY 1), commune |
| 5, 6, 7 | dernier emplacement sauvé par MONOTRACKER, MONOPIXEL, MONOSFX |
| 8–11 | réglages des éditeurs (MONOTRACKER : bit 0 de l'octet 8, la vélocité) |
| 12, 13, 14 | emplacements choisis pour le jeu (images, bruitages, musique ; 0xFF : le dernier) |
| 16–271 | 32 × {u32 décalage, u32 taille} : MONOTRACKER 0–7, MONOPIXEL 8–15, MONOSFX 16–23 |
| 272 → | les éléments, rangés à la suite |
| les 256 derniers | la sauvegarde du jeu (`mj_save` : record…) |

## Outils (`tools/atelier/`)

    python3 tools/atelier/atelier.py monoatelier.sav                   # ce qu'elle contient
    python3 tools/atelier/atelier.py monoatelier.sav --export jeu/     # images_1.mgf, bruits_1.mfx, musique_1.mts...
    python3 tools/atelier/atelier.py monoatelier.sav --import monopixel.sav monosfx.sav monotracker.sav
    python3 tools/atelier/atelier.py monoatelier.sav --put 1 3 niveau.mgf   # un fichier dans l'emplacement 3 de MONOPIXEL

`--import` reprend le travail fait dans les éditeurs seuls, aux mêmes numéros d'emplacement.
`tools/mgfx/mgfx.py`, `tools/msfx/msfx.py` et `tools/mtrack/sav2song.py` lisent aussi
directement `monoatelier.sav`. Dans le simulateur, MONOATELIER est dans la liste des ROM, et les
boutons **Morceaux .mts**, **Banques .mgf** et **Bruitages .mfx** lisent sa NVRAM.
