# L'Aigle d'Or sur MONOCORE-1

Portage natif de L'Aigle d'Or (Loriciels, 1985, Louis-Marie Rocques), version MO5, pour un usage
personnel. Ce n'est pas un émulateur : le programme BASIC du jeu est traduit à la main en C
(`game.c`), et le code machine 6809 (moteur, dessins, commandes) est traduit automatiquement en C
par `tools/recomp6809.py` (`aigle_code.c`). Le reste du MO5 dont le jeu a besoin (mémoire vidéo
forme/couleur, texte, `PSET`/`LINE`/`BOX`, `SCREEN`, `CONSOLE`, `PLAY`) est réécrit dans `mo5rt.c`.

## Préparer les données (une fois)

Les données du jeu ne sont pas dans le dépôt. Elles se fabriquent à partir de ta propre copie :

    python3 tools/prepare.py chemin/l-aigle-d-or_mo5.sd chemin/l-aigle-d-or_win.exe

La disquette `.sd` donne l'image mémoire du jeu (`aigle_image.c`) puis le code traduit
(`aigle_code.c`) ; l'exécutable DCMOTO donne la police du MO5 (`mo5font.c`) et les images de
l'écran d'accueil (`intro_data.c`), il faut `upx`.
Ces fichiers sont dans `.gitignore` : **ne les publie pas**.

## Construire

    python3 ../../sdk/build.py .        # -> build/aigle.mnc

## Commandes à la manette

| Commande | Bouton |
|---|---|
| Marcher / courir (deux appuis) | croix |
| Arrêt / debout | START |
| Prendre, Ouvrir | B1, B2 |
| Saut en longueur, saut en hauteur | B3, B4 |
| Accroupir, Grimper | B5, B6 |
| Boire, Lire, Inventaire, Suicide | COIN + B1, B2, B3, B4 |
| Objet en main précédent / suivant | COIN + gauche / droite |
| Menus (1/2, O/N, achats 1-4, 0 pour sortir) | B1, B2… (B5 = 0 chez le marchand) |

## Au clavier

Avec un clavier USB sur la MiSTer (matériel version 2), le jeu se joue aussi avec les touches
d'origine du MO5, à la même place sur un clavier AZERTY : flèches, espace, A, B, S, D, O, P, L, G,
I, Q et 0 à 6 (rangée du haut ou pavé numérique). Les menus acceptent 1/2, O/N et 0-4.

## Banc de test sur PC

    gcc -O1 -o pc/game pc/pc_main.c intro.c game.c mo5rt.c mo5font.c aigle_image.c intro_data.c aigle_code.c -I.
    cd pc && ./game "5:k41 300:s 301:q" "1"     # touche MO5 41 (gauche) tenue, capture au tour 300
    python3 ../tools/vram2png.py shot01.vram shot01.png

## Points techniques

- Le moteur graphique du jeu se modifie lui-même pour choisir le mode de dessin (19 emplacements,
  table `SMC` du traducteur) : chaque emplacement est traduit dans toutes ses variantes.
- Le clavier est lu touche par touche (appel SWI $0C du moniteur) : `main.c` traduit la manette
  en numéros de touche MO5.
- Vitesse : un tour de la boucle du jeu toutes les 5 images (`TICK_FRAMES`), à comparer avec le
  MO5 réel ou DCMOTO. Charge CPU mesurée dans le simulateur : environ 4 % en moyenne.
- `RND` avec argument négatif est remplacé par une fonction déterministe : chaque salle garde
  toujours le même décor, mais pas exactement celui de l'original.
- L'écran d'accueil (`intro.c`) est le chargeur de la version cassette, plus propre que celui de la
  disquette (dont les fichiers d'intro ont des octets dupliqués). Il utilise le même moteur de dessin
  que le jeu, donc le même code traduit. La page des commandes décrit la manette.
