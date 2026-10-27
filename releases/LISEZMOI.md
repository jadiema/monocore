# Cores compilés

Une version du core MONOCORE-1 par dossier, prête à l'emploi pour la MiSTer : copiez le fichier `.rbf` dans
`_Console` sur la carte SD, et le `boot.rom` du même dossier (écran d'accueil AUBE) dans `games/MONOCORE`
(les autres ROM `.mnc` vont aussi dans `games/MONOCORE`). Pour recompiler vous-même : `mister/LISEZMOI.md`.
Prenez le plus récent qui fonctionne : un core fait tourner toutes les ROM des versions précédentes.

| Dossier | Matériel | Contenu | État |
|---|---|---|---|
| `v5_haute_resolution/` | version 5 | comme la version 4, plus la **haute résolution** (jusqu'à 640 pixels de large avec 270 lignes au plus) et le **mode entrelacé** 640 × 480 ; AUBE en 640 × 480i | testée le 27 octobre 2026, **dernière version** |
| `v4_midi_20261026c/` | version 4 | comme la version 3, plus l'**entrée MIDI** : clavier MIDI USB par MidiLink (menu du core : *UART : MIDI*, connexion USB) | testée, version précédente (sans haute résolution) |
| `v3_souris_20261026b/` | version 3 | 4 manettes, analogique, clavier, **souris** (molette positive vers l'avant), NVRAM sur la carte SD, cartouche SPI | testée |

Chaque dossier a son `LISEZMOI.md` : commit des sources, date et outil de compilation, tests, SHA-256.
Il contient aussi `quartus/`, le projet Quartus complet de cette version (`quartus/mister/MONOCORE.qpf`),
pour la recompiler à l'identique sans revenir en arrière dans l'historique du dépôt.
