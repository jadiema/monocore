# Core FPGA MONOCORE-1

État : **étapes 1 à 4 terminées** (processeur, mémoires, entrées/sorties, DMA, synchronisation d'image, moteur de rendu, moteur son, intégration MiSTer). Les images produites par le RTL sont identiques au pixel près à celles du simulateur JavaScript, sur tous les écrans vidéo de la ROM de test et sur une ROM de test aléatoire (`tools/vidfuzz`). Le moteur son produit les mêmes échantillons au bit près que le simulateur. Le core MiSTer (`../mister`) fonctionne sur la carte, image et son en HDMI.

## Fichiers

| Fichier | Rôle |
|---|---|
| `mc_core.v` | module de tête : CPU, bus instructions et données, DMA, instanciation des blocs |
| `mc_rom_cache.v` | cache en lecture seule devant la ROM (8 Ko instructions, 4 Ko données) |
| `mc_rom_arb.v` | partage du port ROM/SDRAM (rafales de 4 × 32 bits) |
| `mc_ram.v` | mémoires internes à octets adressables (inférence M10K en Verilog-2005) |
| `mc_io.v` | entrées, interruptions, timers, compteur de cycles, registres DMA, sortie de debug |
| `mc_gpu.v` | registres et mémoires vidéo ; le port A des mémoires sert au CPU, le port B au rendu |
| `mc_render.v` | séquencement du rendu d'une ligne, composition des plans, lectures graphiques (CHR RAM ou SDRAM), palette |
| `mc_layer.v` | un plan de tuiles : lecture de la tilemap, préchargement des graphismes, un pixel par cycle |
| `mc_sprites.v` | sélection des sprites d'une ligne, préparation (diviseurs du zoom), tracé avec lecture-modification-écriture du tampon de ligne |
| `mc_util.v` | FIFO et diviseur séquentiel |
| `mc_apu.v` | registres et moteur son : 8 voix PCM/ADPCM, 8 canaux PSG, mixage, cache d'une ligne par voix sur le port ROM |
| `mc_vtiming.v` | compteurs de ligne et d'image, interruptions VBLANK et LINE |
| `hazard3/` | processeur Hazard3 (Apache 2.0), non modifié |
| `tb/tb_main.cpp` | banc de test Verilator du core complet, avec un modèle de SDRAM ; écrit les captures et le son (`audio.wav`) |
| `tb/tb_apu.cpp` | banc de test du moteur son seul, qui rejoue un journal d'écritures de registres |

Tout tourne sur une seule horloge de 50 MHz (SPEC §2 : le cœur Hazard3 ne tient pas plus d'environ 53 MHz sur ce FPGA). Le port ROM de `mc_core` (`rom_req`, `rom_addr`, `rom_ack`, `rom_valid`, `rom_data`) est ce que le contrôleur SDRAM devra servir : une requête alignée sur 16 octets, puis 4 mots de 32 bits.

## Simuler

Il faut Verilator 5 (`pip install verilator` suffit) et un compilateur C++.

```sh
./build_sim.sh
obj_dir/Vmc_core ../rom/build/testrom.mnc /tmp/rtl "60:dump=menu"
node ../tools/run.js ../rom/build/testrom.mnc /tmp/js "60:dump=menu"
python3 ../tools/cmp_mem.py /tmp/js/menu.mem /tmp/rtl/menu.mem --ignore-hud
```

Le script d'entrées a la même syntaxe pour les deux : `image:p1=<hex>` règle les entrées du joueur 1 à partir de cette image, `image:dump=<nom>` enregistre les mémoires vidéo et son à la fin de cette image. Le banc de test tourne à environ 2,5 millions de cycles par seconde (une image toutes les 0,7 s).

## Le rendu

Chaque ligne visible est calculée au début de sa propre période (la « fenêtre de rendu »), puis affichée pendant la ligne suivante. Pendant la fenêtre, les écritures du CPU et du DMA dans le bloc vidéo attendent : la ligne est calculée sur l'état du début de ligne, exactement comme dans le simulateur, qui dessine une ligne avant de faire tourner le CPU.

Dans la fenêtre :

1. lecture des 4 entrées de la table de défilement par ligne, calcul des paramètres de chaque plan ;
2. les 4 plans avancent en parallèle (chacun a sa propre mémoire de tilemap et ses FIFO), un pixel composé par cycle dans le tampon de ligne ; en même temps, la table des sprites est parcourue (une entrée de 128 bits par cycle) ;
3. les sprites retenus sont tracés du dernier au premier, un pixel par cycle, avec la règle de priorité du simulateur ;
4. la palette convertit le tampon de ligne en RGB555.

Les graphismes viennent soit de la CHR RAM (mémoire interne, un mot par cycle), soit de la SDRAM par le port `gfx_*` (lectures de 32 bits, en ordre, plusieurs en vol). Le banc de test modélise ce port à une lecture par cycle avec 6 cycles de latence (SDRAM 16 bits à 100 MHz derrière un core à 50 MHz). Chaque plan et les sprites lisent dans une seule source par ligne (celle de leur adresse de base), ce qui garde les réponses dans l'ordre.

Durée mesurée de la fenêtre, pour 2 668 à 3 856 cycles par ligne (50 MHz) :

| Contenu | Cycles |
|---|---|
| menu de la ROM de test (2 plans, logo 8 bpp zoomé) | 837 |
| écran des sprites de la ROM de test, au budget de 64 sprites par ligne | 1 992 au plus |
| ROM aléatoire, 30 images | 1 967 au plus |
| pire cas construit : 4 plans 16 × 16 en 8 bpp et 256 sprites 8 bpp réduits, tout en SDRAM | 1 544 |
| ROM aléatoire haute résolution (`vidfuzz_hr.mnc`, 640 × 480i, 640 × 512i, 640 × 270…), 161 images | 2 593 au plus (lignes de 2 832 à 3 156 cycles) |
| pire cas haute résolution (`vfs_hr.mnc`), sprites étalés sur 640 pixels | 2 056 |

Le pire cas tient grâce au budget de 512 mots de graphismes par ligne pour les sprites (SPEC §4.7) : sans lui, des sprites 8 bpp très réduits liraient un mot par pixel et dépasseraient la durée d'une ligne. La marge restante (au moins 670 cycles sur la ligne la plus courte) est pour le partage de la SDRAM avec le CPU à l'étape 4 : le contrôleur devra donner la priorité aux lectures graphiques.

## Le son

À chaque échantillon (48 kHz, soit toutes les 1 041,67 cycles), le moteur parcourt les 8 voix puis les 8 canaux PSG, l'un après l'autre, et produit un échantillon stéréo en moins de 350 cycles en général (au plus 748 mesurés, avec les 8 voix en ADPCM au pas maximal : 16 quartets à décoder par voix et par échantillon). Le calcul est découpé en étapes courtes (multiplications et additions sur des cycles séparés). Chaque voix garde la dernière ligne de 16 octets lue en ROM ; un défaut de cache passe par le même arbitre que les caches du CPU (rafale de 4 mots). Tous les calculs suivent la section 5 de la spécification au bit près.

La validation se fait sur le moteur seul, parce que le CPU du simulateur et celui du core n'exécutent pas leurs écritures au même cycle :

```sh
./build_apu.sh
node ../tools/apu/apu_log.js session ../rom/build/testrom.mnc "<script>" 360 /tmp/s.log   # écritures d'une vraie session
node ../tools/apu/apu_log.js random ../rom/build/testrom.mnc 1 48000 /tmp/r.log           # écritures aléatoires
node ../tools/apu/apu_ref.js ../rom/build/testrom.mnc /tmp/r.log /tmp/js.raw
obj_apu/Vmc_apu ../rom/build/testrom.mnc /tmp/r.log /tmp/rtl.raw
python3 ../tools/apu/cmp_raw.py /tmp/js.raw /tmp/rtl.raw /tmp/r.log
python3 ../tools/apu/isolate.py ../rom/build/testrom.mnc /tmp/r.log 8000   # voix par voix
```

Résultats à 50 MHz : 6 journaux aléatoires d'une seconde (environ 19 000 écritures sur les 8 voix et les 8 canaux PSG) et 3 journaux de pire cas (8 voix ADPCM au pas maximal, 8 voix 8 ou 16 bits qui changent de ligne de cache à chaque échantillon) sont identiques au bit près ; `Vmc_apu` affiche aussi la durée de calcul la plus longue. Avant le passage à 50 MHz, la session de la ROM de test (287 948 échantillons) l'était aussi, y compris rejouée depuis les écritures du CPU matériel enregistrées par le banc de test complet (`apu.log` à côté de `audio.wav`).

Pendant le calcul d'un échantillon, les écritures dans le bloc son attendent (comme pour la vidéo pendant le rendu d'une ligne) : une écriture s'applique toujours entière à un échantillon. Les tests aléatoires ont trouvé deux erreurs que la session ne voyait pas : une extension de signe perdue (en Verilog, une concaténation n'est jamais signée) et un produit ADPCM tronqué à 14 bits pour les grands pas.

## Tests

```sh
tools/regress.sh /tmp/reg                    # tout : CPU, écrans de la ROM de test, ROM aléatoire, pire cas, son
tools/vidfuzz/build.sh                       # ROM aléatoire ; arguments : DIP par défaut (voir vidfuzz.c), fichier
tools/vidfuzz/build.sh 256 vfs.mnc           # variante pire cas pour la fenêtre de rendu
tools/vidfuzz/run_cmp.sh tools/vidfuzz/vidfuzz.mnc /tmp/fuzz 61
node tools/vidfuzz/triage.js tools/vidfuzz/vidfuzz.mnc 5 /tmp/fuzz/rtl/v5.ppm   # quel plan diverge
```

Le test aléatoire a trouvé une erreur que la ROM de test ne voyait pas (mauvais mot lu pour les tuiles retournées horizontalement) ; ses DIP switches permettent d'isoler une partie du rendu (sans sprites, sans plans, sans zoom…).

## Mesures

Coûts mesurés sur le RTL à 50 MHz (le simulateur en est recalé, SPEC §2 et §6) : 0,94 instruction par cycle sur un CRC32 (environ 47 millions par seconde) ; DMA : remplissage 1 cycle par mot, copie entre mémoires internes 3 cycles par mot, copie depuis la ROM 5,5 cycles par mot (45 095 cycles pour 32 Ko, 45 068 dans le simulateur).

La charge CPU affichée en haut à droite par la ROM de test est une estimation qui dépend du modèle de temps : elle peut différer d'un point entre le simulateur et le core (c'est la seule différence de l'écran `planes`).

## Notes pour Quartus

- Le code est en Verilog-2005 (pas de SystemVerilog) ; les mémoires portent l'attribut `ramstyle = "M10K, no_rw_check"`. Sans lui, le premier essai Quartus (14 octobre 2026) dupliquait chaque mémoire à double accès (une copie par port de lecture) pour garantir la lecture d'une case écrite au même cycle sur l'autre port, cas que le design n'autorise jamais, et convertissait la table des sprites et les étiquettes des caches en bascules : 175 000 cellules logiques et plus de 553 blocs M10K, le design ne rentrait pas. Le projet d'essai désactive aussi `AUTO_RAM_TO_LCELL_CONVERSION`.
- Les FIFO du rendu (8 à 16 mots) sont volontairement en logique (`ramstyle = "logic"`).
- Hazard3 est configuré dans l'instanciation de `mc_core.v` : RV32IM, compteurs, multiplieur rapide, diviseur à 2 bits par cycle, prédicteur de branchement.
- La table des durées de ligne de `mc_vtiming.v` est générée (une entrée par hauteur de 192 à 288).
- Estimation des blocs M10K du core seul : environ 410 sur 553 (WRAM 128, CHR RAM 128, tilemaps 64, NVRAM 32, le reste moins de 60).
- Fréquence : le deuxième essai Quartus (16 octobre 2026) plafonnait à 45 MHz (modèle lent), limité par la chaîne PSG du moteur son (multiplication de fréquence, phase, forme d'onde, volume et accumulation dans le même cycle, 22 ns). Ont été découpés depuis : le moteur son (une opération longue par cycle), le calcul des adresses graphiques des plans et des sprites (champs disjoints réunis par OU, une seule addition), l'évaluation des sprites (multiplications, test de ligne et contrôle du budget sur trois cycles, budget compté en reste), la composition des 4 plans (trois étages), la base de temps (fin de ligne et ligne suivante préparées en registres, taille d'écran recopiée en registre) et les sélections d'esclave du DMA. Le résultat reste identique au simulateur au pixel et à l'échantillon près.
- `tools/sta/` estime les temps de propagation à partir d'une synthèse Yosys (`synth.sh` puis `sta.py`), avec un modèle de délais calé sur le chemin PSG mesuré par Quartus. Il sert à classer les chemins, Quartus a le dernier mot. Le script d'essai `trial_report.tcl` écrit aussi le pire chemin de chaque bloc (`trial_blocks.txt`).
- Le troisième essai Quartus (17 octobre 2026, rapport bloc par bloc) a montré que Hazard3 plafonne à environ 53 MHz : son chemin critique va du tampon d'instructions au décodage, au test d'instruction invalide, à la décision d'attente sur dépendance et à la requête de bus, 18 niveaux de logique dans le même cycle. La console passe donc à une horloge unique de 50 MHz (SPEC v0.3), avec des budgets par ligne divisés par deux (64 sprites, 1280 pixels, 512 mots) et 8 voix PCM. Les blocs vidéo et son, eux, étaient déjà entre 61 et 93 MHz et ont encore été raccourcis (file des requêtes graphiques, liste des sprites retenus, sortie de la CHR RAM, bornage de la taille d'écran).
- Le quatrième essai (50 MHz) passait avec 0,09 ns de marge : le pire chemin allait du signal de transfert du CPU (`d_htrans`, sa sortie la plus tardive) aux validations de lecture des mémoires vidéo et de la WRAM. Ces mémoires internes (vidéo, WRAM, NVRAM) lisent désormais l'adresse présentée dès que le bus peut prendre une phase d'adresse, transfert ou non : leurs lectures n'ont pas d'effet de bord et se font en un cycle, donc rien ne change pour le logiciel (mêmes résultats et mêmes nombres de cycles), mais leur validation ne dépend plus de `d_htrans`.

## Intégration MiSTer (étape 4)

`mister/` contient `mc_system.v` (le core avec son contrôleur SDRAM, le chargement des ROM et le balayage vidéo, tout ce qui est sous le framework), `mc_sdram.v` et `mc_scanout.v` ; `../mister/monocore.sv` les relie au framework.

- SDRAM à 100 MHz (horloge alignée sur celle de la console à 50 MHz, même PLL) : une ligne ouverte par banque, commandes en pipeline, rafales de 2 (un mot de 32 bits), une ligne de ROM en 4 lectures consécutives ; priorité au rendu, avec un tour garanti pour les lignes de ROM après 8 mots graphiques. Lecture des données 3 cycles après la commande (CAS 2 et horloge SDRAM inversée, comme les cores MiSTer à 96 MHz) ; l'option OSD « Lecture SDRAM : Retardée » ajoute un cycle si la carte l'exige.
- Balayage : deux tampons de ligne ; la ligne y, calculée pendant la période y, est affichée pendant la période y + 1, un pixel tous les 4 cycles (12,5 MHz), synchros positives, image décalée de 112 pixels après le début de ligne.
- Chargement : chaque mot de 16 bits reçu du menu (hps_io, mode large) est écrit en SDRAM ; l'en-tête donne la taille de la ROM et les DIP switches par défaut ; la console démarre à la fin du chargement.

`build_system.sh` construit `obj_sys/Vmc_system`, le système complet avec un modèle de puce SDRAM (`tb/tb_system.cpp` : CAS 2, vérification de tRCD, tRP, tRAS, tRC, tRRD, du rafraîchissement et des masques d'octets) ; la ROM y est chargée par le port de téléchargement et les images sont prises sur la sortie vidéo. `tools/regress_system.sh` compare au simulateur : les 10 écrans de la ROM de test, 20 images de la ROM aléatoire et 20 du pire cas sont identiques (hors l'indicateur de charge CPU et deux premières lignes du pire cas, mêmes écarts que sur le core seul), sans aucune erreur de timing SDRAM ; fenêtre de rendu la plus longue avec la vraie SDRAM : 2 098 cycles sur 2 668.

