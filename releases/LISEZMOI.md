# Core compilé

Le core MONOCORE-1 prêt à l'emploi pour la MiSTer : copiez le fichier `.rbf` dans `_Console` sur la carte
SD (les ROM `.mnc` vont dans `games/MONOCORE`). Pour le recompiler vous-même : `mister/LISEZMOI.md`.
Prenez le plus récent : il fait tourner toutes les ROM des versions précédentes.

| Fichier | Matériel | Contenu | Sources |
|---|---|---|---|
| `MONOCORE_20261026c_midi.rbf` | version 4 | comme la version 3, plus l'**entrée MIDI** : clavier MIDI USB par MidiLink (menu du core : *UART : MIDI*, connexion USB) | commit `eba9fa0` |
| `MONOCORE_20261026b.rbf` | version 3 | 4 manettes, analogique, clavier, **souris** (molette positive vers l'avant), NVRAM sur la carte SD, cartouche SPI | commit `be98b62` |

`MONOCORE_20261026c_midi.rbf` : compilé avec Quartus Prime 17.0.2 Lite le 26 octobre 2026, testé sur DE10-Nano avec un
Novation Launchkey 49 branché en USB (MidiLink en mode USB) et MONOTRACKER. Mode d'emploi :
`manual/MONOCORE-1_Guide_MIDI.pdf`.
SHA-256 : `02836ad1bdd6c01ffe495ac2d03d4fc29e2d68051069c4adc7f84ded4b4573db`

`MONOCORE_20261026b.rbf` : compilé avec Quartus Prime 17.0.2 Lite le 26 octobre 2026, timing respecté (marge de
setup la plus faible 0,826 ns sur l'horloge SDRAM à 100 MHz), testé sur DE10-Nano (souris, molette, MONOTRACKER).
SHA-256 : `519acd88a93d04f9e4d8891bb7573151c4ca66cdad7ff99ff391915aef2df52c`
