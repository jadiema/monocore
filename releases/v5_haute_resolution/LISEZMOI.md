# MONOCORE-1, matériel version 5 (haute résolution et entrelacé) — à compiler

| Fichier | Rôle | SHA-256 |
|---|---|---|
| `boot.rom` | AUBE en 640 × 480 entrelacé (`examples/aube`), à copier dans `games/MONOCORE` | `e9f2d7ba177e4626a8db37248e9dcaf4340f46abdf663ee48bd050498f55e85a` |
| `MONOCORE_aaaammjj_hires.rbf` | core : **pas encore compilé**, à ajouter ici après le test sur la carte | |

Sources : commit « Haute resolution et mode entrelace (materiel version 5) ». Nouveautés (SPEC v0.7, §4.1) :

- largeur jusqu'à 640 pixels si l'image a au plus 270 lignes (640 × 270 = exactement 4 fois l'écran en 1080p) ;
- mode entrelacé (bit 10 de `DISP_CTRL`, ou `mc_video_mode(640, 480)` dans le SDK) : deux trames de 240 lignes,
  toutes les capacités graphiques conservées ; parité de la trame au bit 2 de `STATUS` ;
- `HW_VERSION` = 5.

Vérifié en simulation : simulateur et RTL identiques au pixel près sur toutes les régressions, dont 161 images
de la ROM aléatoire haute résolution et le banc système (vraie sortie vidéo) ; fenêtre de rendu au plus
2 593 cycles en 640 × 480i (lignes de 3 156 cycles).

À vérifier sur la carte :

1. Quartus : le timing à 50 MHz (les compteurs de position passent de 9 à 10 bits).
2. HDMI : l'ordre des trames. Si les textes d'AUBE paraissent dédoublés ou tremblants, remplacer
   `f1 <= interlace && !field;` par `f1 <= interlace && field;` dans `rtl/mister/mc_scanout.v`.
3. Laisser le désentrelacement de `MiSTer.ini` sur son réglage par défaut (le mode « bob » fait trembler l'image).

## Projet Quartus de cette version

`quartus/` contient le projet Quartus complet tel qu'il était au commit `f687e56` : `quartus/mister/` (projet,
framework MiSTer `sys/`, contraintes) et `quartus/rtl/` (sources Verilog du core et processeur Hazard3), rangés
comme dans le dépôt pour que les chemins `../rtl/...` de `files.qip` restent valables. Pour recompiler
exactement cette version : ouvrir `quartus/mister/MONOCORE.qpf` avec Quartus Prime 17.0.2 Lite, puis
*Processing > Start Compilation* ; le core sort dans `quartus/mister/output_files/MONOCORE.rbf`.
