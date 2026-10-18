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

## 6. Règles pour rester compatible avec le FPGA

1. **Se synchroniser sur les interruptions** (VBLANK, LINE, timers), jamais sur un nombre de cycles. Le simulateur estime le temps CPU ; le vrai processeur, avec ses caches, ne fera pas exactement les mêmes comptes.
2. **Garder de la marge CPU.** Le moniteur du simulateur affiche la charge par image : au-delà de 70 %, prévois de l'optimisation.
3. **Respecter les limites matérielles** : 128 Ko de WRAM (variables et pile), 64 sprites, 1280 pixels et 512 mots de graphismes de sprites par ligne, 8 voix PCM, 4096 couleurs, 4 plans. Le moniteur signale les débordements des sprites.
4. **N'utiliser que les registres documentés.** `DEBUG_OUT` n'existe que dans le simulateur : sur le FPGA, les écritures y sont ignorées.
5. **Ne jamais déréférencer l'adresse 0 en C.** C'est le début de la ROM, mais le compilateur la traite comme un pointeur nul et peut supprimer le code qui la lit. Passe par un symbole (`assets`, `hero_chr`…).
6. **Compiler en RV32IM** (`-march=rv32im -mabi=ilp32`), sans extension C (instructions compressées) ni F (flottants). Pour des calculs fractionnaires, utilise la virgule fixe.

## 7. Sur le MiSTer

Quand le core existera : copie le fichier `.mnc` dans `games/MONOCORE` sur la carte SD du MiSTer, lance le core MONOCORE-1, puis choisis la ROM dans le menu OSD. Le framework MiSTer charge la ROM en SDRAM ; le bitstream ne change pas.
