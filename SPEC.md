# MONOCORE-1 — spécification de la console virtuelle

Version 0.4 — octobre 2026 (4 manettes, entrées analogiques et clavier ; horloge unique de 50 MHz)

## 1. Principe

MONOCORE-1 est une console figée, conçue pour un seul bitstream sur MiSTer (DE10-Nano, Cyclone V 5CSEBA6). Elle n'émule aucune machine : les jeux d'arcade sont **traduits** vers elle (recompilation statique du code CPU d'origine vers RV32IM, réécriture des accès au matériel vidéo et son vers les blocs génériques décrits ici, conversion des graphismes et échantillons).

Choix directeurs :

- **CPU unique et rapide** : un RISC-V 32 bits à 50 MHz. C'est la cible la plus simple pour un traducteur (LLVM la connaît, jeu d'instructions régulier) et il absorbe sans peine le CPU principal *et* le CPU son d'une carte d'arcade 8 ou 16 bits (Z80, 6502, 6809, 68000 de 1 à 12 MHz).
- **Vidéo « sur-ensemble »** : quatre plans de tuiles et 512 sprites avec zoom couvrent ce que font la plupart des cartes 1978–1995 (Namco, Sega System 16, Capcom CPS1, Neo Geo, Midway, Konami…). Le rendu se fait **ligne par ligne**, donc les effets de raster (changement de défilement en cours d'image) restent possibles.
- **Son générique** : 8 voix PCM (dont ADPCM OKI) et 8 canaux PSG, plus un emplacement réservé pour un bloc FM compatible YM2151.
- **Sans système** : pas d'OS, pas de BIOS. Le processeur ARM du DE10-Nano n'est utilisé que par le framework MiSTer pour charger la ROM depuis la carte SD et afficher l'OSD ; la console elle-même n'en dépend pas.

## 2. Processeur

| Élément | Valeur |
|---|---|
| Jeu d'instructions | RV32IM, mode machine uniquement, pas de MMU |
| Cœur | Hazard3 (Apache 2.0), configuré RV32IM, multiplieur rapide, diviseur 2 bits par cycle |
| Fréquence | 50 MHz, horloge unique de toute la console (le cœur Hazard3 ne dépasse pas environ 53 MHz sur ce FPGA) |
| Caches | 8 Ko instructions et 4 Ko de données en lecture seule, devant la ROM (SDRAM) ; la WRAM est en mémoire interne, sans cache |
| Interruptions | une seule ligne externe (MEI, `mcause` = 0x8000000B), voir §6 |
| CSR | `mstatus` (MIE/MPIE), `mie` (MEIE), `mip` (MEIP), `mtvec` (mode direct), `mepc`, `mcause`, `mscratch`, `mcycle[h]`, `minstret[h]`, `mcountinhibit` (les compteurs peuvent démarrer inhibés : `crt0.S` les active) |
| `wfi` | met le CPU en sommeil jusqu'à une interruption activée dans `mie`, même si `mstatus.MIE` = 0 |

Modèle de temps du simulateur : 1 cycle par instruction, +1 pour un chargement, +2 pour un saut ou un branchement pris, +2 pour une multiplication, +16 pour une division ou un reste. Il a été recalé sur la simulation du core : écart de 2 à 14 % selon le type de code (0,94 instruction par cycle mesurée sur un CRC32, soit environ 47 millions par seconde). C'est une estimation : un programme se synchronise sur les interruptions, jamais sur un nombre de cycles.

Au reset : `pc` = `0x100` (vecteur fixe, juste après l'en-tête), interruptions désactivées, `DISP_CTRL` = 0 (affichage éteint). Le reset remet à zéro les registres mais **pas le contenu des mémoires** (WRAM, palette, sprites, tilemaps, CHR RAM, registres des voix et canaux son) : sur MiSTer, charger une nouvelle ROM les laisse tels que le jeu précédent les a laissés. Le code de démarrage du SDK (`sdk/crt0.S`) les met à zéro par DMA avant `main` (environ 2 ms) ; la NVRAM est conservée.

## 3. Carte mémoire

| Adresse | Taille | Contenu | Accès |
|---|---|---|---|
| `0x0000_0000` | 32 Mo max | ROM (code, données, graphismes, échantillons), en SDRAM (barrette MiSTer de 32 Mo ou plus) | lecture |
| `0x1000_0000` | 128 Ko | RAM de travail (WRAM), mémoire interne du FPGA | lecture/écriture |
| `0x1800_0000` | 32 Ko | NVRAM (scores, réglages ; sauvegardée) | lecture/écriture |
| `0x2000_0000` | 4 Ko | registres vidéo | l/é |
| `0x2001_0000` | 8 Ko | palette : 4096 couleurs × 16 bits | l/é |
| `0x2002_0000` | 8 Ko | table des sprites : 512 × 16 octets | l/é |
| `0x2004_0000` | 64 Ko | tilemaps : 4 plans × 16 Ko | l/é |
| `0x2008_0000` | 8 Ko | tables de défilement par ligne : 4 × 2 Ko | l/é |
| `0x200C_0000` | 128 Ko | CHR RAM (graphismes modifiables par le jeu) | l/é |
| `0x3000_0000` | 4 Ko | registres son | l/é |
| `0x4000_0000` | 4 Ko | entrées, interruptions, timers, DMA | l/é |

Tous les espaces acceptent des accès 8, 16 et 32 bits, petit-boutiste. Les graphismes sont lus par la vidéo en ROM ou en CHR RAM, les échantillons sonores en ROM seulement (ni la WRAM ni la CHR RAM n'ont de port libre pour eux). Les registres « base » contiennent simplement une adresse CPU ; une base hors de ces deux zones désactive le plan ou les sprites, et un mot lu au-delà de la fin de la zone vaut 0 (transparent).

## 4. Vidéo

### 4.1 Format d'image

- Résolution native réglable : largeur 224 à 384 pixels, hauteur 192 à 288 lignes (`SCREEN_W`, `SCREEN_H`, pris en compte à l'image suivante). Défaut : 320 × 240.
- Nombre total de lignes par image : `SCREEN_H + 24` (24 lignes de blanking vertical).
- Une ligne dure `4 × ⌊50 000 000 / (240 × lignes totales)⌋` cycles CPU (un nombre entier de pixels à 12,5 MHz), soit 3 156 cycles et 60,01 Hz en 240 lignes (2 668 cycles en 288 lignes, la ligne la plus courte). La hauteur est prise en compte au début de chaque image.
- Rotation de sortie 0°, 90°, 180°, 270° pour les jeux verticaux (Pac-Man, Galaga… se décrivent en 224 × 288 puis se tournent de 90°). La rotation est faite par le scaler de sortie du framework MiSTer.
- Couleurs : palette de 4096 entrées RGB555 (`0RRRRRGGGGGBBBBB`, R dans les bits 14–10).

### 4.2 Registres (`0x2000_0000`)

| Offset | Nom | Description |
|---|---|---|
| 0x000 | `DISP_CTRL` | bit 0 affichage ; bits 1–4 plans 0–3 ; bit 5 sprites ; bits 8–9 rotation (0, 90, 180, 270° horaire) |
| 0x004 | `SCREEN_W` | largeur visible (224–384) |
| 0x008 | `SCREEN_H` | hauteur visible (192–288) |
| 0x00C | `BG_COLOR` | index de palette de la couleur de fond |
| 0x010 | `LINE_CMP` | ligne qui déclenche l'interruption LINE (comparée sur 32 bits : une valeur hors écran, par exemple 0x7FFF, la désactive) |
| 0x014 | `VCOUNT` | ligne en cours (lecture seule) ; 0 à `SCREEN_H−1` visibles |
| 0x018 | `FRAME` | compteur d'images (lecture seule) |
| 0x01C | `STATUS` | bit 0 dans le blanking vertical ; bit 1 débordement de sprites pendant l'image précédente |
| 0x020 | `SPR_CHR` | adresse de base des graphismes de sprites |
| 0x024 | `SPR_COUNT` | nombre d'entrées de la table de sprites parcourues (0–512) |
| 0x028 | `SPR_PEAK` | statistique : maximum de sprites sur une ligne pendant l'image précédente |
| 0x100 + n×0x20 | `Ln_CTRL` | plan n : bit 0 tuiles 16 × 16 (sinon 8 × 8) ; bit 1 8 bpp (sinon 4 bpp) ; bits 2–3 largeur de carte (0 = 32, 1 = 64, 2 = 128 tuiles) ; bits 4–5 hauteur (idem, largeur × hauteur ≤ 4096) ; bit 6 défilement par ligne ; bits 8–9 priorité du plan (0–3) |
| +0x04 | `Ln_CHR` | adresse de base des graphismes du plan |
| +0x08 | `Ln_SCROLLX` | défilement horizontal (signé 16 bits) |
| +0x0C | `Ln_SCROLLY` | défilement vertical (signé 16 bits) |

### 4.3 Graphismes

- 4 bpp : 2 pixels par octet, quartet bas = pixel de gauche. Tuile 8 × 8 = 32 octets ; 16 × 16 = 128 octets ; lignes de haut en bas.
- 8 bpp : 1 octet par pixel. Tuile 8 × 8 = 64 octets ; 16 × 16 = 256 octets.
- La valeur de pixel 0 est toujours transparente.
- Index de couleur final : `(palette × 16 + pixel) & 0xFFF`. En 4 bpp, une palette fait 16 couleurs ; en 8 bpp, le champ palette décale la fenêtre de 256 couleurs.

### 4.4 Tilemaps

Une entrée de 32 bits par tuile, rangées ligne par ligne (`0x2004_0000 + plan × 0x4000`) :

| Bits | Champ |
|---|---|
| 15–0 | numéro de tuile (relatif à `Ln_CHR`) |
| 23–16 | palette |
| 24 | miroir horizontal |
| 25 | miroir vertical |
| 26 | priorité haute (+1, voir §4.7) |

La carte boucle dans les deux directions.

### 4.5 Moment du rendu

Chaque ligne visible y est calculée au début de sa propre période (quand `VCOUNT` passe à y), puis affichée pendant la ligne suivante ; ce décalage d'une ligne est invisible pour le logiciel. Le calcul utilise l'état des registres et des mémoires vidéo (palette comprise) **au début de la ligne** : pendant ce calcul, une écriture du CPU ou du DMA dans le bloc vidéo (`0x2000_0000`) est mise en attente jusqu'à sa fin. Une écriture faite pendant la ligne y prend donc toujours effet à la ligne y + 1, que ce soit un défilement, une couleur de palette ou une tuile. Les lectures ne sont jamais retardées.

La durée du calcul dépend du contenu : environ 400 cycles pour une ligne de texte simple, un peu moins de 900 pour le menu de la ROM de test (deux plans, un sprite 8 bpp zoomé). Elle reste toujours inférieure à la durée d'une ligne : le pire cas mesuré (4 plans 16 × 16 en 8 bpp lus en ROM et sprites 8 bpp réduits au budget maximal) prend environ 1 550 cycles, pour 2 668 cycles dans la ligne la plus courte.

### 4.6 Défilement par ligne

Si le bit 6 de `Ln_CTRL` est actif, la ligne d'écran y du plan n ajoute deux décalages lus en `0x2008_0000 + n × 0x800 + y × 4` : un `int16` horizontal puis un `int16` vertical. C'est ce qui sert pour les routes de jeux de course, les vagues, les « rowscroll » des cartes Capcom ou Konami. Pour les effets plus exotiques, l'interruption LINE permet de modifier n'importe quel registre entre deux lignes.

### 4.7 Sprites

Table de 512 entrées de 16 octets (`0x2002_0000`) :

| Offset | Taille | Champ |
|---|---|---|
| 0 | int16 | X (coin haut-gauche, peut être négatif) |
| 2 | int16 | Y |
| 4 | u16 | première cellule 8 × 8 (relative à `SPR_CHR`, en unités de 32 octets en 4 bpp, 64 en 8 bpp) |
| 6 | u8 | palette |
| 7 | u8 | bit 0 miroir H ; bit 1 miroir V ; bits 2–4 priorité (0–7) ; bit 5 8 bpp ; bit 7 actif |
| 8 | u8 | largeur en cellules (1–32) |
| 9 | u8 | hauteur en cellules (1–32) |
| 10 | u16 | réservé |
| 12 | u16 | zoom horizontal (8.8 ; 0x100 = 1,0 ; 0 vaut 1,0) |
| 14 | u16 | zoom vertical |

La cellule (cx, cy) d'un sprite est `tuile + cy × largeur + cx`. Un sprite fait donc jusqu'à 256 × 256 pixels avant zoom.

**Zoom.** Le sprite occupe `⌊largeur × 8 × ZOOMX / 256⌋` × `⌊hauteur × 8 × ZOOMY / 256⌋` pixels à l'écran. Le pixel d'écran situé à (dx, dy) du coin haut-gauche affiche le pixel source `(⌊dx × 256 / ZOOMX⌋, ⌊dy × 256 / ZOOMY⌋)`, puis les miroirs s'appliquent (`sx' = largeur × 8 − 1 − sx`). Le matériel calcule ces divisions exactement.

**Priorités.** Chaque pixel reçoit une priorité de 0 à 7 : `2 × priorité du plan + bit de priorité de la tuile` pour un plan, la priorité de l'entrée pour un sprite. La plus haute l'emporte. À égalité : un sprite passe devant un plan, un plan d'indice plus bas devant un indice plus haut, un sprite d'indice plus bas devant un indice plus haut.

**Limites matérielles par ligne** : 64 sprites, 1280 pixels de sprites affichés et 512 mots de graphismes. Un sprite compte pour `min(largeur affichée, mots d'une ligne source)` mots, soit `largeur` en 4 bpp et `2 × largeur` en 8 bpp (en cellules) ; ce budget ne gêne que les sprites très réduits, qui lisent un mot par pixel. Au-delà, les sprites d'indice le plus élevé sont ignorés et le bit 1 de `STATUS` passe à 1. Ces limites viennent du budget du tampon de ligne (voir §9) ; elles couvrent la plupart des cartes d'origine (16 à 96 sprites par ligne ; les plus chargées, comme la Neo Geo, plafonnent à 96 sprites de 16 pixels, soit 1536 pixels, et peuvent donc perdre des sprites sur les lignes les plus remplies).

## 5. Son

Sortie 48 kHz stéréo, 16 bits. Un échantillon est calculé au cycle `⌈k × 50 000 000 / 48 000⌉` (k = 1, 2…) depuis le reset, soit toutes les 1 041,67 cycles. Tout le calcul est en entiers et défini ci-dessous au bit près : le simulateur et le core produisent exactement les mêmes échantillons à partir des mêmes écritures de registres. Le calcul d'un échantillon prend en général moins de 350 cycles après son instant, et au plus environ 750 cycles (8 voix ADPCM au pas maximal), toujours moins que l'intervalle entre deux échantillons ; une écriture du CPU ou du DMA dans le bloc son pendant ce calcul attend sa fin, si bien qu'une écriture s'applique toujours entière à un échantillon donné, jamais à une partie des voix seulement.

### 5.1 Voix PCM (8 voix, `0x3000_0000 + v × 0x20`, v = 0 à 7 ; `0x3000_0100`–`0x3000_01FF` est réservé)

| Offset | Nom | Description |
|---|---|---|
| 0x00 | `START` | adresse de l'échantillon, **en ROM** (au-delà de la fin de la ROM, les octets valent 0) |
| 0x04 | `LENGTH` | longueur en échantillons |
| 0x08 | `LOOP` | index de reprise de boucle ; `0xFFFFFFFF` = pas de boucle ; ignoré en ADPCM |
| 0x0C | `STEP` | pas de lecture 16.16 (0x10000 = 48 kHz ; 0x8000 = 24 kHz…) |
| 0x10 | `VOL_L` (u8) | volume gauche 0–255 |
| 0x11 | `VOL_R` (u8) | volume droit |
| 0x12 | `FORMAT` (u8) | 0 = signé 8 bits ; 1 = signé 16 bits ; 2 et plus = ADPCM OKI MSM6295 (4 bits, quartet haut d'abord) |
| 0x14 | `CTRL` | écriture 1 = lancer depuis le début, 0 = couper ; lecture bit 0 = en cours |
| 0x18 | `POS` | position courante (lecture seule) |

Pour chaque échantillon de sortie, une voix en cours :

1. s'arrête si `POS ≥ LENGTH` ;
2. lit sa valeur `s` sur 16 bits : octet signé × 256 en 8 bits ; mot signé petit-boutiste à `START + 2 × POS` en 16 bits ; en ADPCM, décode les quartets jusqu'à la position (16 au plus par échantillon de sortie) et prend le signal 12 bits × 16 ;
3. ajoute `(s × VOL_L) >> 8` et `(s × VOL_R) >> 8` aux sommes gauche et droite (décalage arithmétique) ;
4. avance : `somme = fraction + STEP`, `POS += somme >> 16`, `fraction = somme & 0xFFFF` ;
5. si `POS ≥ LENGTH` : en PCM avec une boucle valide (`LOOP < LENGTH`), `POS −= LENGTH − LOOP`, puis s'arrête si `POS` reste hors de l'échantillon ; sinon s'arrête.

L'ADPCM OKI est le format des MSM6295, présents sur une grande partie des cartes des années 1990 : leurs échantillons se chargent tels quels. Les MSM6295 ne bouclent pas, d'où l'absence de boucle en ADPCM.

### 5.2 Canaux PSG (8 canaux, `0x3000_0400 + c × 0x10`)

| Offset | Nom | Description |
|---|---|---|
| 0x00 | `FREQ` | fréquence en 1/16 Hz (20 bits utiles, jusqu'à 65 535 Hz) |
| 0x04 | `WAVE` (u8) | 0 carré, 1 triangle, 2 dent de scie, 3 bruit (autre valeur : silence) |
| 0x05 | `DUTY` (u8) | rapport cyclique du carré sur 256 (0 vaut 128, soit 50 %) |
| 0x06 | `VOL_L` (u8) | volume gauche |
| 0x07 | `VOL_R` (u8) | volume droit |

Un canal dont les deux volumes ou `FREQ` sont nuls est figé (sa phase n'avance pas). Sinon, à chaque échantillon, sa phase de 32 bits avance de `⌊FREQ × 366 503 875 / 65 536⌋` (2³² / 768 000 en virgule fixe) modulo 2³², puis, avec `t = phase >> 16` :

| Onde | Valeur `s` |
|---|---|
| carré | `+16 384` si `phase < DUTY × 2²⁴`, sinon `−16 384` |
| triangle | `t − 16 384` si `t < 32 768`, sinon `49 152 − t` |
| dent de scie | `(t >> 1) − 16 384` |
| bruit | `±16 384` selon un LFSR 15 bits (`bit = l₀ ⊕ l₁`, `l = (l >> 1) ∣ (bit << 14)`, départ à 1), avancé d'un pas chaque fois que la phase franchit une demi-période |

puis `(s × VOL) >> 8` s'ajoute à chaque côté. Ces canaux couvrent les AY-8910, SN76489 et générateurs d'ondes Namco ; le traducteur convertit leurs registres de période en `FREQ`.

### 5.3 Mixage et divers

Chaque côté de la sortie vaut `⌊somme × MASTER_VOL × 77 / 65 536⌋`, borné à ±32 767 (le facteur 77/256 ≈ 0,3 laisse de la marge aux 16 sources).

| Adresse | Nom | Description |
|---|---|---|
| `0x3000_0F00` | `MASTER_VOL` | volume général 0–255 (défaut 255) |
| `0x3000_1000` | réservé FM | bloc compatible YM2151 (registres adresse/donnée), prévu plus tard ; non simulé |

## 6. Entrées, interruptions, timers, DMA (`0x4000_0000`)

| Offset | Nom | Description |
|---|---|---|
| 0x00 | `IN_P1` | bit 0 haut, 1 bas, 2 gauche, 3 droite, 4–9 boutons 1–6, 10 start, 11 pièce (1 = appuyé) |
| 0x04 | `IN_P2` | idem joueur 2 |
| 0x08 | `IN_SYS` | bit 0 service, bit 1 test, bit 2 tilt |
| 0x0C | `DIP` | 32 bits de DIP switches, réglés dans l'OSD |
| 0x10 | `IRQ_STATUS` | bit 0 VBLANK, 1 LINE, 2 TIMER0, 3 TIMER1, 4 DMA ; écrire 1 pour acquitter |
| 0x14 | `IRQ_ENABLE` | masque des sources |
| 0x20 | `T0_RELOAD` | période du timer 0 en cycles CPU |
| 0x24 | `T0_CTRL` | bit 0 marche |
| 0x28 | `T1_RELOAD` | timer 1 |
| 0x2C | `T1_CTRL` | |
| 0x30 | `CYCLE_LO` | compteur de cycles, 32 bits bas |
| 0x34 | `CYCLE_HI` | 32 bits hauts |
| 0x40 | `DMA_SRC` | source (ou motif 32 bits en mode remplissage) |
| 0x44 | `DMA_DST` | destination |
| 0x48 | `DMA_LEN` | longueur en octets |
| 0x4C | `DMA_CTRL` | écriture bit 0 lancer, bit 1 mode remplissage ; lecture bit 0 occupé |
| 0x60 | `DEBUG_OUT` | un octet écrit = un caractère sur la console de debug (absent du FPGA final) |
| 0x70 | `HW_VERSION` | version du matériel : 2 depuis l'ajout des joueurs 3 et 4, de l'analogique et du clavier (1 avant) |
| 0x80 | `IN_P3` | joueur 3, même format que `IN_P1` |
| 0x84 | `IN_P4` | joueur 4 |
| 0x90 + 4n | `ANALOG_Pn` | manette n (0-3 = joueurs 1-4) : stick gauche X (bits 7–0) et Y (15–8), stick droit X (23–16) et Y (31–24), signés de −127 à +127, 0 au repos ; Y positif vers le bas |
| 0xA0 + 4n | `PADDLE_Pn` | paddle de 0 à 255 (bits 7–0) ; bits 31–16 : position du spinner, compteur 16 bits qui accumule ses déplacements |
| 0xB0 + 4k | `KEY_STATE0`–`KEY_STATE7` | clavier : 256 bits, 1 = touche enfoncée ; la touche de code PS/2 (jeu 2) *c* est le bit *i* = c (bit 7 de *c* à 0), + 128 pour un code étendu (préfixe E0) ; le code 0x83 (F7) prend la place libre i = 0x02 |
| 0xD0 | `KEY_EVENT` | lecture : plus ancien événement clavier non lu, bit 31 valide, bit 9 appui (0 = relâchement), bit 8 étendu, bits 7–0 code PS/2 ; écriture (valeur quelconque) : retire cet événement. File de 16 événements ; une fois pleine, les nouveaux sont perdus |

Les joueurs 3 et 4, l'analogique et le clavier viennent du framework MiSTer : manettes 3 et 4 de `hps_io`, sticks analogiques, paddles et spinners des manettes 1 à 4, clavier USB de la carte (codes PS/2). Une ROM qui veut les utiliser vérifie que `HW_VERSION` ≥ 2. La lecture de ces registres n'a aucun effet de bord : seule l'écriture dans `KEY_EVENT` fait avancer la file.

VBLANK se déclenche au début de la ligne `SCREEN_H`, LINE au début de la ligne `LINE_CMP`. La ligne d'interruption du CPU vaut `(IRQ_STATUS & IRQ_ENABLE) != 0`.

Les timers servent typiquement à cadencer le pilote son traduit à la fréquence d'origine (souvent 60 à 250 Hz). Le DMA sert aux transferts ROM → VRAM ou à l'effacement de la mémoire. Le CPU reste suspendu sur l'écriture de `DMA_CTRL` jusqu'à la fin du transfert, ce qui évite toute course entre les deux. Il transfère des mots de 32 bits quand la source (en copie) et la destination sont alignées, sinon des octets. Coût mesuré : remplissage 1 cycle par mot ; copie 3 cycles par mot, plus environ 2,5 par mot lu en ROM (soit environ 0,7 octet par cycle depuis la ROM et 1,3 octet par cycle entre mémoires internes).

## 7. Format de ROM (`.mnc`)

La ROM est l'image brute de l'espace `0x0000_0000`. Les 256 premiers octets forment l'en-tête :

| Offset | Taille | Champ |
|---|---|---|
| 0x00 | 4 | signature `MNC1` |
| 0x04 | 4 | version d'en-tête (1) |
| 0x08 | 4 | point d'entrée : doit valoir `0x100` (le CPU démarre toujours là) |
| 0x0C | 4 | taille de la ROM |
| 0x10 | 4 | bits 0–1 rotation par défaut, utilisée par l'interface (OSD, simulateur) pour orienter l'écran avant que le jeu n'écrive `DISP_CTRL` |
| 0x14 | 4 | DIP switches par défaut, proposés par l'interface |
| 0x20 | 32 | titre (UTF-8, complété par des zéros) |
| 0x40 | 32 | origine (par exemple le nom du jeu MAME traduit) |

Sur MiSTer, une ROM se charge depuis la carte SD (OSD, ou `boot.rom` au démarrage) ou depuis une **cartouche** branchée sur le port USER : une ou deux Flash SPI de 8 Mo (16 Mo au total) qui contiennent le fichier `.mnc` tel quel. Le core en vérifie l'en-tête puis la copie dans la SDRAM ; pour le jeu, les deux chargements sont identiques (voir `mister/LISEZMOI.md`).

Le code commence en `0x100`. Une ROM traduite contient typiquement : le code 68000/Z80 recompilé, une petite bibliothèque d'exécution (gestion des interruptions, traduction des écritures matérielles), les graphismes convertis au format §4.3 et les échantillons au format §5.1.

## 8. Chaîne de portage visée

1. Désassemblage et recompilation statique du ou des CPU d'origine vers RV32IM (même méthode que pour la traduction JavaScript de Pigskin, avec une autre sortie).
2. Remplacement des accès à la vidéo et au son d'origine par des écritures dans les blocs MONOCORE (repérés à la traduction, réécrits en appels de la bibliothèque d'exécution).
3. Conversion des ROM graphiques et des échantillons.
4. Vérification par comparaison de traces (RAM, image, son) avec MAME, dans le simulateur puis sur la carte.

## 9. Budget FPGA estimé (Cyclone V 5CSEBA6)

| Bloc | ALM | M10K (Kbit) | Remarque |
|---|---|---|---|
| CPU RV32IM + caches | ~4 000 | 120 | Hazard3, 3 étages ; caches 8 Ko + 4 Ko |
| Vidéo (4 plans, sprites, palette) | ~8 000 | ~700 | tilemaps 512, palette 64, sprites 64, tables de ligne 64, tampons de ligne |
| WRAM | — | 1 024 | 128 Ko |
| CHR RAM | — | 1 024 | 128 Ko |
| NVRAM et registres son | — | 320 | 32 Ko + 8 Ko |
| Son | ~2 500 | 20 | 8 voix PCM + 8 PSG, multiplieurs en DSP |
| Contrôleur SDRAM + arbitrage | ~2 000 | 40 | ROM en SDRAM |
| Framework MiSTer (scaler, OSD, HDMI) | ~10 000 | ~1 000 | |
| **Total** | **~27 000 / 41 910 (≈ 65 %)** | **~4 300 / 5 570** | |

Bande passante SDRAM (16 bits, ~200 Mo/s) : au pire, 4 plans 8 bpp en 384 pixels + 512 mots de sprites par ligne ≈ 3,5 Ko par ligne × 18 700 lignes/s ≈ 65 Mo/s pour la vidéo, le reste pour le CPU (amorti par ses caches) et le son.

Le rendu d'une ligne trace les sprites à un pixel par cycle : 1280 pixels de sprites, plus les plans et la palette, tiennent dans une période de ligne (au moins 2 668 cycles ; le pire cas mesuré prend environ 1 550 cycles), d'où les limites de §4.7.

## 10. Hors périmètre de la v0.1

- Bloc FM (YM2151, puis YM2203/YM2612) : emplacement réservé en `0x3000_1000`.
- Transparence et ombres (shadow/highlight des cartes Sega et Capcom).
- Rotation et déformation de plans (mode 7, sprites tournants de type Konami).
- Affichage vectoriel.
