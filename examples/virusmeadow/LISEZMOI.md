# VIRUS MEADOW — pong-casse-briques coopératif à 4 joueurs

Quatre raquettes gardent les quatre côtés d'une prairie carrée : J1 à gauche (vert), J2 en haut
(rouge), J3 en bas (bleu), J4 à droite (orange). Au centre, un virus s'étend brique par brique.
Un seul palet, qui prend la couleur du dernier joueur qui l'a touché, casse les briques.

- le virus touche la clôture : la prairie est perdue ;
- le palet sort : un palet perdu, le virus fait une poussée, le joueur qui l'a raté sert à nouveau ;
- toutes les briques détruites : niveau suivant (plus de colonies, croissance plus rapide).

Les jeunes cellules (rose pâle) sont cassées au passage ; les plus anciennes (violettes) perdent un
niveau de résistance à chaque coup et font rebondir le palet.

![VIRUS MEADOW : écran d'accueil et partie](apercu.png)

## Compiler et jouer

```sh
cd examples/virusmeadow
python3 ../../sdk/build.py .        # -> build/virusmeadow.mnc
```

Une version déjà compilée est fournie dans `build/virusmeadow.mnc`.

START sur n'importe quelle manette : rejoindre (à l'écran de sélection ou en pleine partie), puis
pause. Les places libres sont jouées par la console. J1 et J4 : haut / bas ; J2 et J3 : gauche /
droite (croix, stick gauche, spinner ou paddle). Bouton 1 : servir. Sans action pendant 25 s sur
l'écran d'accueil, une démonstration à quatre joueurs console se lance. Le record est gardé en NVRAM.

## Ce que l'exemple montre

| Technique | Où | Fonctions du SDK |
|---|---|---|
| Écran d'accueil en image 8 bpp (256 couleurs, tuiles 16 × 16), fondu par la palette | `show_title_picture`, `title_colours` | clé `maps8`, `mc_layer`, `MC_8BPP` |
| Prairie procédurale en tuiles 8 × 8 avec partage des tuiles identiques, carte gardée pour revenir de l'écran d'accueil | `build_meadow`, `show_meadow` | `mc_chr_upload`, `mc_tilemap_load` |
| Virus sur un plan de tuiles, palette pulsante | `infect`, `heal`, `virus_colours` | `mc_tile`, `mc_color` |
| Encarts sombres derrière le texte, sur un plan dédié | `panel` | `mc_layer` (plan 3) |
| Quatre joueurs, entrées numériques et analogiques | `control_human` | `mc_pad`, `mc_stick_x/y`, `mc_spinner_delta`, `mc_paddle` |
| Joueurs console | `control_ai` | — |

L'image d'accueil (`title.png`) est tirée d'une jaquette générée par IA, recomposée en 368 × 272 et
réduite à 255 couleurs.
