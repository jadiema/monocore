# SDK bas niveau « mc » de la MONOCORE-1

La bibliothèque **easy** du manuel fixe une configuration simple (un plan de texte, une carte,
64 sprites de 16 × 16). Le SDK **mc** donne accès à toute la console, en C uniquement, pour
écrire ou porter des jeux complexes : aucune ligne d'assembleur n'est nécessaire, le démarrage
et l'entrée des interruptions restant assurés par `crt0.S`.

## Utilisation

Dans `project.txt`, ajoutez :

    library = mc

puis `#include "mc.h"`. La compilation se fait comme d'habitude (`python3 …/sdk/build.py .`).
Les en-têtes `<string.h>`, `<stdlib.h>`, `<stdio.h>`, `<ctype.h>` et `<assert.h>` deviennent
disponibles, ce qui permet de compiler la plupart du code C portable sans modification.

La clé `cflags = …` ajoute des options de compilation aux fichiers C du programme (chemins
d'inclusion, avertissements), par exemple pour du code produit par un traducteur.

La nouvelle clé `data = fichier.bin …` embarque des fichiers bruts dans la ROM (niveaux,
musiques, données d'un jeu porté) : symboles `fichier_file` (données) et `fichier_size` (taille).

## Squelette

    #include "mc.h"

    int main(void) {
      mc_init();
      mc_text_layer(0, 3);                       // texte de débogage sur le plan 0
      mc_text(0, 2, 2, "BONJOUR", 0);
      for (;;) {
        mc_frame();                              // attend l'image suivante, envoie les sprites
        if (mc_pad_pressed(1) & IN_START) { … }
      }
    }

## Ce que couvre le SDK

| Domaine | Fonctions | Remarques |
|---|---|---|
| Système | `mc_init`, `mc_frame`, `mc_frames`, `mc_cycles`, `mc_micros` | `mc_frame` attend la pause verticale, copie la table de sprites par DMA, lit les manettes et fait avancer la musique |
| Interruptions | `mc_on_vblank`, `mc_on_timer`, `mc_irq_off/on` | gestionnaires en C ; timers 0 et 1 à la fréquence voulue |
| Effets raster | `mc_raster_add`, `mc_raster_clear`, `mc_raster_line` | jusqu'à 64 effets par image ; l'écriture faite dans l'effet s'applique **exactement** à partir de la ligne demandée (vérifié en simulation) |
| Vidéo | `mc_video_mode`, `mc_display`, `mc_layer`, `mc_layer_scroll`, `mc_layer_linescroll`, `mc_tile`, `mc_tilemap_fill/load` | résolution de 224 à 384 × 192 à 288 ; 4 plans en tuiles 8 ou 16, 4 ou 8 bpp |
| Couleurs | `mc_color`, `mc_palette`, `mc_background`, `MC_RGB` | 4096 couleurs RGB555 = 256 palettes de 16 |
| Graphismes dynamiques | `mc_chr_upload`, `MC_CHR`, `MC_CHRRAM` | 128 Ko de CHR RAM utilisables par les plans et les sprites, à la place de la ROM |
| Sprites | `mc_sprite`, `mc_sprite_zoom`, `mc_sprite_flip`, `mc_sprite_8bpp`, `mc_sprite_hide`, `mc_sprites_used` | 512 sprites de 8 × 8 à 256 × 256, table fantôme `mc_spr[]` en WRAM |
| Texte | `mc_text_layer`, `mc_text`, `mc_text_clear` | police du SDK, pour les menus et le débogage |
| Son | `mc_pcm_play`, `mc_pcm_stop`, `mc_pcm_pos`, `mc_psg`, `mc_psg_freq16`, `mc_psg_volume`, `mc_master_volume`, `mc_tune` | 8 voies PCM (8/16 bits, ADPCM) et 8 voies PSG |
| Entrées | `mc_pad`, `mc_pad_pressed`, `mc_pad_released`, `mc_sys`, `mc_dip`, `mc_hw_version` | 4 manettes (joueurs 1 à 4) |
| Analogique | `mc_stick_x`, `mc_stick_y`, `mc_paddle`, `mc_spinner`, `mc_spinner_delta` | sticks gauche et droit de −127 à +127, paddle 0-255, spinner en position 16 bits |
| Clavier | `mc_key`, `mc_key_pressed`, `mc_key_released`, `mc_key_event` | codes PS/2 `KEY_xxx` de `monocore.h`, état des 256 touches et file de 16 événements |
| Mémoire | `malloc`, `calloc`, `realloc`, `free`, `mc_heap_free`, `mc_copy`, `mc_fill`, `MC_NVRAM` | tas d'environ 100 Ko entre les variables et la pile (16 Ko) |
| Maths | `fix16`, `fix_mul`, `fix_div`, `mc_sin`, `mc_cos`, `mc_atan2`, `mc_isqrt`, `mc_rand` | virgule fixe 16.16, angles sur 1024 pas |
| Débogage | `mc_debug`, `printf`, `puts`, `assert` | sortie sur `DEBUG_OUT`, affichée par le simulateur |

## Règles matérielles à connaître pour un portage

- **Mémoire de travail** : la WRAM fait 128 Ko pour les variables, le tas et la pile. Les données
  constantes restent en ROM (`const`), lues à travers les caches.
- **Écritures vidéo** : chaque ligne est calculée au début de sa période, avec l'état des registres
  à cet instant ; les écritures du processeur sont retardées pendant ce calcul. C'est ce qui rend
  les effets raster exacts.
- **Sprites** : au plus 64 sprites et 1 280 pixels par ligne. Un sprite de priorité *p* (0-7)
  passe devant les pixels de plan dont la priorité effective est ≤ *p* (priorité effective =
  priorité du plan × 2 + bit `T_PRIO` de la tuile). Entre sprites, le plus petit numéro est devant.
- **Cartes** : 4096 entrées par plan (64 × 64, 128 × 32…), une entrée = numéro de tuile, palette
  et retournements (`MC_TILE`).
- **DMA** : le processeur attend la fin du transfert ; à préférer pour toute copie de plus de
  quelques dizaines d'octets (ROM → CHR RAM, palettes, cartes).
- **Virgule flottante** : le processeur (RV32IM) n'a pas d'unité flottante. Le compilateur émule
  `float`, mais lentement ; pour un portage, convertissez les calculs en `fix16`.
- **NVRAM** : 32 Ko, pas encore sauvegardés sur la carte SD par le core.

## Manettes 3 et 4, analogique et clavier

Ces entrées existent à partir de la version 2 du matériel (`mc_hw_version() >= 2`) ; sur une
version plus ancienne, les fonctions renvoient 0 (ou -1 pour `mc_key_event`). Elles viennent du
framework MiSTer : manettes 3 et 4, sticks analogiques, paddles et spinners des manettes 1 à 4,
clavier USB de la carte. Dans le simulateur, les sticks des manettes du navigateur sont lus, et la
case « clavier de la console » (touche F9) envoie le clavier du PC à la console au lieu des
joueurs 1 et 2.

Les touches sont désignées par leur code PS/2 (jeu 2), `KEY_xxx` dans `monocore.h`, avec 0x100 en
plus pour les touches étendues (flèches, Inser…). Ce code désigne une **position** : il porte le nom
de la touche d'un clavier QWERTY, si bien que sur un clavier AZERTY la touche A est `KEY_Q`.
`mc_key` et `mc_key_pressed` lisent l'état relevé par `mc_frame()` ; `mc_key_event()` rend les
événements un par un, dans l'ordre, ce qui convient à la saisie de texte (`examples/test_inputs`
affiche tout ce que la console reçoit).

## Exemples

| Dossier | Contenu |
|---|---|
| `examples/test_sdk` | test minimal : raster, timers, `malloc`, `printf` |
| `examples/test_inputs` | affiche les 4 manettes, leurs sticks, paddles et spinners, et les événements du clavier |
| `examples/boot` | l'écran d'accueil de la console (`boot.rom`) : barres copper par défilement ligne par ligne, logo en un seul sprite de 256 × 40, ciel raster, texte défilant en sprites zoomés |
| `examples/horizon` | course en fausse 3D (style OutRun) : une texture de route de 120 lignes, choisie (`dy`) et décalée (`dx`) ligne par ligne pour la largeur, les bandes et les virages ; objets zoomés selon leur distance et triés avec `qsort` |
| `examples/nebula` | shoot'em up complet ; tous les graphismes sont produits au démarrage dans la CHR RAM (sprites en « ASCII art », décors procéduraux découpés en tuiles avec partage des tuiles identiques), parallaxe, sol en perspective, 233 sprites, tirs visés avec `mc_atan2` |

## Fichiers

- `mc.h`, `mc.c` : le SDK ;
- `libc.c` et `libc/` : la petite bibliothèque C standard ;
- `build.py` : clés `library = mc` et `data = …` ajoutées ; les projets existants (easy) ne changent pas.
