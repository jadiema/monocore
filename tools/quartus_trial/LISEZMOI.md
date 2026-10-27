# Essai de compilation MONOCORE-1 (Quartus, DE10-Nano)

Ce dossier compile le core MONOCORE-1 seul pour le Cyclone V du DE10-Nano. On cherche trois informations :

1. si Quartus accepte le code sans erreur ;
2. quelle part du FPGA il occupe (logique, mémoires, multiplieurs) ;
3. s'il tient la fréquence visée de 50 MHz, et sinon quels chemins sont trop lents.

Ce n'est **pas** un core MiSTer : il n'y a ni affichage, ni SDRAM, ni menu.

> **Ne pas programmer la carte avec le fichier `.sof` produit.** Seule l'horloge est placée sur une broche connue ; Quartus place les autres broches au hasard, et une sortie pourrait se retrouver sur une broche déjà pilotée par la carte.

## Essai n° 5

L'essai n° 4 tenait les 50 MHz avec seulement 0,09 ns de marge ; cette version raccourcit le chemin le plus serré (processeur vers mémoires vidéo). **Supprimer l'ancien dossier `monocore`** (avec ses sous-dossiers `db`, `incremental_db` et `output_files`) et décompresser celui-ci à la place, pour que Quartus reparte de zéro. Le déroulement et les fichiers à renvoyer sont les mêmes ; joindre en plus `output_files/monocore_trial.fit.summary` (occupation du FPGA).

## Prérequis

- Quartus Prime **Lite 17.0** (la version utilisée par MiSTer), avec la prise en charge du **Cyclone V** installée.
- Le dossier `monocore` complet : ce projet lit les sources dans `../../rtl`.

## Lancer la compilation

**Avec l'interface :**

1. *File > Open Project*, choisir `monocore_trial.qpf`.
2. *Processing > Start Compilation*. Compter 10 à 30 minutes selon la machine.
3. À la fin, *Tools > Tcl Scripts…*, choisir `trial_report.tcl`, puis *Run*. Le script lance lui-même l'analyse de timing (`quartus_sta`, 1 à 3 minutes, l'interface est figée pendant ce temps) et écrit les fichiers `trial_*.txt` dans ce dossier (compter 5 minutes de plus pour le rapport bloc par bloc). On peut aussi le lancer dans une invite de commande : `quartus_sta -t trial_report.tcl`.

**En ligne de commande :** lancer `compile.bat` (Windows) ou `compile.sh` (Linux). Le dossier `bin64` de Quartus doit être dans le `PATH`, par exemple `C:\intelFPGA_lite\17.0\quartus\bin64`.

## Fichiers à renvoyer

| Fichier | Contenu |
|---|---|
| `output_files/monocore_trial.flow.rpt` | déroulement de la compilation, erreurs éventuelles |
| `output_files/monocore_trial.fit.summary` | ressources utilisées |
| `trial_fmax_slow.txt` | fréquence maximale atteinte (modèle lent, le plus pessimiste) |
| `trial_paths.txt` | détail des 40 chemins les plus lents |
| `trial_paths_summary.txt` | liste des 200 chemins les plus lents |
| `trial_blocks.txt` | pire chemin de chaque bloc (processeur, caches, rendu, son…) |
| `trial_blocks_detail.txt` | détail des 10 pires chemins de chaque bloc |
| `output_files/monocore_trial.map.rpt` | ressources module par module |

Si le script de rapport échoue, `output_files/monocore_trial.sta.rpt` contient déjà la fréquence maximale et les chemins les plus lents : le renvoyer à la place des fichiers `trial_*.txt`.

Si la compilation s'arrête sur une erreur, renvoyer plutôt tous les fichiers `.rpt` du dossier `output_files` : celui de l'étape qui a échoué (`.map.rpt` pour la synthèse, `.fit.rpt` pour le placement) contient le message. Le fichier `.map.rpt` (rapport de synthèse) est utile dans tous les cas : il détaille les ressources module par module.

## Avertissements normaux

- *No exact pin location assignment* pour `rst_n_pin`, `sin` et `sout` : voulu, ces broches ne servent qu'à garder la logique en vie.
- Avertissements de largeur (*truncated value*, *assigned a value but never read*) dans `hazard3/` et dans `mc_*.v` : sans conséquence.
- Timing en rouge (*Timing requirements not met*) : c'est précisément ce qu'on veut mesurer, la compilation va quand même au bout.

## Ce que contient l'essai

`monocore_trial_top.v` enveloppe le core (`../../rtl/mc_core.v`). Toutes les entrées du core (données de la ROM, manettes, DIP switches, taille de la ROM) viennent d'un registre à décalage alimenté par une seule broche, et toutes ses sorties (pixels, son, adresses mémoire) sont combinées en 8 broches. Quartus ne peut donc rien supprimer, et le résultat reflète le core réel.

La contrainte (`monocore_trial.sdc`) demande 50 MHz sur l'horloge, la fréquence de l'oscillateur de la carte sur cette broche. Sur la vraie carte, cette horloge viendra d'une PLL ; pour l'essai, elle est déclarée directement sur la broche de l'oscillateur.

Les chiffres concernent le core seul. Le framework MiSTer (menu, mise à l'échelle, HDMI) prendra en plus environ un quart de la logique et une partie des blocs mémoire.
