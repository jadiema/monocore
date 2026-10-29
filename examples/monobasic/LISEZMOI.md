# MONOBASIC

Le BASIC de la MONOCORE-1, pour écrire de petits jeux sur la console elle-même. Il s'utilise au
clavier, en AZERTY ou en QWERTY. L'éditeur est en 640 × 480 et le programme tourne en 320 × 240
avec les modules du SDK : `mgfx` (les images de MONOPIXEL), `mjeu` (objets et collisions), `msfx`
(les bruitages de MONOSFX) et `mtrack` (la musique de MONOTRACKER). Dans l'atelier
(`sdk/ATELIER.md`, F4), le programme prend les images, les bruitages et la musique sauvés là.
Seul, il prend les banques de démonstration.

    python3 ../../sdk/build.py .        # -> build/monobasic.mnc (avec la musique de démo : 1,3 Mo)

Pour commencer : `EXEMPLE`, puis `RUN` (ou F5) pour jouer, et `LIST` pour lire le jeu et le changer.

## L'éditeur

C'est un éditeur plein écran, à la manière du MO5 ou du C64. On tape une ligne puis Entrée :

- avec un numéro (`120 PRINT "BONJOUR"`), elle est rangée dans le programme (un numéro seul l'efface) ;
- sans numéro, elle est faite tout de suite.

Les flèches déplacent le curseur partout. On remonte sur une ligne listée, on la corrige, puis
Entrée la range à nouveau. Échap efface la ligne du curseur, et arrête un programme en cours.

| Commande | Rôle |
|---|---|
| `RUN` [ligne], F5 | lance le programme (depuis le début ou cette ligne) |
| `LIST` [a][-b], F2 | montre le programme, ou les lignes a à b |
| `NEW` | efface le programme |
| `SAVE "NOM"`, `LOAD "NOM"`, `LOAD 3`, `DIR` | les 8 programmes de la NVRAM (sous forme de texte) |
| `AIDE` | le résumé des instructions |
| `EXEMPLE` | un petit jeu de plateformes à lire et à changer |
| F12 | retour à l'atelier |

Au démarrage, le dernier programme sauvé est chargé.

## Le langage

Les nombres sont des **entiers** de 32 bits. Une chaîne fait au plus 80 caractères. Un nom de
variable fait jusqu'à 8 lettres et chiffres : `A`, `SCORE`, `VX`, `NOM$` pour une chaîne. Les mots
clés ne sont reconnus que s'ils sont des mots entiers (`TOTAL` est une variable). Plusieurs
instructions vont sur une ligne, séparées par `:`.

| Classique | |
|---|---|
| `PRINT a; b, c` | `;` colle, `,` va à la colonne suivante (de 10 en 10), un `;` final garde la ligne |
| `A = 3`, `A$ = "TEXTE" + B$`, `T(I) = 5`, `LET` facultatif | affectations |
| `IF c THEN …` [`ELSE …`] | sur une ligne ; `THEN 120` saute à la ligne 120 |
| `FOR I = a TO b` [`STEP s`] … `NEXT` [I] | boucle |
| `WHILE c` … `WEND` | boucle tant que |
| `GOTO n`, `GOSUB n` … `RETURN` | sauts et sous-programmes |
| `DIM T(100)` | un tableau de 0 à 100 (sans `DIM` : de 0 à 10) |
| `END`, `STOP`, `REM …`, `CLS` | fin, arrêt (avec son numéro de ligne), commentaire, effacer le texte |
| `+ - * / MOD`, `= <> < > <= >=`, `AND OR NOT` | vrai vaut 1, faux 0 |
| `RND(n)` (0 à n−1), `ABS(n)`, `LEN(A$)`, `STR$(n)`, `CHR$(n)` | fonctions |

| Pour les jeux | |
|---|---|
| `DECOR n` | affiche le décor n de la banque d'images et crée ses objets (numérotés de 0 à `OBJETS` − 1, dans l'ordre où ils ont été posés dans MONOPIXEL) |
| `CHERCHE("HEROS")`, `CHERCHE("PIECE", I)` | le premier objet de ce lutin, le suivant après I (−1 : aucun) |
| `EST(I, "BLOB")`, `NOM$(I)`, `ACTIF(I)` | ce qu'est un objet, s'il existe encore |
| `CREE("TIR", x, y)` | un nouvel objet (un tir, une explosion…) ; donne son numéro |
| `POSX(I)`, `POSY(I)`, `VITX(I)`, `VITY(I)` | position en pixels, vitesse en 1/16 de pixel par image |
| `PLACE I, x, y`, `VITESSE I, vx, vy` | les changer |
| `GRAVITE I, 1`, `BOITE I, x, y, l, h` | il tombe ; sa boîte de collision |
| `BOUGE I` (ou `BOUGE` : tous) | le déplace contre les tuiles (murs, plateformes) |
| `PATROUILLE I, v` | le fait marcher, demi-tour aux murs et au bord du vide |
| `SOL(I)`, `PLAFOND(I)`, `MUR(I)` | ce que le dernier `BOUGE` a touché |
| `DANGER(I)`, `EAU(I)`, `ECHELLE(I)` | une tuile de cet attribut sous lui |
| `CHOC(I, J)` | les deux objets se touchent |
| `ENLEVE I`, `CACHE I, 1`, `MIROIR I, 1`, `ANIME I, 1`, `IMAGE I, n` | il disparaît, n'est plus dessiné, regarde à gauche, s'anime, montre l'image n |
| `CAMERA I` | la caméra le suit |
| `GAUCHE`, `DROITE`, `HAUT`, `BAS`, `BOUTON(n)`, `APPUI(n)` | la manette 1 (aussi les flèches ; Espace ou Z = bouton 1 ; X ou Alt = bouton 2) ; `APPUI` : vient d'être enfoncé |
| `ATTENDS` [n] | dessine les objets et attend l'image suivante (n images) : une fois par tour de la boucle du jeu |
| `TEXTE x, y, …` | écrit en colonne x (0–39), ligne y (0–29) ; `PRINT` écrit à la suite |
| `SON "SAUT"`, `MUSIQUE`, `SILENCE` | un bruitage par son nom, la musique, le silence |
| `FOND r, v, b`, `TEMPS` | la couleur du ciel ; les images écoulées depuis `RUN` |

À la fin d'un programme, l'image reste jusqu'à une touche ou au bouton 1. Une erreur s'affiche
avec le numéro de sa ligne et cette ligne : `? SYNTAXE EN 150`.

## Les programmes dans la NVRAM, sur l'ordinateur

    python3 ../../tools/basic/basic.py monobasic.sav               # les 8 programmes (aussi monoatelier.sav)
    python3 ../../tools/basic/basic.py monobasic.sav 1 -o jeu.bas  # en texte
    python3 ../../tools/basic/basic.py jeu.bas --to-sav monobasic.sav 2   # un texte écrit sur l'ordinateur

## Limites actuelles

MONOBASIC n'a ni nombres à virgule ni `INPUT` ; il n'a pas non plus de `DATA` / `READ`. Un
programme fait au plus 20 Ko, avec 160 variables, 40 chaînes et 24 tableaux (8192 nombres en
tout). Il interprète le texte converti en jetons, sans compilation. Dans le simulateur, le jeu
d'exemple (une vingtaine d'objets) occupe en moyenne environ un cinquième du processeur.
