# PLATEFORMES

Un modèle de jeu de plateformes à copier pour faire le sien. Tout le niveau est dans sa banque
`niveau.mgf`, dessinée avec MONOPIXEL : le décor, ce que sont les tuiles et les objets posés
dedans, et ses sons dans `bruits.mfx`, faite avec MONOSFX. Le programme (`main.c`, environ 150 lignes) ne dit que ce que fait chaque sorte d'objet,
d'après le nom de son lutin :

| Lutin | Rôle |
|---|---|
| `HEROS` | le joueur. Il court avec un peu d'inertie et saute avec B1 : plus haut tant qu'on tient le bouton. Bas + B1 descend à travers une plateforme. L'eau, une tuile `DANGER` ou une chute font perdre une vie |
| `PIECE` | 10 points |
| `BLOB` | un ennemi qui marche et se retourne contre un mur ou au bord d'un vide. Sa vitesse est le paramètre P de l'objet. Sauté dessus, il disparaît (50 points) ; touché autrement, il fait perdre une vie |
| `DRAPEAU` | la fin du niveau |

Le jeu enchaîne un écran titre, la partie, puis « BRAVO » ou « PERDU », avec 3 vies. Le record
est sauvé dans la NVRAM (`mj_save`). Les bruitages viennent de `bruits.mfx`, faits avec MONOSFX et
joués par leur nom : `SAUT`, `PIECE`, `ECRASE`, `AIE`, `BRAVO`, `PERDU`, `MENU`. Pour changer un
son, il suffit de le refaire dans MONOSFX sous le même nom et d'exporter la banque.

Dans **MONOATELIER** (`sdk/ATELIER.md`), le jeu prend le niveau, les bruitages et la musique
sauvés dans l'atelier : on les refait dans les éditeurs, puis F5 pour y jouer.

## Construire

    python3 ../../sdk/build.py .         # -> build/plateformes.mnc

Commandes : B1, START, Espace ou Entrée pour commencer ; gauche et droite (manette 1 ou 2, ou les flèches)
pour courir ; B1, B2 ou Espace pour sauter ; bas + saut pour descendre d'une plateforme.

## Faire son niveau

1. Dans MONOPIXEL (page BANQUE), charger la **DEMO**, ou partir d'une nouvelle banque, et la
   modifier.
2. Page DECORS : dessiner le décor, régler les attributs des tuiles (SOLIDE, PLATEF., EAU,
   DANGER…), puis poser les objets avec l'outil OBJETS : un `HEROS`, des `PIECE`, des `BLOB`
   (P = vitesse) et un `DRAPEAU`.
3. Ctrl+S, puis exporter :
   `python3 ../../tools/mgfx/mgfx.py monopixel.sav 1 -o niveau.mgf`. Dans le simulateur :
   **Banques .mgf**.
4. Reconstruire.

Pour une nouvelle sorte d'objet (une clé, un ressort, un ennemi qui vole…), il suffit de dessiner un
lutin, de lui donner un nom, de le poser, puis d'ajouter un cas dans `update()`. Les fonctions
utilisées sont celles de `mjeu` (`sdk/MJEU.md`) et `mgfx` (`sdk/MGFX.md`).

`niveau.mgf` est une copie de la banque de démonstration de MONOPIXEL (`tools/mgfx/make_demo.py`),
`bruits.mfx` celle de MONOSFX (`tools/msfx/make_demo.py`).
