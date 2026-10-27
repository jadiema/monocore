# Module « mgfx » : les graphismes de MONOPIXEL dans les jeux

`mgfx` lit les **banques graphiques** dessinées avec l'éditeur **MONOPIXEL** (`examples/monopixel`).
L'éditeur s'en sert pour ranger et relire ce qu'on dessine, et les jeux s'en servent pour afficher
les banques exportées. C'est le même code et le même format, donc un lutin ou un décor s'affiche dans
le jeu exactement comme dans l'éditeur. C'est le pendant graphique de `mtrack` (`MTRACK.md`).

## Ce que contient une banque

| Partie | Contenu |
|---|---|
| planche | 1 à 4 pages de 16 × 16 cellules de 8 × 8 pixels en 4 bpp (128 × 128 pixels par page, 8 Ko) ; la cellule (cx, cy) est la n° `cy × 16 + cx` |
| palettes | 1 à 16 palettes de 16 couleurs RGB555 ; la couleur 0 est toujours transparente |
| lutins | jusqu'à 64 : un rectangle de la planche (1 à 8 cellules de côté), une palette, une animation de 1 à 16 images, un nom de 8 lettres |
| décors | jusqu'à 4 cartes de 32, 64 ou 128 tuiles de côté (4096 cases au plus, comme un plan matériel), en tuiles de 8 ou de 16 pixels |

Les images d'une animation sont **côte à côte** dans la planche : l'image 1 est à droite de
l'image 0, et ainsi de suite. Quand une image ne tient plus dans les 16 colonnes, elle passe au
début de la bande suivante (h rangées de cellules plus bas). `mg_frame_pos` donne la position de
chaque image.

Une tuile de 16 pixels t couvre les cellules `(t % 8) × 2` et `(t / 8) × 2` de la planche, sur
2 × 2 cellules. Il y a donc 64 tuiles de 16 par page et 256 en tout. Une tuile de 8 pixels est
simplement une cellule (0 à 1023).

## Utilisation dans un jeu

Dans `project.txt` :

    library = mc
    modules = mgfx
    data    = mabanque.mgf

Dans le programme :

    #include "mgfx.h"
    static mg_bank_t bank;

    mg_open(&bank, mabanque_file, mabanque_size);   // lit l'en-tête (rien n'est encore copié)
    mg_load(&bank, 0, 16, MG_LOAD_ALL);             // graphismes en CHR RAM depuis 0, palettes 16-31
    mg_map_layer(&bank, 0, 1, 1);                   // décor 0 sur le plan 1, priorité 1
    int heros = mg_sprite_find(&bank, "HEROS");
    // à chaque image :
    mg_sprite(&bank, 1, heros, mg_frame_at(&bank, heros, mc_frames), x, y, 4);   // sprite matériel 1

`examples/test_mgfx` en est un exemple complet. Il prend le décor de la banque de démonstration, un
héros qui court et saute sur les tuiles, des pièces qui tournent et des étoiles.

`mg_load` décompresse la planche en CHR RAM puis, selon les drapeaux :

| Drapeau | Effet | Taille en CHR RAM |
|---|---|---|
| (toujours) | la planche, en tuiles 8 × 8 (décors de tuiles de 8) | 8 Ko par page |
| `MG_LOAD_TILES16` | les tuiles 16 × 16 au format du matériel (décors de tuiles de 16) | 8 Ko par page |
| `MG_LOAD_SPRITES` | les cellules de chaque image de chaque lutin, rangées comme le veut le matériel (cellule = `tuile + cy × largeur + cx`) | 32 octets par cellule |

Avec `MG_LOAD_SPRITES`, `SPR_CHR` pointe sur le début de la CHR RAM. Les numéros de cellule que
donne `mg_sprite_cell` sont comptés depuis là. `mg_load_size` dit combien d'octets il faut, et
`mg_load` renvoie 0 si cela ne tient pas. La banque de démonstration prend 17 Ko.

Les palettes de la banque vont dans les palettes matérielles `first_pal` à `first_pal + n − 1`.
`mg_palettes` les recharge seules, par exemple après un fondu.

## Fonctions

| Fonction | Rôle |
|---|---|
| `mg_open(b, data, max)` | vérifie et lit une banque (0, ou −1 si elle est invalide) ; `mg_check` vérifie seulement |
| `mg_load(b, chr, pal, what)` | voir ci-dessus |
| `mg_sprite_count`, `mg_sprite_def`, `mg_sprite_find` | les lutins : nombre, description, numéro d'après le nom |
| `mg_sprite(b, hw, s, image, x, y, prio)` | règle le sprite matériel `hw` (table fantôme de `mc.h`) ; les miroirs se font ensuite par `mc_sprite_flip` |
| `mg_frame_at(b, s, t)` | l'image d'une animation à l'image vidéo t (vitesse = images vidéo par image, aller-retour possible) |
| `mg_sprite_cell`, `mg_sprite_w`, `mg_sprite_h` | première cellule d'une image, taille en pixels |
| `mg_map_count`, `mg_map_w`, `mg_map_h`, `mg_map_tile` | les décors : nombre, taille en tuiles, taille d'une tuile (8 ou 16) |
| `mg_map_layer(b, m, plan, prio)` | met le décor m sur un plan matériel (graphismes, taille, cases) |
| `mg_map_read(b, m, out)` | décompresse les cases d'un décor dans un tableau de `u16` |
| `mg_tile_at(plan, px, py)` | numéro de la tuile sous un pixel du plan, en bouclant comme le matériel : pour les collisions |
| `mg_layer_entry`, `mg_layer_set` | lire ou changer une case d'un plan (une porte qui s'ouvre…) |

Une case de décor (`u16`) contient le numéro de tuile (bits 0–9), la palette de la banque
(bits 10–13), le miroir horizontal (bit 14) et le miroir vertical (bit 15). Sur le plan matériel,
`mg_map_layer` y ajoute `first_pal`. `mg_tile_at` renvoie donc le numéro de tuile dans les 16 bits
bas de l'entrée matérielle.

## Format du fichier `.mgf`

Petit-boutiste. C'est le même octet pour octet dans un emplacement de la NVRAM de MONOPIXEL et
dans un fichier `.mgf`. Il est lu et écrit en C par `sdk/mgfx.c` et en Python par
`tools/mgfx/mgbank.py`. Toute modification se fait dans les deux.

| Offset | Taille | Champ |
|---|---|---|
| 0 | 4 | `MGF1` |
| 4 | 4 | taille de toute la banque |
| 8 | 4 | pages (1–4), palettes (1–16), lutins (0–64), décors (0–4) |
| 12 | 4 | drapeaux (0), version (1), réservé |
| 16 | 16 | nom (ASCII, complété par des zéros) |
| 32 | 32 × palettes | les couleurs (RGB555) |
| … | 16 × lutins | x, y (cellule de l'image 0), largeur, hauteur (cellules), palette, images, vitesse, drapeaux (bit 0 aller-retour), nom (8 octets) |
| … | 8 × décors | code de largeur, code de hauteur (0 = 32, 1 = 64, 2 = 128), tuiles de 16 (0/1), 5 octets réservés |
| … | 4 + n | la planche compressée : sa longueur n, puis les données (pages × 8 Ko une fois décompressées) |
| … | 4 + n par décor | les cases compressées (largeur × hauteur × 2 octets) |

La compression ressemble à PackBits. Un octet de contrôle c < 128 est suivi de c + 1 octets
recopiés tels quels. Un octet c ≥ 128 est suivi d'un octet répété c − 125 fois (3 à 130). Une
page vide tient en une centaine d'octets.

Dans la NVRAM, MONOPIXEL range ses banques comme MONOTRACKER ses morceaux. On y trouve `MGNV`, la
disposition du clavier (octet 4) et le dernier emplacement utilisé (octet 5). À partir de l'octet 16
viennent 8 × {décalage, taille}, et les banques commencent à l'octet 80.

## Outils (`tools/mgfx/`)

    python3 tools/mgfx/mgfx.py monopixel.sav                    # liste les 8 emplacements
    python3 tools/mgfx/mgfx.py monopixel.sav 1 -o banque.mgf    # emplacement 1 -> data = banque.mgf
    python3 tools/mgfx/mgfx.py monopixel.sav 1 -o banque.h      # en tableau C
    python3 tools/mgfx/mgfx.py banque.mgf --dump                # le contenu en texte
    python3 tools/mgfx/mgfx.py banque.mgf --png images/         # planche, lutins (GIF animés), décors
    python3 tools/mgfx/mgfx.py --new perso.mgf --import heros.png:0,64:1 --sprite HEROS:0,8,2,2,1,4,8
    python3 tools/mgfx/mgfx.py perso.mgf --to-sav monopixel.sav 3   # pour la retoucher dans MONOPIXEL

`--import image.png:x,y:p` dessine une image dans la planche au pixel (x, y), avec ses couleurs dans
la palette p : 15 couleurs au plus plus la transparence. `--sprite NOM:x,y,l,h,pal[,images,vitesse]`
déclare un lutin en cellules. `make_demo.py` refait la banque de démonstration
(`examples/monopixel/demo.mgf`) et définit la palette par défaut d'une nouvelle banque.

Sur MiSTer, la sauvegarde est `saves/MONOCORE/monopixel.sav` sur la carte SD. Elle est écrite
quand on ouvre l'OSD après une sauvegarde dans MONOPIXEL. Dans le simulateur, **Exporter .sav**
donne la NVRAM entière et **Banques .mgf** donne directement les banques.
