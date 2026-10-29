# Filtres vidéo MiSTer du simulateur

Copie, sans modification, des fichiers de la carte SD de la MiSTer utilisés par la liste *Filtres vidéo MiSTer* de la page du simulateur (`sim/page.html`, embarqués par `tools/build_page.py`) :

| Dossier | Origine | Version copiée |
|---|---|---|
| `Presets/` | [MiSTer-devel/Presets_MiSTer](https://github.com/MiSTer-devel/Presets_MiSTer) (RGarciaLago et contributeurs) | `0b6571e` (2026-08-14) |
| `Filters/`, `Gamma/` | [MiSTer-devel/Filters_MiSTer](https://github.com/MiSTer-devel/Filters_MiSTer) (Soltan_G42, Tonurics et contributeurs) | `00416db` (2024-07-10) |
| `Shadow_Masks/` | [MiSTer-devel/ShadowMasks_MiSTer](https://github.com/MiSTer-devel/ShadowMasks_MiSTer) (TrashUncle et contributeurs) | `9f0948c` (2023-09-19) |

Seuls les préréglages *Display Specific*, *General Hardware* et généraux sont repris, avec les fichiers qu'ils nomment. *Faded Tube* est laissé de côté : son masque n'est pas dans le dépôt des masques.

Le calcul suit le framework MiSTer (`mister/sys/`) : gamma sur les pixels du core (`gamma_corr.sv`), filtre polyphasé à 4 points, horizontal puis vertical (`ascal.vhd` ; 64 ou 256 phases, coefficients sur 128 ou sur 256 en `10bit`), puis masque d'écran sur les pixels de sortie (`shadowmask.sv` : couleur, intensité allumée `(16 + on) / 16`, éteinte `off / 16`, sections `Resolution=` choisies par la hauteur de sortie, modes `2x` et `rotated`). Les pixels de sortie sont ceux de l'écran du navigateur (taille CSS × `devicePixelRatio`), comme les pixels HDMI de la MiSTer. Les jeux de coefficients `adaptive` (sombre / clair) sont mélangés selon la luminance du pixel le plus proche ; le placement exact des phases d'`ascal.vhd` n'est pas reproduit au pixel près.

Le bouton *Importer de la MiSTer…* accepte d'autres fichiers pris sur la carte SD : un `.ini` de `Presets` avec les `.txt` qu'il nomme (cherchés par leur nom), ou des `.txt` seuls de `Filters`, `Gamma` ou `Shadow_Masks`, qui remplacent la partie correspondante du réglage choisi avant.
