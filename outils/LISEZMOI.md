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
| `MONOATELIER.mnc` | l'atelier : les cinq programmes ci-dessous dans une seule ROM, avec une seule sauvegarde | 7 480 Ko | `34db2b0bd24155ee…` |
| `MONOPIXEL.mnc` | les images : planche, palettes, lutins animés, décors, attributs des tuiles, objets | 105 Ko | `aac65587b3f07dc6…` |
| `MONOSFX.mnc` | les bruitages : 32 sons PSG par banque, générateurs | 67 Ko | `39910812c50f70e2…` |
| `MONOTRACKER.mnc` | la musique : morceaux de 16 pistes (PSG et échantillons), clavier MIDI | 1 314 Ko | `f25e68d67bd4c2da…` |
| `MONOBASIC.mnc` | le BASIC : écrire un jeu sur la console | 1 336 Ko | `8ba03bd59c0bc2be…` |
| `PLATEFORMES.mnc` | un jeu de plateformes tout fait, à refaire avec les outils | 1 281 Ko | `901794b685c11fbd…` |

Construits par `python3 tools/mkoutils.py` le 2026-10-29, à partir du commit `c81053e`.
Dans le simulateur (`dist/monocore.html`), MONOATELIER et chacun des outils sont dans la liste des ROM intégrées.
