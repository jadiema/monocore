# Core compilé

Le core MONOCORE-1 prêt à l'emploi pour la MiSTer : copiez le fichier `.rbf` dans `_Console` sur la carte
SD (les ROM `.mnc` vont dans `games/MONOCORE`). Pour le recompiler vous-même : `mister/LISEZMOI.md`.

| Fichier | Matériel | Contenu | Sources |
|---|---|---|---|
| `MONOCORE_20261026b.rbf` | version 3 | 4 manettes, analogique, clavier, **souris** (molette positive vers l'avant), NVRAM sur la carte SD, cartouche SPI | commit `be98b62` |

`MONOCORE_20261026b.rbf` : compilé avec Quartus Prime 17.0.2 Lite le 26 octobre 2026, timing respecté (marge de
setup la plus faible 0,826 ns sur l'horloge SDRAM à 100 MHz), testé sur DE10-Nano (souris, molette, MONOTRACKER).
SHA-256 : `519acd88a93d04f9e4d8891bb7573151c4ca66cdad7ff99ff391915aef2df52c`

Les sources de `main` sont maintenant en version 4 du matériel (entrée MIDI) : ce core version 3 fait tourner
toutes les ROM, mais sans le clavier MIDI. Recompilez le core pour l'avoir, puis ajoutez ici le nouveau `.rbf`.
