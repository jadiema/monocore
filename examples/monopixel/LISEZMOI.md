# MONOPIXEL

L'éditeur graphique de la MONOCORE-1, pendant de MONOTRACKER pour les images. Il est dans l'esprit
de Deluxe Paint sur Amiga et s'utilise à la souris et au clavier (USB, sur le DE10-Nano). Il
fonctionne en 640 × 480 entrelacé (80 × 30 caractères), ce qui demande le core en version 5 ; un
core plus ancien affiche un message.

On y dessine une **banque** : une planche de 4 pages de 128 × 128 pixels en cellules de 8 × 8,
16 palettes de 16 couleurs, 64 lutins animés et 4 décors. Les banques se gardent dans la NVRAM
(8 emplacements) et s'exportent pour les jeux, qui les affichent avec le module `mgfx` du SDK
(`sdk/MGFX.md`). L'interface est le module `ui480` (`sdk/UI480.md`), réutilisable par les
prochains éditeurs.

## Construire

    python3 ../../tools/mgfx/make_demo.py      # demo.mgf (déjà fourni)
    python3 ../../sdk/build.py .               # -> build/monopixel.mnc

Sur MiSTer : `monopixel.mnc` dans `games/MONOCORE`, avec une souris et un clavier branchés.

Dans le simulateur (`dist/monocore.html`), MONOPIXEL est dans la liste des ROM intégrées. La
« souris de la console » et le « clavier de la console » s'activent tout seuls (F9 coupe le
clavier). Sous l'écran, **Banques .mgf** télécharge les banques sauvées, et **Exporter .sav** /
**Importer .sav…** échangent la NVRAM avec la MiSTer.

## Les pages

| Touche | Page | Contenu |
|---|---|---|
| F1 | DESSIN | à gauche la zone en cours (8 à 64 pixels de côté) agrandie, à droite la page de la planche ; outils, palette, couleurs A et B, réglage R V B de 0 à 31 |
| F2 | LUTINS | la liste des 64 lutins ; pour celui choisi : nom, position et taille dans la planche, palette, nombre d'images, vitesse, aller-retour, et l'aperçu animé |
| F3 | DECORS | la carte (vue de 320 × 320 pixels), la planche pour choisir la tuile ou un bloc de tuiles, taille de la carte et des tuiles, palette, miroirs |
| F4 | BANQUE | les 8 emplacements de la NVRAM, le nom de la banque, sauver, charger, effacer, nouvelle banque, démo, disposition du clavier |
| F5 | AIDE | les raccourcis |

La zone agrandie est un sprite zoomé par le matériel, qui lit directement la copie de la zone
en CHR RAM. Il n'y a aucun rendu logiciel et le dessin se met à jour à chaque image.

## Dessin

| Souris / touche | Rôle |
|---|---|
| clic / clic droit | dessine avec la couleur A / B ; sur le nuancier : choisit A / B ; molette : couleur A |
| C, T, R, P, S, I | crayon, trait, cadre, pavé, seau (remplissage dans la zone), pipette |
| X, Y, O | miroir horizontal, vertical, pivoter d'un quart de tour (zone carrée) |
| flèches / Maj+flèches | déplacent la zone dans la planche / décalent ses pixels (en boucle) |
| 1 2 3 4 | zone de 8, 16, 32, 64 pixels (aussi les compteurs L et H) |
| Page préc. / suiv. | page de la planche |
| Suppr, Ctrl+C, Ctrl+V | efface, copie, colle la zone |
| G | grille (pixels et cellules) |
| planche : clic / clic droit | choisit la zone / y recopie la zone en cours (pour faire l'image suivante d'une animation) |

Pour une animation, on dessine l'image 0, puis on fait un clic droit juste à sa droite dans la
planche pour la recopier, et on retouche la copie. Sur la page LUTINS, **PRENDRE** donne au lutin
la zone du dessin, et IMAGES / VITESSE règlent l'animation. **DESSINER** ramène à son image 0.

## Décors

Un clic pose la tuile choisie, et un glissement sur la planche choisit un bloc de tuiles posé
d'un coup. Le clic droit efface. Les outils sont POSER, PAVE (un rectangle rempli du motif),
SEAU et PIPETTE (I). H et V sont les miroirs de la tuile, et PAL sa palette. On fait défiler la
carte au bouton du milieu, aux flèches (Maj : 8 tuiles) ou à la molette (Maj : horizontalement).
Une carte fait 32, 64 ou 128 tuiles de côté, 4096 cases au plus, comme un plan matériel. Une
tuile de 16 pixels est un carré de 2 × 2 cellules de la planche, aligné, et le dessin indique son
numéro (« TUILE 16 : »).

Les boutons **SOLIDE**, **PLATEF.**, **ECHELLE**, **EAU** et **DANGER** disent ce que sont les
tuiles du pinceau, pour toutes à la fois. Ces attributs s'affichent en symboles (`#` `=` `H` `~` `!`)
dans le coin des tuiles de la planche, et A les montre ou les cache. Les jeux les lisent avec
`mg_attr_at`, et la bibliothèque `mjeu` s'en sert pour les collisions.

L'outil **OBJETS** (O) pose dans le décor le lutin choisi à droite du bouton (▲▼ : le lutin suivant
qui a un nom), sur la grille des tuiles, avec son paramètre **P** (0 à 255) et le miroir H. Un clic
sur un objet le prend : on le fait glisser, et P ou le lutin choisis ensuite le modifient. Un clic
droit ou Suppr l'enlève. On peut en poser 64 par décor. Ils servent au départ du héros, aux pièces,
aux ennemis ou à l'arrivée : le jeu décide ce que fait chacun d'après le nom de son lutin
(`examples/plateformes`). Ctrl+Z annule aussi les objets.

La vue de la carte est au pixel près. En 640 × 480, une carte est donc deux fois plus petite qu'en
jeu en 320 × 240.

## Annuler, sauver

Ctrl+Z annule la dernière modification (un trait, une opération sur la zone, une pose de tuiles).
Une seconde fois, il la refait. Ctrl+S sauve dans l'emplacement de la banque, ou dans le premier
emplacement libre. Sur MiSTer, il faut ensuite ouvrir l'OSD pour que la NVRAM soit écrite sur la
carte SD, comme pour MONOTRACKER. La page BANQUE affiche la taille compacte de la
banque (la démo fait 5,3 Ko) et la place libre dans les 32 Ko de NVRAM. Les actions qui feraient perdre des modifications demandent un second clic.

## Pour un jeu

    python3 ../../tools/mgfx/mgfx.py monopixel.sav 1 -o ../monjeu/banque.mgf

puis `modules = mgfx` et `data = banque.mgf` dans le `project.txt` du jeu. Les exemples sont
`examples/test_mgfx` (la bibliothèque seule) et `examples/plateformes` (un jeu fait de sa banque,
avec la bibliothèque `mjeu`). Dans l'autre sens, `mgfx.py --new … --import image.png:x,y:pal` fait une banque
à partir d'images, et `--to-sav` la place dans un emplacement pour la retoucher ici.

## Mémoire

Le format d'une banque est décrit dans `sdk/MGFX.md`. La planche en cours d'édition vit dans la
CHR RAM (0x00000, 32 Ko), puisque le matériel l'y lit pour l'afficher. On y trouve aussi les tuiles
16 × 16 tirées de la planche (0x08000), les copies montrées par les sprites (zone, aperçu :
0x10000), le damier (0x11000) et l'interface `ui480` (0x1A000). La WRAM garde les palettes, les
lutins, les 4 décors (32 Ko) et les tampons d'annulation. La sauvegarde est compressée directement
dans la NVRAM.

## Limites actuelles

Les graphismes sont en 4 bpp seulement (16 couleurs par palette). Il n'y a pas de 8 bpp ni
d'import d'image depuis la console : l'import passe par `mgfx.py`. L'annulation n'a qu'un niveau.
La zone de dessin est carrée ou rectangulaire de 1, 2, 4 ou 8 cellules de côté. Il manque encore
l'ellipse, le texte et la brosse personnalisée.
