# MONOCORE-1 — notes de travail (pour Claude et pour les contributeurs)

Console virtuelle figée pour MiSTer (DE10-Nano, Cyclone V 5CSEBA6U23I7). Un seul bitstream ; les jeux d'arcade sont **traduits** vers elle (recompilation statique vers RV32IM), ils ne sont pas émulés. Voir `README.md` pour la carte du dépôt, `SPEC.md` (v0.4) pour la référence matérielle.

## Conventions

- **Langue** : la documentation et les textes à l'écran sont en français. **Tout ce qui est technique est en anglais** : noms de fonctions, de fichiers, de variables, de registres, clés de configuration, réglages, commentaires dans le code. Ne jamais mélanger les deux langues dans la syntaxe du code.
- Le simulateur JavaScript (`sim/core.js`) est la **référence** : le RTL doit produire les mêmes images au pixel près et le même son au bit près. Toute modification du comportement se fait d'abord dans `SPEC.md`, puis dans le simulateur et le RTL ensemble, et se vérifie avec les tests de régression.
- Une modification du RTL doit garder le timing à 50 MHz (marge actuelle : 1,32 ns, essai Quartus n° 5).

## Décisions d'architecture (ne pas remettre en cause sans raison)

- **Une seule horloge de 50 MHz** pour toute la console. Le CPU Hazard3 RV32IM (Apache 2.0, non modifié, `rtl/hazard3`) plafonne vers 53 MHz sur ce FPGA ; chemin critique décodage → x_stall_on_raw → d_htrans → bus. Les mémoires bloc GPU/WRAM/NVRAM sont lues de façon spéculative dès que `hready_nc` est haut pour sortir `d_htrans` du chemin critique.
- Ligne = 4 × ⌊5e7 / (240 × lignes totales)⌋ cycles (3156 à 240 lignes, 2668 à 288). Un pixel tous les 4 cycles (12,5 MHz). Échantillon son au cycle ⌈k × 5e7 / 48000⌉.
- Budgets par ligne : 64 sprites, 1280 pixels de sprites, 512 mots de graphismes. Son : 8 voix PCM/ADPCM + 8 canaux PSG. ROM ≤ 32 Mo. Bloc FM (YM2151) réservé en `0x3000_1000`, **pas encore fait**.
- SDRAM à 100 MHz (horloge 2× en phase) : une rangée ouverte par banque, BL2, CL2 ; priorité rafraîchissement > écriture > graphismes > ROM (ROM servie après 8 mots graphiques d'affilée). Option OSD « Lecture SDRAM » Normale/Retardée = capture décalée d'un cycle (`sd_rd_late`).
- Sortie : double tampon de ligne, HDMI via le framework MiSTer (scaler), son en HDMI.

## Construire et tester

Outils : clang + ld.lld + llvm-objcopy, Python 3 (Pillow, Markdown), make, Node, Verilator 5 (`pip install verilator`), Quartus Prime Lite 17.0 pour le core MiSTer.

```sh
make -C rom                       # ROM de test -> rom/build/testrom.mnc
python3 tools/build_page.py       # simulateur autonome -> dist/monocore.html
cd rtl && ./build_sim.sh          # banc Verilator du core -> rtl/obj_dir/Vmc_core
tools/regress.sh                  # régression simulateur / RTL (images, son, stress)
tools/regress_system.sh           # système complet avec modèle de SDRAM (rtl/mister)
python3 manual/tools/manual.py    # régénère le manuel PDF (Playwright/Chromium)
```

Quartus : ouvrir `mister/MONOCORE.qpf` (voir `mister/LISEZMOI.md`). `quartus_trial/` est l'essai de synthèse du core seul.

## SDK

- `sdk/monocore.h` : registres matériels. `sdk/easy.h` / `easy.c` : bibliothèque pour débutants (init, wait_frame, print, sprite, load_map, play…). `sdk/build.py` construit un jeu à partir d'un `project.txt` et déclare les noms qu'il crée dans `build/assets.h`, inclus d'office. Avec `easy`, `init()` charge toutes les planches (mises bout à bout, une entrée par dessin) et le premier décor, chacun avec sa palette de couleurs.
- `sdk/mc.h` / `mc.c` : SDK bas niveau (interruptions en C, raster, sprites par table fantôme + DMA, CHR RAM, PCM/PSG, virgule fixe), avec une petite libc (`sdk/libc.c`, en-têtes dans `sdk/libc/`). Choisi par `library = mc` dans `project.txt` ; `easy` reste la valeur par défaut. Référence : `sdk/SDK_MC.md`. La clé `data =` embarque des fichiers bruts ; `cflags =` ajoute des options de compilation aux seuls fichiers du programme (chemins d'inclusion, code produit par un traducteur).
- Le manuel (`manual/`) a un exemple par chapitre dans `manual/examples/`. Exemples `mc` : `examples/aube` (écran d'accueil actuel, `boot.rom`), `examples/boot` (l'ancien), `examples/nebula` (shoot'em up complet), `examples/horizon` (course en fausse 3D), `examples/test_inputs`, `examples/test_sdk`.

## Portages (jeux traduits, jamais émulés)

- Les fichiers tirés du jeu original (données, programme, code traduit, ROM construite) restent **hors du dépôt** (`.gitignore` de chaque portage) : ils sont produits par les outils à partir de sa propre copie. Le dépôt est public : ne jamais y pousser une ROM `.mnc` d'un portage. Une liste d'adresses (`entries.txt`) n'est pas une donnée du jeu et se commite.
- `examples/aigle` : L'Aigle d'Or (MO5, 6809), méthode de référence (BASIC traduit à la main, code machine par `recomp6809.py`, runtime MO5 `mo5rt.c`).
- `examples/pbobble2` : Puzzle Bobble 2 (Neo Geo, 68000). Chaîne : `prepare.py` (données) → `m68kmap.py` (carte du code) → `translate.py` (`build/tr/pb2_code.c`) → `sdk/build.py`. `neort.c` = runtime Neo Geo portable (mémoire, LSPC, palettes, BIOS remplacé en AES) ; `main.c` = couche MONOCORE (plan fixe → plan 0, décors → plans 1-3, autres colonnes → sprites via un cache de tuiles en CHR RAM, ombre de la VRAM en CHR RAM). `discover.py` (banc PC `pc/bench.c`, ou `--sim` avec `tools/run.js`) ajoute à `entries.txt` les adresses atteintes à l'exécution ; il récolte aussi les adresses de code rangées comme données dans la ROM. Son : `tools/sound/` (pilote Z80 réel + YM2610 ymfm, hors ligne), en cours.
- Traducteur 68000 : `tools/recomp68k/` (décodeur, générateur, `m68k_rt.h`) ; toute modification se valide par `python3 tools/recomp68k/test/fuzz.py 4000 1 3` (0 différence attendue contre Musashi).
- Piège : dans le bac à sable, ne pas tuer un processus par `pkill -f` avec un motif qui figure aussi dans la commande en cours (la commande se tue elle-même).
- Chargement MiSTer : `boot.rom` est envoyé par MiSTer **pendant que le reset du core est maintenu** (`status[0]`), qui n'est relâché qu'après. Le chargeur (`rtl/mister/mc_system.v`) ne doit donc rien effacer sur `reset` pendant ou juste après un téléchargement ; `rtl/tb/tb_system.cpp` reproduit ce cas avec `HOLD_RESET=1`.

## Ce qui reste à faire

- Core MiSTer : rotation d'écran (nécessite la DDRAM pour `screen_rotate`), sauvegarde NVRAM, DIP switches dans l'OSD.
- Bloc FM compatible YM2151.
- La ROM de stress déborde du blanking sur 2 images en mode 288 lignes.
- Traducteur statique vers RV32IM : 6809 et 68000 faits, Z80 à faire.
- Puzzle Bobble 2 : son (classer les 19 commandes du pilote, pré-rendre, jouer sur les voix PCM), essai sur la MiSTer.
- Jeu candidat pour tester la puissance : Final Fight (CPS1), avec musiques pré-enregistrées ou bloc FM ; le grand plan 32 × 32 du CPS1 dépasse la taille de carte d'un plan.
