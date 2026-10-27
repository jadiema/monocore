# Les outils de la MONOCORE-1, prêts à l'emploi

Copiez les fichiers `.mnc` dans `games/MONOCORE` sur la carte SD de la MiSTer. Ils demandent le core en
**version 5** (`releases/v5_haute_resolution/`), branché à une **souris** et un **clavier** USB.

- **`MONOATELIER.mnc`** suffit pour tout faire. Il réunit les cinq programmes, qui partagent une seule
  sauvegarde (`saves/MONOCORE/MONOATELIER.sav`) : ce qu'on fait dans un outil sert aussitôt aux autres et
  au jeu. Le mode d'emploi est `manual/MONOATELIER_Manuel.pdf`.
- Les autres fichiers sont les mêmes programmes **seuls**. Chacun a sa propre sauvegarde
  (`saves/MONOCORE/MONOPIXEL.sav`…). `tools/atelier/atelier.py --import` en reprend le contenu dans l'atelier.

| Fichier | Contenu | Taille | SHA-256 |
|---|---|---|---|
| `MONOATELIER.mnc` | l'atelier : les cinq programmes ci-dessous dans une seule ROM, avec une seule sauvegarde | 7 477 Ko | `fdc02a9dfaa28977…` |
| `MONOPIXEL.mnc` | les images : planche, palettes, lutins animés, décors, attributs des tuiles, objets | 102 Ko | `0a65bb37a60c2dcc…` |
| `MONOSFX.mnc` | les bruitages : 32 sons PSG par banque, générateurs | 64 Ko | `017857c74809d3d0…` |
| `MONOTRACKER.mnc` | la musique : morceaux de 16 pistes (PSG et échantillons), clavier MIDI | 1 311 Ko | `ba91bbc7da369c43…` |
| `MONOBASIC.mnc` | le BASIC : écrire un jeu sur la console | 1 333 Ko | `cf284d6240d17f7d…` |
| `PLATEFORMES.mnc` | un jeu de plateformes tout fait, à refaire avec les outils | 1 277 Ko | `55b09730df97737b…` |

Construits par `python3 tools/mkoutils.py` le 2026-10-27, à partir du commit `0639fc1`.
Dans le simulateur (`dist/monocore.html`), MONOATELIER et chacun des outils sont dans la liste des ROM intégrées.
