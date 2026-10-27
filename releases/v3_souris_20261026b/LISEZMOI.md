# MONOCORE-1, matériel version 3 (souris)

| Fichier | Rôle | SHA-256 |
|---|---|---|
| `MONOCORE_20261026b.rbf` | core, à copier dans `_Console` | `519acd88a93d04f9e4d8891bb7573151c4ca66cdad7ff99ff391915aef2df52c` |

Sources : commit `be98b62`. Compilé avec Quartus Prime 17.0.2 Lite le 26 octobre 2026, timing respecté (marge
de setup la plus faible 0,826 ns sur l'horloge SDRAM à 100 MHz), testé sur DE10-Nano (souris, molette,
MONOTRACKER). Utilise le `boot.rom` de `v4_midi_20261026c/`.

## Projet Quartus de cette version

`quartus/` contient le projet Quartus complet tel qu'il était au commit `be98b62` : `quartus/mister/` (projet,
framework MiSTer `sys/`, contraintes) et `quartus/rtl/` (sources Verilog du core et processeur Hazard3), rangés
comme dans le dépôt pour que les chemins `../rtl/...` de `files.qip` restent valables. Pour recompiler
exactement cette version : ouvrir `quartus/mister/MONOCORE.qpf` avec Quartus Prime 17.0.2 Lite, puis
*Processing > Start Compilation* ; le core sort dans `quartus/mister/output_files/MONOCORE.rbf`.
