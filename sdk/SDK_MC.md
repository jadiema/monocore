# SDK bas niveau « mc » de la MONOCORE-1

La bibliothèque **easy** du manuel fixe une configuration simple (un plan de texte, une carte,
64 sprites de 16 × 16, 32 × 32 ou 64 × 64). Le SDK **mc** donne accès à toute la console, en C uniquement, pour
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

Les noms que `build.py` crée pour les fichiers de `project.txt` sont déclarés tout seuls
(`build/assets.h`, inclus d'office dans chaque fichier C) : `x_chr`, `x_pal` pour `sprites`,
`x_chr`, `x_pal`, `x_map`, `x_width`, `x_height` pour `maps` et `maps8`, `x_data`, `x_count`,
`x_rate` pour `sounds`, `x_file`, `x_size` pour `data` (`x` : le nom du fichier sans extension).
Les tableaux sont des `const uint8_t[]` (`const int8_t[]` pour les sons), les nombres des
`const uint32_t`. Avec `mc`, les planches de sprites peuvent avoir n'importe quelle largeur.

La nouvelle clé `data = fichier.bin …` embarque des fichiers bruts dans la ROM (niveaux,
musiques, données d'un jeu porté) : symboles `fichier_file` (données) et `fichier_size` (taille).

La clé `modules = …` ajoute des modules facultatifs du SDK. Le premier est `mtrack`, le lecteur
des morceaux du tracker MONOTRACKER (`#include "mtrack.h"`, documentation : `MTRACK.md`).

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
| Musique (module `mtrack`) | `mt_init`, `mt_play`, `mt_stop`, `mt_sfx`, `mt_reserve`, `mt_level`… | morceaux de MONOTRACKER : enveloppes, vibrato, arpèges et effets sur les 16 canaux (`MTRACK.md`) |
| Entrées | `mc_pad`, `mc_pad_pressed`, `mc_pad_released`, `mc_sys`, `mc_dip`, `mc_hw_version` | 4 manettes (joueurs 1 à 4) |
| Analogique | `mc_stick_x`, `mc_stick_y`, `mc_paddle`, `mc_spinner`, `mc_spinner_delta` | sticks gauche et droit de −127 à +127, paddle 0-255, spinner en position 16 bits |
| Clavier | `mc_key`, `mc_key_pressed`, `mc_key_released`, `mc_key_event` | codes PS/2 `KEY_xxx` de `monocore.h`, état des 256 touches et file de 16 événements |
| MIDI | `mc_midi_event`, `MIDI_STATUS`, `MIDI_DATA1`, `MIDI_DATA2` | messages d'un clavier MIDI USB (matériel version 4) |
| Souris | `mc_mouse_present`, `mc_mouse_x`, `mc_mouse_y`, `mc_mouse_buttons`, `mc_mouse_pressed`, `mc_mouse_wheel`, `mc_mouse_dx`, `mc_mouse_speed`, `mc_mouse_set` | pointeur gardé dans l'écran par `mc_frame()`, boutons `MOUSE_LEFT/RIGHT/MIDDLE`, molette (matériel version 3) |
| Mémoire | `malloc`, `calloc`, `realloc`, `free`, `mc_heap_free`, `mc_copy`, `mc_fill`, `MC_NVRAM` | tas d'environ 100 Ko entre les variables et la pile (16 Ko) |
| Maths | `fix16`, `fix_mul`, `fix_div`, `mc_sin`, `mc_cos`, `mc_atan2`, `mc_isqrt`, `mc_rand` | virgule fixe 16.16, angles sur 1024 pas |
| Débogage | `mc_debug`, `printf`, `puts`, `assert` | sortie sur `DEBUG_OUT`, affichée par le simulateur ; `printf` accepte `%d %u %x %s %c %p`, la largeur, `0`, `-`, `+` et `%.Ns` |

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
- **NVRAM** : 32 Ko, sauvegardés par le core dans `saves/MONOCORE/<rom>.sav` (à l'ouverture de l'OSD).

## Souris

La souris existe à partir de la version 3 du matériel (`mc_hw_version() >= 3`). Elle vient du
framework MiSTer (souris USB de la carte, `ps2_mouse` de `hps_io`) ; le core en tient deux
registres, des compteurs de position et l'état des boutons et de la molette (SPEC, `MOUSE_XY` et
`MOUSE_BTN`). `mc_frame()` en tire un pointeur gardé dans l'écran : `mc_mouse_x()` et
`mc_mouse_y()` en pixels, à raison d'un pixel par pas de la souris au départ (`mc_mouse_speed(n, d)`
pour n/d pixels par pas, `mc_mouse_set` pour le placer). `mc_mouse_pressed()` donne les boutons
enfoncés depuis l'image précédente, `mc_mouse_wheel()` les crans de molette (positifs vers l'avant).
`mc_mouse_present()` reste faux tant que la souris n'a rien envoyé. Le pointeur n'est pas dessiné :
le jeu affiche le sien, par exemple avec un sprite (`examples/tracker`).

Dans le simulateur, la case « souris de la console » fait suivre au pointeur du jeu celui du
navigateur sur l'écran (un pas = un pixel de la console) ; `tools/run.js` accepte
`ms=dx,dy,boutons,molette`.

## Clavier MIDI

L'entrée MIDI existe à partir de la version 4 du matériel (`mc_hw_version() >= 4`). Sur MiSTer, un
clavier MIDI USB branché sur la carte passe par MidiLink : dans le menu du core, réglez le mode UART
sur MIDI (connexion USB). Dans le simulateur, la case « clavier MIDI » envoie à la console les
messages d'un clavier branché sur le PC (Chrome, Edge ; Firefox si la page est ouverte par
`dist/simulateur.bat` ou `dist/simulateur.py`, qui la servent en local) ; `tools/run.js` accepte `mi=90,3C,64` (octets
en hexadécimal).

`mc_midi_event()` rend le message complet suivant, ou −1 : `MIDI_STATUS(e)` (`MIDI_NOTE_ON`,
`MIDI_NOTE_OFF`, `MIDI_CC`, `MIDI_PITCH`…), `MIDI_CHANNEL(e)`, `MIDI_DATA1(e)` (note ou numéro de
contrôleur) et `MIDI_DATA2(e)` (vélocité ou valeur). Le statut implicite (running status) est géré ;
une note jouée avec une vélocité nulle arrive comme une note relâchée ; l'horloge, le sysex et les
autres messages système sont écartés. Le do du milieu est la note 60.

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
| `examples/test_inputs` | affiche les 4 manettes, leurs sticks, paddles et spinners, les événements du clavier et la souris |
| `examples/boot` | l'écran d'accueil de la console (`boot.rom`) : barres copper par défilement ligne par ligne, logo en un seul sprite de 256 × 40, ciel raster, texte défilant en sprites zoomés |
| `examples/horizon` | course en fausse 3D (style OutRun) : une texture de route de 120 lignes, choisie (`dy`) et décalée (`dx`) ligne par ligne pour la largeur, les bandes et les virages ; objets zoomés selon leur distance et triés avec `qsort` |
| `examples/nebula` | shoot'em up complet ; tous les graphismes sont produits au démarrage dans la CHR RAM (sprites en « ASCII art », décors procéduraux découpés en tuiles avec partage des tuiles identiques), parallaxe, sol en perspective, 233 sprites, tirs visés avec `mc_atan2` |
| `examples/tracker` | MONOTRACKER, tracker de musique au clavier et à la souris, interface à panneaux façon trackers Amiga : 16 pistes, page d'édition des sons, sauvegarde en NVRAM, motifs rangés dans la CHR RAM |
| `examples/cascade` | CASCADE, jeu de rythme au clavier MIDI : un morceau de MONOTRACKER parcouru, ses notes défilant dans une couche de tuiles en anneau, synchronisées sur `mtrack` |
| `examples/defender` | PIANO DEFENDER, shoot'em up au clavier MIDI : vélocité, accords, molette de modulation, tirs joués avec un instrument défini dans le programme (`mt_note`) |
| `examples/test_mtrack` | le module `mtrack` comme dans un jeu : un morceau, deux bruitages, pause |
| `examples/virusmeadow` | pong-casse-briques coopératif à 4 joueurs : écran d'accueil en image 8 bpp (`maps8`) avec fondu par la palette, prairie procédurale, virus sur un plan de tuiles, joueurs console pour les places libres, manettes, sticks, spinners et paddles |

## Fichiers

- `mc.h`, `mc.c` : le SDK ;
- `libc.c` et `libc/` : la petite bibliothèque C standard ;
- `mtrack.h`, `mtrack.c`, `MTRACK.md` : le module de musique (`modules = mtrack`) ;
- `build.py` : clés `library = mc`, `data = …` et `modules = …` ajoutées ; les projets existants (easy) ne changent pas.
