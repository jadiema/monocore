# Module « mtrack » : la musique de MONOTRACKER dans les jeux

`mtrack` est le lecteur de musique du tracker **MONOTRACKER** (`examples/tracker`). Le tracker
l'utilise pour jouer ce qu'on compose, et les jeux l'utilisent pour jouer les morceaux exportés :
c'est le même code, donc un morceau sonne dans le jeu exactement comme dans le tracker.

Le matériel son de la console ne connaît qu'une fréquence, une forme d'onde et deux volumes par
canal. Tout le reste (enveloppes, vibrato, arpèges, glissements, effets) est calculé par `mtrack`
une fois par *tick*, depuis une interruption de timer : 50 ticks par seconde à 125 BPM, comme dans
les trackers classiques (`ticks/s = BPM × 2 / 5`).

## Les 16 pistes

| Pistes | Canal | Macro |
|---|---|---|
| 0 à 7 | canaux PSG 0 à 7 (carré, triangle, dent de scie, bruit) | `MT_PSG(c)` |
| 8 à 15 | voix PCM 0 à 7 (échantillons de la banque `mtbank.bin`) | `MT_PCM(v)` |

Un instrument PSG ne joue que sur une piste PSG, un instrument PCM sur une piste PCM.

## Utilisation dans un jeu

Dans `project.txt` :

    library = mc
    modules = mtrack
    data    = ../tracker/mtbank.bin monmorceau.mts

Dans le programme :

    #include "mtrack.h"

    mt_init(mtbank_file);                 // la banque d'échantillons, une fois
    mt_play(monmorceau_file, 0);          // le morceau, depuis la position 0 (timer 1)
    ...
    mt_sfx(MT_PSG(7), 60, 9, 64);         // bruitage : piste, note (1-96), instrument du morceau, volume
    mt_stop();

`examples/test_mtrack` est un exemple complet (morceau, deux bruitages, pause, niveaux des pistes).

### Bruitages

`mt_sfx` (ou `mt_note` avec un instrument quelconque) joue une note sur une piste ; tant que cette
note sonne, le morceau n'y joue plus, puis il reprend à sa note suivante. Pour qu'un bruitage ne
soit jamais coupé et ne coupe rien, réservez une piste que le morceau n'utilise pas :
`mt_reserve(MT_PSG(7), true)`.

### Fonctions

| Fonction | Rôle |
|---|---|
| `mt_init(banque)` | prépare le lecteur et ses 16 pistes |
| `mt_set_timer(t)` | timer utilisé (0 ou 1, 1 par défaut), avant `mt_play` |
| `mt_play(morceau, position)` | joue un morceau compact (`.mts`) ; -1 s'il est invalide |
| `mt_stop()`, `mt_pause(oui)`, `mt_playing()` | arrêt, pause, état |
| `mt_order()`, `mt_row()` | position et ligne jouées (synchroniser un jeu sur la musique) |
| `mt_set_tempo(vitesse, bpm)`, `mt_master(volume)` | tempo et volume général (0-64) |
| `mt_mute(piste, oui)`, `mt_reserve(piste, oui)` | coupe une piste du morceau, ou la laisse aux bruitages |
| `mt_level(piste)` | volume de sortie actuel 0-255 (vumètres, effets visuels sur le rythme) |
| `mt_note`, `mt_note_off`, `mt_sfx` | notes hors du morceau |
| `mt_tick()`, `mt_tick_hz()` | un tick, si le programme préfère cadencer lui-même |

Le tick tourne dans l'interruption du timer : le programme principal n'a rien à appeler.

## Notes, instruments, effets

Une case de motif contient une note (C-0 à B-7, ou `===` note coupée), un instrument (01-1F), un
volume (00-40) et un effet. Les instruments (32 octets) ont une enveloppe ADSR comptée en ticks,
un vibrato retardé, un arpège, un glissement de hauteur (grosse caisse PSG), une transposition, un
accord fin et une option de hauteur fixe ; en PSG, l'onde, le rapport cyclique et son balayage
(PWM, avec rebond) ; en PCM, l'échantillon et sa boucle.

| Effet | Rôle |
|---|---|
| `0xy` | arpège : note, +x, +y demi-tons, un pas par tick |
| `1xx` / `2xx` | montée / descente de xx/16 de demi-ton par tick |
| `3xx` | portamento vers la note de la case, vitesse xx/16 de demi-ton par tick |
| `4xy` | vibrato, vitesse x, ampleur y/16 de demi-ton |
| `8xx` | panoramique (00 gauche, 80 centre, FF droite) |
| `9xx` | PSG : rapport cyclique ; PCM : la note commence à l'échantillon xx × 256 |
| `Axy` | volume +x et −y par tick |
| `Bxx` | saut à la position xx |
| `Cxx` | volume de la piste (00-40) |
| `Dxx` | fin du motif, reprise à la ligne xx du suivant |
| `E1x` / `E2x` | montée / descente fine (x/16 de demi-ton, une fois) |
| `E9x` | redéclenche la note tous les x ticks |
| `ECx` | coupe la note au tick x |
| `EDx` | retarde la note de x ticks |
| `Fxx` | vitesse (01-1F ticks par ligne) ou BPM (20-FF) |

Volume final = enveloppe × volume de l'instrument × volume de la piste × volume général.

## Formats

Tous les nombres sont petit-boutistes. `tools/mtrack/mtsong.py` lit et écrit ces formats en Python.

### Case de motif (32 bits)

| Bits | Champ |
|---|---|
| 0-6 | note : 0 vide, 1-96 = C-0 à B-7, 97 note coupée |
| 7-11 | instrument 0-31 (0 : aucun) |
| 12-18 | volume : 0 vide, 1-65 = 0 à 64 |
| 19-23 | effet : 0 aucun, 1-16 = effet 0 à F |
| 24-31 | paramètre |

### Morceau compact (`.mts`, « MTS1 »)

| Octets | Contenu |
|---|---|
| 0 | `MTS1` |
| 4 | taille totale (u32, 64 Ko au plus) |
| 8 | nombre de positions, de motifs, vitesse, BPM (u8) |
| 12 | position de reprise, volume général, nombre d'instruments, drapeaux (u8) |
| 16 | 4 octets réservés |
| 20 | nom (16 octets) |
| 36 | instruments (32 octets chacun, l'instrument 0 compris) |
| … | positions (un octet par position : numéro de motif) |
| … | décalage de chaque motif depuis le début du morceau (u16) |
| … | motifs |

Un motif : un masque des pistes non vides (u16), puis pour chacune un masque des 64 lignes non
vides (u64) suivi des cases non vides (u32). Un motif vide tient en 2 octets. Le lecteur décode le
motif en cours dans un tampon de 4 Ko de la WRAM.

### Instrument (32 octets)

`name[10]`, `type` (0 PSG, 1 PCM), `wave` (onde PSG ou numéro d'échantillon), `duty` (PSG ; en
PCM : 1 = sans boucle), `duty_sweep` (s8), `volume`, `pan`, `attack`, `decay`, `sustain`,
`release`, `vib_speed`, `vib_depth`, `vib_delay`, `arp1`, `arp2` (s8), `arp_speed`,
`pitch_sweep`, `finetune`, `transpose` (s8), `flags` (bit 0 rebond du balayage, bit 1 hauteur
fixe), 2 octets réservés. C'est `mt_instrument_t` de `mtrack.h`.

### Banque d'échantillons (`mtbank.bin`, « MTBK »)

`MTBK`, nombre d'échantillons (u32), puis une entrée de 32 octets par échantillon : décalage depuis
le début de la banque, longueur en échantillons, début de boucle (`0xFFFFFFFF` : aucune),
fréquence en Hz (u32 chacun), note de référence (u8, 49 = C-4), format (1 = 16 bits signés),
2 octets libres, nom (12 octets) ; puis les échantillons. `tools/mtrack/make_bank.py` la produit
(21 sons originaux synthétisés, environ 1,2 Mo) ; les sons tenus bouclent sans raccord.

### NVRAM du tracker (« MTNV »)

`MTNV`, disposition du clavier (0 AZERTY, 1 QWERTY), dernier emplacement utilisé, 10 octets
libres, puis 8 emplacements {décalage, taille} (u32) et, à partir de l'octet 80, les morceaux
compacts les uns à la suite des autres (alignés sur 4 octets).

## Outils (`tools/mtrack/`)

    python3 sav2song.py monotracker.sav                 # liste les 8 emplacements
    python3 sav2song.py monotracker.sav 2 -o theme.mts  # emplacement 2 -> theme.mts (data = theme.mts)
    python3 sav2song.py monotracker.sav 2 -o theme.h    # ou un tableau C
    python3 sav2song.py theme.mts --dump                # le morceau en texte
    python3 make_bank.py                                # régénère examples/tracker/mtbank.bin
    python3 make_demo.py                                # régénère examples/tracker/demo.mts

Sur MiSTer, la sauvegarde du tracker est `saves/MONOCORE/monotracker.sav` (nom de la ROM) ; elle
est écrite sur la carte SD quand on ouvre l'OSD après un Ctrl+S dans le tracker.

Dans le simulateur, le tracker est une ROM intégrée : le bouton **Morceaux .mts** sous l'écran
télécharge directement les morceaux sauvés (les mêmes fichiers que `sav2song.py`), et
**Exporter .sav** / **Importer .sav…** échangent la NVRAM avec la MiSTer.
