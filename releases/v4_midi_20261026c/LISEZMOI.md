# MONOCORE-1, matériel version 4 (entrée MIDI)

| Fichier | Rôle | SHA-256 |
|---|---|---|
| `MONOCORE_20261026c_midi.rbf` | core, à copier dans `_Console` | `02836ad1bdd6c01ffe495ac2d03d4fc29e2d68051069c4adc7f84ded4b4573db` |
| `boot.rom` | AUBE en 360 × 270 (`examples/aube` du commit `cbb0c09`), à copier dans `games/MONOCORE` | `c96cfbf49dabf751d919a1387fddf2ccd96df8f133039ba95990db1fef51b642` |

Sources : commit `eba9fa0`. Compilé avec Quartus Prime 17.0.2 Lite le 26 octobre 2026, testé sur DE10-Nano avec
un Novation Launchkey 49 branché en USB (MidiLink en mode USB) et MONOTRACKER. Mode d'emploi :
`manual/MONOCORE-1_Guide_MIDI.pdf`.

Ce core ne connaît pas la haute résolution : garder ce `boot.rom` avec lui (l'AUBE de la version 5 est en
640 × 480 entrelacé et serait coupée).
