# MONOCORE-1, matériel version 5 (haute résolution et entrelacé)

| Fichier | Rôle | SHA-256 |
|---|---|---|
| `boot.rom` | AUBE en 640 × 480 entrelacé (`examples/aube`), à copier dans `games/MONOCORE` | `3f6f0609d62fccc561c78d26a0fb8f10ec00b0d765318f9122da05a334a6ef29` |
| `MONOCORE_20261027_HiResb.rbf` | core, à copier dans `_Console` | `326ee2c60e0d767feb19a2fe6b495e9133fb07cec394e46b3a2085b25bc50bb7` |

Sources : commits « Haute resolution et mode entrelace (materiel version 5) » et « VGA_F1 : ordre des trames corrige » (projet Quartus de `quartus/` à jour). Nouveautés (SPEC v0.7, §4.1) :

- largeur jusqu'à 640 pixels si l'image a au plus 270 lignes (640 × 270 = exactement 4 fois l'écran en 1080p) ;
- mode entrelacé (bit 10 de `DISP_CTRL`, ou `mc_video_mode(640, 480)` dans le SDK) : deux trames de 240 lignes,
  toutes les capacités graphiques conservées ; parité de la trame au bit 2 de `STATUS` ;
- `HW_VERSION` = 5.

Vérifié en simulation : simulateur et RTL identiques au pixel près sur toutes les régressions, dont 161 images
de la ROM aléatoire haute résolution et le banc système (vraie sortie vidéo) ; fenêtre de rendu au plus
2 593 cycles en 640 × 480i (lignes de 3 156 cycles).

Compilé avec Quartus Prime 17.0.2 Lite le 27 octobre 2026, à partir des sources de `quartus/` (commit `adf60b1`). Timing respecté : à la première compilation (même code, seule la polarité de `VGA_F1` différait), marge de setup la plus faible 0,392 ns à 50 MHz et 0,559 ns à 100 MHz (SDRAM), hold 0,243 ns ; 40 % des ALM, 65 % de la mémoire bloc, 51 % des DSP.

Testé sur DE10-Nano le 27 octobre 2026 (écran HP L1906) : AUBE en 640 × 480 entrelacé nette, textes et décor stables. Restent de très légers scintillements sur ce qui bouge (poussières de lumière, pulsation des couleurs), limite normale du désentrelacement par tissage.

Points vérifiés sur la carte :

1. Quartus : le timing à 50 MHz (les compteurs de position passent de 9 à 10 bits).
2. HDMI : l'ordre des trames. Premier essai (27 octobre) : avec `f1 <= interlace && !field;`, les paires
   de lignes étaient échangées (textes d'AUBE en dents de scie). Corrigé en `f1 <= interlace && field;` dans
   `rtl/mister/mc_scanout.v` : le scaler lit `VGA_F1` sur la dernière ligne de la trame précédente.
3. Laisser le désentrelacement de `MiSTer.ini` sur son réglage par défaut (le mode « bob » fait trembler l'image).

## Projet Quartus de cette version

`quartus/` contient le projet Quartus complet tel qu'il était au commit `f687e56` : `quartus/mister/` (projet,
framework MiSTer `sys/`, contraintes) et `quartus/rtl/` (sources Verilog du core et processeur Hazard3), rangés
comme dans le dépôt pour que les chemins `../rtl/...` de `files.qip` restent valables. Pour recompiler
exactement cette version : ouvrir `quartus/mister/MONOCORE.qpf` avec Quartus Prime 17.0.2 Lite, puis
*Processing > Start Compilation* ; le core sort dans `quartus/mister/output_files/MONOCORE.rbf`.
