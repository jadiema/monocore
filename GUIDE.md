# Créer sa propre ROM MONOCORE-1

Une ROM MONOCORE-1 est un programme RISC-V 32 bits (RV32IM) suivi de ses données. Elle ne parle au matériel qu'à travers les registres décrits dans la spécification, ce qui lui permet de tourner telle quelle dans le simulateur et sur le core FPGA, sans reprogrammer le FPGA.

## 1. Installer les outils

Il faut trois choses, toutes gratuites :

- **LLVM** (clang, ld.lld, llvm-objcopy) : il sait produire du RISC-V sans toolchain spécifique.
  - Linux : `sudo apt install clang lld llvm` (ou l'équivalent de ta distribution).
  - macOS : `brew install llvm lld`, puis ajouter `$(brew --prefix llvm)/bin` au `PATH`.
  - Windows : installer LLVM depuis le site du projet, ou travailler dans WSL avec la commande Linux.
- **Python 3** avec **Pillow** (`pip install pillow`) pour convertir les images.
- **make**.

Vérification : `clang -print-targets | grep riscv32` doit afficher une ligne.

## 2. Partir de l'exemple

Copie le dossier `examples/hello` sous un autre nom, puis lance `make` dedans. Le fichier `build/hello.mnc` est ta ROM. Glisse-la sur la page du simulateur (ou clique sur « Charger une ROM »).

Le dossier contient :

| Fichier | Rôle |
|---|---|
| `hello.c` | le programme |
| `data.S` | inclut les graphismes convertis dans la ROM (`.incbin`) |
| `robot.png` | un sprite de 16 × 16 pixels |
| `Makefile` | nom, titre, sources et règles de conversion |

Le kit (`sdk/`) fournit :

| Fichier | Rôle |
|---|---|
| `monocore.h` | tous les registres, sous forme de macros et de structures C |
| `crt0.S` | l'en-tête de ROM, l'initialisation de la RAM, l'entrée des interruptions |
| `link.ld` | code et constantes en ROM, variables et pile en WRAM |
| `mini.c` | `memset`, `memcpy`… dont le compilateur peut avoir besoin |
| `mkrom.py` | écrit titre, taille, rotation et DIP par défaut dans l'en-tête |
| `font.png` | une police 8 × 8 prête à convertir (ASCII 32 à 127) |
| `tools/png2chr.py` | PNG → graphismes (`.chr`) et palette (`.pal`) |
| `tools/wav2pcm.py` | WAV → échantillon 8 bits, 16 bits ou ADPCM OKI |

## 3. Structure d'un programme

Un jeu tient dans une boucle synchronisée sur le retour vertical :

```c
#include "monocore.h"

static volatile uint32_t vblanks;

void irq_handler(void) {                 // appelé par crt0.S à chaque interruption
  if (IRQ_STATUS & IRQ_VBLANK) { IRQ_STATUS = IRQ_VBLANK; vblanks++; }
}

int main(void) {
  // ... palettes, plans, sprites ...
  DISP_CTRL = DISP_ON | DISP_LAYER(0) | DISP_SPRITES;
  IRQ_ENABLE = IRQ_VBLANK;
  irq_global_enable();
  for (;;) {
    uint32_t v = vblanks;
    while (vblanks == v) wfi();          // dort jusqu'à l'image suivante
    // lire IN_P1, déplacer les objets, écrire les sprites, jouer les sons
  }
}
```

`irq_handler` est obligatoire : `crt0.S` l'appelle pour chaque interruption, après avoir sauvegardé les registres.

## 4. Graphismes

Convertis tes images avec `png2chr.py` :

```sh
# sprites : cellules 8x8 dans l'ordre des lignes, 16 couleurs (0 = transparent)
python3 sdk/tools/png2chr.py hero.png build/hero
# plan de tuiles 16x16 avec carte et dédoublonnage des tuiles
python3 sdk/tools/png2chr.py niveau1.png build/niveau1 --tile 16 --map
# 256 couleurs
python3 sdk/tools/png2chr.py titre.png build/titre --bpp 8
```

Puis inclus les fichiers produits dans `data.S` et déclare-les en C :

```c
extern const uint8_t hero_chr[], hero_pal[];
dma_copy(PALETTE + 3 * 16, hero_pal, 32);   // palette 3
SPR_CHR = (uint32_t)hero_chr;
```

Un sprite de `w × h` cellules utilise les cellules `tile + cy × w + cx`. Une image de 32 × 32 pixels convertie en sprite donne donc 4 × 4 cellules, à afficher avec `w = 4, h = 4`.

Pour un plan, la carte (`.map`) contient un numéro de tuile par case ; chaque entrée de tilemap vaut `TILE(numéro, palette)`, avec éventuellement `T_HFLIP`, `T_VFLIP` ou `T_PRIO`.

## 5. Son

```sh
python3 sdk/tools/wav2pcm.py explosion.wav build/explosion.raw adpcm
# -> affiche le nombre d'échantillons et la valeur STEP correspondant à la fréquence du WAV
```

```c
volatile pcm_voice_t *v = &PCM[3];
v->ctrl = 0;
v->start = (uint32_t)explosion_raw; v->length = 15872; v->loop = PCM_NOLOOP;
v->step = PCM_STEP(16000); v->format = PCM_ADPCM; v->vol_l = v->vol_r = 200;
v->ctrl = 1;                            // lance la voix
```

Pour une musique, le plus simple est de cadencer un séquenceur avec le timer 0 (par exemple à 240 Hz) et de piloter les canaux PSG et PCM depuis `irq_handler`, comme le fait la ROM de test.

Plus simple encore : composer avec **MONOTRACKER**, le tracker de la console. Choisissez-le dans la liste des ROM intégrées du simulateur : le « clavier de la console » s'active tout seul et <kbd>F4</kbd> affiche l'aide. Les morceaux sauvés (<kbd>Ctrl</kbd>+<kbd>S</kbd>) restent dans la NVRAM du navigateur ; le bouton **Morceaux .mts** sous l'écran les télécharge, prêts pour un jeu écrit avec le SDK `mc` (`modules = mtrack`, voir `sdk/MTRACK.md`). **Exporter .sav** et **Importer .sav…** font passer la NVRAM du simulateur à la MiSTer (`saves/MONOCORE`) et inversement.

## 6. Règles pour rester compatible avec le FPGA

1. **Se synchroniser sur les interruptions** (VBLANK, LINE, timers), jamais sur un nombre de cycles. Le simulateur estime le temps CPU ; le vrai processeur, avec ses caches, ne fera pas exactement les mêmes comptes.
2. **Garder de la marge CPU.** Le moniteur du simulateur affiche la charge par image : au-delà de 70 %, prévois de l'optimisation.
3. **Respecter les limites matérielles** : 128 Ko de WRAM (variables et pile), 64 sprites, 1280 pixels et 512 mots de graphismes de sprites par ligne, 8 voix PCM, 4096 couleurs, 4 plans. Le moniteur signale les débordements des sprites.
4. **N'utiliser que les registres documentés.** `DEBUG_OUT` n'existe que dans le simulateur : sur le FPGA, les écritures y sont ignorées.
5. **Ne jamais déréférencer l'adresse 0 en C.** C'est le début de la ROM, mais le compilateur la traite comme un pointeur nul et peut supprimer le code qui la lit. Passe par un symbole (`assets`, `hero_chr`…).
6. **Compiler en RV32IM** (`-march=rv32im -mabi=ilp32`), sans extension C (instructions compressées) ni F (flottants). Pour des calculs fractionnaires, utilise la virgule fixe.

## 7. Aller plus loin : le SDK bas niveau « mc »

Ce guide accède au matériel par ses registres, et le manuel du programmeur utilise la bibliothèque `easy`, volontairement simple (un plan de texte, une carte, 64 sprites de 16 × 16, 32 × 32 ou 64 × 64). Entre les deux, le SDK **mc** donne accès à toute la console en C, sans assembleur, pour les jeux complexes et les portages. Il suffit d'ajouter une ligne dans `project.txt` :

```
library = mc
```

puis d'inclure `mc.h`. On obtient alors :

- des **interruptions en C** : `mc_on_vblank`, deux timers (`mc_on_timer`), et jusqu'à 64 **effets raster** par image (`mc_raster_add`), exacts à la ligne près ;
- toute la **vidéo** : résolution de 224 à 384 × 192 à 288 (`mc_video_mode`), quatre plans en tuiles 8 ou 16 et 4 ou 8 bpp (`mc_layer`), défilement ligne par ligne, **CHR RAM** de 128 Ko pour les graphismes produits à l'exécution (`mc_chr_upload`) ;
- **512 sprites** gérés par une table fantôme en WRAM, envoyée par DMA à chaque `mc_frame()` ;
- le **son** PCM et PSG en accès direct (`mc_pcm_play`, `mc_psg`), et un petit séquenceur (`mc_tune`) ;
- une **petite libc** : `malloc`/`free`, `<string.h>`, `<stdlib.h>`, `<stdio.h>` (`printf` écrit sur la sortie de débogage), `<ctype.h>`, `<assert.h>` ;
- des **maths en virgule fixe** 16.16 : `mc_sin`, `mc_cos`, `mc_atan2`, `mc_isqrt` ;
- la clé `data = fichier.bin` dans `project.txt`, pour embarquer des fichiers bruts (niveaux, données d'un jeu porté).
- les clés `rotation = 0` à `3` (quarts de tour de l'image, pour les jeux verticaux) et `dip = 0x…` (les DIP switches par défaut, que l'OSD de la MiSTer et le simulateur proposent) dans `project.txt`, écrites dans l'en-tête de la ROM.

Squelette :

```c
#include "mc.h"

int main(void) {
  mc_init();
  mc_text_layer(0, 3);                  // texte sur le plan 0, devant tout
  mc_text(0, 2, 2, "BONJOUR", 0);
  for (;;) {
    mc_frame();                         // image suivante : sprites, manettes, musique
    if (mc_pad_pressed(1) & IN_START) { /* ... */ }
  }
}
```

La référence complète est dans `sdk/SDK_MC.md`. Trois exemples l'utilisent :

| Exemple | Ce qu'il montre |
|---|---|
| `examples/test_sdk` | le minimum à vérifier : raster, timers, `malloc`, `printf` |
| `examples/boot` | l'écran d'accueil de la console : barres copper, logo, ciel raster, texte défilant, musique |
| `examples/horizon` | une course en fausse 3D : la route est choisie et décalée ligne par ligne, les objets sont zoomés selon leur distance |
| `examples/nebula` | un shoot'em up complet : sprites et décors générés au démarrage, parallaxe sur trois plans, sol en perspective, 54 effets raster, jusqu'à 233 sprites, tirs visés, particules zoomées |

## 8. Sur le MiSTer

Copie le core dans `_Console` et tes fichiers `.mnc` dans `games/MONOCORE` sur la carte SD du MiSTer, lance le core MONOCORE-1, puis choisis la ROM dans le menu OSD (F12 → Charger une ROM). Le framework MiSTer charge la ROM en SDRAM ; le bitstream ne change pas.

Un fichier nommé exactement **`games/MONOCORE/boot.rom`** est chargé automatiquement au lancement du core (c'est ainsi que démarre l'écran d'accueil, `examples/boot`). Pour que la console démarre directement sur le core, ajoute `bootcore=lastexactcore` dans `MiSTer.ini`, en mettant `bootcore_timeout` en commentaire.
