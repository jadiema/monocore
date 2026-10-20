# MONOCORE-1

Console virtuelle pour MiSTer (DE10-Nano) : un core FPGA unique et figé sur lequel tournent des jeux d'arcade traduits et des jeux faits maison. Ce dépôt contient la spécification (v0.4), le simulateur de référence, le kit de développement, le manuel du programmeur, une ROM de test et le core FPGA, qui fonctionne sur la carte (image et son en HDMI).

| Dossier | Contenu |
|---|---|
| `SPEC.md` | la spécification complète (CPU, mémoire, vidéo, son, entrées, format de ROM, budget FPGA) |
| `GUIDE.md` | créer sa propre ROM, pas à pas |
| `sim/core.js` | le simulateur (CPU RV32IM, vidéo par lignes, son), utilisable dans le navigateur et dans Node |
| `sim/page.html` | la page du simulateur (modèle) ; `tools/build_page.py` produit `dist/monocore.html` |
| `sdk/` | en-têtes, démarrage, script d'édition de liens, outils PNG et WAV, outil `build.py`, et deux bibliothèques : `easy` pour débuter (voir le manuel) et `mc`, le SDK bas niveau avec sa petite libc, pour les jeux complexes et les portages (voir `sdk/SDK_MC.md`) |
| `rom/` | la ROM de test (mire et résolutions, plans et raster, sprites, son, contrôles, CPU et DMA) |
| `examples/hello/` | exemple minimal à copier pour démarrer un jeu (registres bruts, `Makefile`) |
| `examples/boot/` | l'écran d'accueil de la console (`boot.rom`), façon intro Amiga, écrit avec `mc` |
| `examples/nebula/` | NEBULA, un shoot'em up complet écrit avec `mc` : décors et sprites générés au démarrage, parallaxe, raster, 233 sprites |
| `examples/horizon/` | HORIZON, une course en fausse 3D écrite avec `mc` (style OutRun) : route dessinée ligne par ligne, virages, objets zoomés selon la distance |
| `examples/aube/` | AUBE, l'écran d'accueil actuel (`boot.rom`) : animation de palette et musique orchestrale synthétisée |
| `examples/aigle/` | portage de L'Aigle d'Or (MO5, 1985) : BASIC traduit à la main, code 6809 traduit automatiquement (`tools/recomp6809.py`) ; données tirées de sa propre copie |
| `examples/pbobble2/` | portage de Puzzle Bobble 2 / Bust-A-Move Again (Neo Geo, Taito 1995) : code 68000 traduit par `tools/recomp68k`, runtime Neo Geo, couche MONOCORE ; jouable, muet pour l'instant ; données tirées de sa propre copie |
| `examples/test_inputs/` | affiche toutes les entrées (manettes, sticks, paddles, spinners, clavier) |
| `examples/test_sdk/` | test du SDK `mc` (raster, timers, `malloc`, `printf`) |
| `tools/run.js` | exécution sans écran : captures et enregistrement du son, pour les tests automatiques |
| `tools/isatest/` | auto-test du jeu d'instructions (même source compilée en natif et en RV32IM) |
| `tools/recomp68k/` | traducteur statique 68000 → C → RV32IM, validé contre Musashi par un banc différentiel, voir son `LISEZMOI.md` |
| `tools/cart/` | `cart_split.py` : découpe une ROM en images pour une ou deux Flash SPI W25Q64 (cartouche sur le port USER) |
| `rtl/` | le core FPGA en Verilog (CPU, mémoires, E/S, DMA, rendu vidéo, son) et ses bancs de test Verilator |
| `quartus_trial/` | essai de compilation du core seul dans Quartus (ressources, fréquence maximale), voir son `LISEZMOI.md` |
| `manual/` | le manuel du programmeur (`MONOCORE-1_Manuel_du_programmeur.pdf`), ses exemples (`examples/`, un dossier par chapitre) et l'outil qui le régénère (`tools/manual.py`) |
| `mister/` | le core MiSTer (framework `Template_MiSTer`, projet `MONOCORE.qpf`), voir son `LISEZMOI.md` ; ses modules propres sont dans `rtl/mister/` (contrôleur SDRAM, balayage vidéo, chargement) |
| `tools/apu/` | journaux d'écritures son, modèle de référence et comparaison au bit près du moteur son |
| `tools/vidfuzz/` | ROM de test vidéo aléatoire et scripts de comparaison simulateur / RTL image par image |
| `tools/cmp_ppm.py` | compare deux captures d'écran au pixel près |
| `tools/cmp_mem.py` | compare les mémoires vidéo et son entre le simulateur et le RTL |

## Construire

```sh
make -C rom                 # ROM de test -> rom/build/testrom.mnc
make -C examples/hello      # exemple -> examples/hello/build/hello.mnc
cd examples/nebula && python3 ../../sdk/build.py .   # exemple mc -> build/nebula.mnc
python3 tools/build_page.py # simulateur autonome -> dist/monocore.html
```

Il faut clang, ld.lld et llvm-objcopy (LLVM), Python 3 avec Pillow et Markdown, et make. Ouvre `dist/monocore.html` dans un navigateur, ou glisse un `.mnc` dessus.

## Tester sans navigateur

```sh
node tools/run.js rom/build/testrom.mnc /tmp/out "5:p1=10 6:p1=0 60:shot=mire"
```

Les actions `p1=`, `p2=` et `sys=` règlent les entrées (bits de `IN_P1`, en hexadécimal) à partir d'une image ; `shot=nom` enregistre une capture PPM. Le son de toute la séance est écrit dans `audio.wav`.

## Sur le MiSTer

Copie le core (`mister/output_files/MONOCORE.rbf`, renommé `MONOCORE_aaaammjj.rbf`) dans `_Console`, et les ROMs `.mnc` dans `games/MONOCORE`. Un fichier `games/MONOCORE/boot.rom` est chargé automatiquement au lancement du core : c'est là que va l'écran d'accueil (`examples/boot`). Détails dans `mister/LISEZMOI.md`.

## Ce qui reste à faire

- Core MiSTer : rotation d'écran pour les jeux verticaux, sauvegarde de la NVRAM, DIP switches dans l'OSD.
- Le bloc FM compatible YM2151 (emplacement réservé en `0x3000_1000`).
- Traducteurs statiques vers RV32IM : 6809 (`examples/aigle`) et 68000 (`tools/recomp68k`) faits, Z80 à faire.
- Puzzle Bobble 2 : le son (pilote Z80 et YM2610 pré-rendus hors ligne, en cours) et l'essai sur la carte.
