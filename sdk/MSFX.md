# Module « msfx » : les bruitages de MONOSFX dans les jeux

`msfx` joue les **bruitages** créés avec l'éditeur **MONOSFX** (`examples/monosfx`) : saut, pièce,
tir, explosion… C'est le pendant de `mtrack` (la musique de MONOTRACKER) et de `mgfx` (les images
de MONOPIXEL). Un bruitage est un son court sur un canal PSG, recalculé à chaque image vidéo
(60 fois par seconde), à la manière de sfxr. L'éditeur s'en sert pour faire écouter chaque réglage,
et les jeux pour jouer une banque exportée.

## Un bruitage

| Réglage | Effet |
|---|---|
| onde | carré, triangle, dents de scie ou bruit (`PSG_SQUARE`…) |
| fréquence, glissement, accélération | la note de départ (Hz), ce qui lui est ajouté à chaque image (1/16 Hz), et ce qui est ajouté au glissement à chaque image |
| attaque, tenue, chute | le volume monte, reste, puis descend ; la durée est leur somme (en images) |
| volume | 0 à 255 |
| rapport, balayage | la forme du carré (128 = moitié), et ce qui lui est ajouté à chaque image |
| vibrato | un écart de ± n Hz, sur une période de n images |
| arpège | un saut de n demi-tons (−24 à 24) à une image donnée : le « ding-ding » d'une pièce |
| répétition | la note repart toutes les n images (bonus, sirènes) |

## Dans un jeu

    library = mc
    modules = msfx                 (avec mjeu : modules = mgfx mjeu msfx)
    data    = bruits.mfx

    #include "msfx.h"
    static ms_bank_t sfx;
    ms_open(&sfx, bruits_file, bruits_size);
    ms_play(&sfx, ms_find(&sfx, "SAUT"));     // sur un canal libre parmi ms_channels (6 et 7)
    // et à chaque image : ms_tick() (mj_frame() du module mjeu l'appelle déjà)

`ms_play` prend un canal libre parmi ceux réservés aux bruitages, ou le plus ancien s'ils sont
tous pris. On les règle avec `ms_channels(premier, nombre)`, en choisissant des canaux que la
musique (`mtrack`) n'utilise pas. Un nom inconnu (`ms_find` renvoie −1) ne joue rien, et le jeu
reste muet plutôt que de s'arrêter. `examples/plateformes` joue ainsi `SAUT`, `PIECE`, `ECRASE`,
`AIE`, `BRAVO`, `PERDU` et `MENU`.

| Fonction | Rôle |
|---|---|
| `ms_open`, `ms_check`, `ms_count`, `ms_find`, `ms_effect` | lire une banque, trouver un bruitage par son nom |
| `ms_play(b, n)`, `ms_play_on(effet, canal)` | jouer un bruitage |
| `ms_stop(canal)`, `ms_stop_all()`, `ms_playing(canal)` | arrêter, savoir si un canal joue |
| `ms_tick()` | une image de plus pour chaque bruitage en cours (à chaque image) |
| `ms_voice_start`, `ms_voice_step`, `ms_length` | le son image par image, sans le matériel : fréquence, volume, rapport (l'éditeur en dessine la courbe) |

## Format du fichier `.mfx`

Petit-boutiste, 32 octets d'en-tête puis 32 octets par bruitage (32 au plus). C'est le même octet
pour octet dans un emplacement de la NVRAM de MONOSFX. Il est lu et écrit en C par `sdk/msfx.c`
et en Python par `tools/msfx/msbank.py`, qui refait exactement les mêmes calculs (vérifié image
par image).

| Offset | Taille | Champ |
|---|---|---|
| 0 | 4 | `MFX1` |
| 4 | 4 | taille de la banque |
| 8 | 4 | nombre de bruitages, version (1), réservé |
| 12 | 16 | nom de la banque |
| 28 | 4 | réservé |

Un bruitage : nom (8 octets), onde, volume, attaque, tenue, chute, rapport, balayage (signé),
répétition, fréquence (u16), glissement (s16), accélération (s16), profondeur et période du
vibrato, arpège (signé) et son image, drapeaux, 5 octets réservés.

Dans la NVRAM, MONOSFX range ses banques comme MONOPIXEL et MONOTRACKER. On y trouve `MSNV`,
la disposition du clavier (octet 4) et le dernier emplacement (octet 5). À partir de l'octet 16
viennent 8 × {décalage, taille}, et les banques commencent à l'octet 80.

## Outils (`tools/msfx/`)

    python3 tools/msfx/msfx.py monosfx.sav                    # liste les 8 emplacements
    python3 tools/msfx/msfx.py monosfx.sav 1 -o bruits.mfx    # emplacement 1 -> data = bruits.mfx
    python3 tools/msfx/msfx.py bruits.mfx --dump              # les réglages de chaque bruitage
    python3 tools/msfx/msfx.py bruits.mfx --wav sons/         # chacun en WAV (une approximation du PSG)
    python3 tools/msfx/msfx.py bruits.mfx --to-sav monosfx.sav 2

`make_demo.py` refait la banque de démonstration (`examples/monosfx/demo.mfx`, copiée dans
`examples/plateformes/bruits.mfx`). Dans le simulateur, **Bruitages .mfx** télécharge les banques
sauvées.
