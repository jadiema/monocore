# MONOCORE-1

Console virtuelle pour MiSTer (DE10-Nano) : un core FPGA unique et figé sur lequel tournent des jeux d'arcade traduits et des jeux faits maison. Ce dépôt contient la spécification (v0.3), le simulateur de référence, le kit de développement, le manuel du programmeur, une ROM de test et le core FPGA, qui fonctionne sur la carte (image et son en HDMI).

| Dossier | Contenu |
|---|---|
| `SPEC.md` | la spécification complète (CPU, mémoire, vidéo, son, entrées, format de ROM, budget FPGA) |
| `GUIDE.md` | créer sa propre ROM, pas à pas |
| `sim/core.js` | le simulateur (CPU RV32IM, vidéo par lignes, son), utilisable dans le navigateur et dans Node |
| `sim/page.html` | la page du simulateur (modèle) ; `tools/build_page.py` produit `dist/monocore.html` |
| `sdk/` | en-têtes, démarrage, script d'édition de liens, outils PNG et WAV, bibliothèque pour débutants `easy` et outil `build.py` (voir le manuel) |
| `rom/` | la ROM de test (mire et résolutions, plans et raster, sprites, son, contrôles, CPU et DMA) |
| `examples/hello/` | exemple minimal à copier pour démarrer un jeu |
| `tools/run.js` | exécution sans écran : captures et enregistrement du son, pour les tests automatiques |
| `tools/isatest/` | auto-test du jeu d'instructions (même source compilée en natif et en RV32IM) |
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
python3 tools/build_page.py # simulateur autonome -> dist/monocore.html
```

Il faut clang, ld.lld et llvm-objcopy (LLVM), Python 3 avec Pillow et Markdown, et make. Ouvre `dist/monocore.html` dans un navigateur, ou glisse un `.mnc` dessus.

## Tester sans navigateur

```sh
node tools/run.js rom/build/testrom.mnc /tmp/out "5:p1=10 6:p1=0 60:shot=mire"
```

Les actions `p1=`, `p2=` et `sys=` règlent les entrées (bits de `IN_P1`, en hexadécimal) à partir d'une image ; `shot=nom` enregistre une capture PPM. Le son de toute la séance est écrit dans `audio.wav`.

## Ce qui reste à faire

- Core MiSTer : rotation d'écran pour les jeux verticaux, sauvegarde de la NVRAM, DIP switches dans l'OSD.
- Le bloc FM compatible YM2151 (emplacement réservé en `0x3000_1000`).
- Adapter le traducteur statique (déjà utilisé pour Pigskin vers JavaScript) pour qu'il produise du RV32IM.
