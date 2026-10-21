# Puzzle Bobble 2 / Bust-A-Move Again (Neo Geo, Taito 1995) sur MONOCORE-1

Portage pour un usage personnel, à partir de sa propre copie du jeu (set MAME `pbobbl2n`). Comme
pour L'Aigle d'Or, le jeu est **traduit** : le code 68000 est recompilé en C puis en RV32IM par
`tools/recomp68k`, et le matériel Neo Geo est remplacé par une bibliothèque d'exécution écrite pour
la MONOCORE-1. Rien n'est émulé sur la console. Les fichiers tirés du jeu (données, programme, code
traduit, ROM construite) restent hors du dépôt (`.gitignore`) : ne jamais pousser `pbobble2.mnc`.

**État** : le jeu se joue dans le simulateur (démo, Player vs Computer, Puzzle Game, deux joueurs),
une session de test de plus de 15 minutes sans arrêt, et jusqu'au niveau 2 contre l'ordinateur.
Musiques et bruitages originaux, de style arcade. Essayé sur la MiSTer.

## Construire la ROM

Depuis `examples/pbobble2/`, avec ta copie du jeu (`pbobbl2n.zip`) :

```sh
pip install numpy pillow capstone
python3 tools/prepare.py pbobbl2n.zip      # données tirées de ta copie
python3 tools/m68kmap.py                    # carte du code 68000
python3 tools/translate.py                  # programme traduit : build/tr/pb2_code.c (environ une minute)
python3 tools/sound/make_music.py           # musiques
python3 tools/sound/make_sfx.py             # bruitages
python3 ../../sdk/build.py .                # build/pbobble2.mnc (23,2 Mo)
```

Il faut aussi clang, ld.lld et llvm-objcopy (comme pour tout le SDK). La ROM se charge dans
`dist/monocore.html` ou se copie sur la MiSTer (`/media/fat/games/MONOCORE/`), avec le core actuel.
Commandes : Entrée = Start, flèches, Z = tirer et valider (boutons 1 à 4 = A à D) ; joueur 2 :
I/J/K/L, N, 2.

Si l'écran affiche « UNKNOWN ADDRESS », le jeu a atteint du code pas encore traduit : l'adresse est
dans la console de débogage (`UNKNOWN 0xxxxx (frame …)`). L'ajouter à `entries.txt`, puis relancer
`translate.py` et `build.py`.

## Fichiers produits par `prepare.py`

| Fichier | Contenu |
|---|---|
| `p1.bin` | programme 68000 tel que MAME le range (mots de 16 bits petit-boutistes) : c'est la forme mémoire du runtime (accès octet à `adresse ^ 1`) |
| `p1_be.bin` | le même programme gros-boutiste, pour la carte du code et le traducteur |
| `spr.bin` | tuiles de sprites 16 × 16 au format 4 bpp des plans (SPEC §4.3), dédupliquées : 34 246 sur 81 920, 4,2 Mo ; tuile 0 vide |
| `spr_cells.bin` | les mêmes tuiles au format des sprites MONOCORE (quatre cellules 8 × 8) |
| `spr_map.bin` | `uint16` par numéro de tuile Neo Geo → numéro dans `spr.bin` |
| `fix.bin` | 4 096 tuiles 8 × 8 du plan fixe, 4 bpp |
| `m1.bin`, `v.bin` | pilote son Z80 et échantillons du YM2610, pour `tools/sound` |

`--sheets` écrit des planches de tuiles dans `build/check/` pour contrôler le décodage.

## Le programme traduit

- `m68kmap.py` cartographie le code depuis les vecteurs et l'en-tête de la cartouche (environ 67 Ko
  de code et 71 tables de sauts en statique).
- Le reste est atteint par des adresses calculées pendant le jeu. Deux sources complètent la carte :
  `entries.txt` (adresses relevées à l'exécution, des adresses seulement, commité) et la récolte des
  adresses de code rangées comme données dans la ROM (tables de pointeurs, scripts) : toute valeur
  32 bits qui pointe dans la ROM et se décode proprement, plus les cibles des `lea`/`pea` relatifs
  au PC (les tâches du jeu rangent leur étape suivante par `lea suite(pc),a6 / move.l a6,(a4) /
  rts`), environ 7 000 en tout, en cache dans `build/check/harvest2.txt`. La récolte trouve d'avance
  plus des deux tiers des adresses dynamiques ; ses faux positifs sont des données traduites comme
  du code, qui ne s'exécutent jamais.
- Le jeu recopie deux routines en RAM avant de les exécuter (`D3EFE→100000`, `D3E94→10FC00`) :
  traduites avec `--alias`, déclarées dans `discover.py`.
- **Noyau multitâche** du jeu : `trap #1` crée une tâche, `trap #5` change de tâche en changeant
  `a7`. Il fonctionne tel quel, parce que le traducteur garde la pile 68000 en mémoire émulée.

## Le runtime Neo Geo (`neort.c`)

Portable, sans dépendance à la MONOCORE : carte mémoire, VRAM du LSPC, deux banques de palettes,
entrées, registre son (le pilote Z80 renvoie la commande), RAM de sauvegarde et un BIOS remplacé en
mode AES Europe (`SYSTEM_INT1`, `SYSTEM_RETURN`, `SYSTEM_IO` avec `PLAYER_START`, `CREDIT_CHECK`,
`FIX_CLEAR`, `LSP_1ST`). Deux points découverts en route : `SYSTEM_INT1` doit effacer `$10FE8C`
(attente d'image), et `FIX_CLEAR` doit remplir le plan fixe avec la tuile `$FF` (la `$20` de ce jeu
n'est pas vide).

## La couche MONOCORE (`main.c`)

| Neo Geo | MONOCORE |
|---|---|
| plan fixe | plan 0 (tuiles 8 × 8 lues en ROM), écrit au fil des écritures du jeu |
| palettes | 4 096 couleurs converties en RGB555 à chaque écriture et au changement de banque |
| décors : colonnes hautes (8 tuiles ou plus) sans réduction, chaînées ou non | plans 1 à 3 (tuiles 16 × 16 lues en ROM) : un plan prend toute colonne de même position modulo 16 qui ne chevauche pas une colonne déjà placée ; une colonne incompatible ouvre le plan suivant, ce qui garde l'ordre d'affichage |
| autres colonnes | un sprite 16 × 16 par tuile, via un cache de 480 tuiles en CHR RAM (le numéro de cellule d'un sprite n'adresse que 2 Mo, le jeu a 4,2 Mo de tuiles) |
| VRAM du LSPC | ombre en CHR RAM (68 Ko), à côté du cache |

La conversion se fait à chaque VBLANK : environ 50 000 à 85 000 cycles par image en moyenne, 163 000
au plus (10 à 20 % d'une image à 50 MHz), affichés toutes les 600 images dans la console de
débogage.

**Attente de l'image** : la boucle principale du jeu (`2AEC`–`2B24`) relance son ordonnanceur de
tâches sans arrêt en attendant l'image suivante : le processeur tournait à 100 %, et le simulateur du
navigateur, qui doit alors calculer chaque cycle, ne tenait plus le temps réel (le son « pulsait » et
grésillait). Le branchement `2B24` (et l'attente de l'animation de démarrage en `100920`) est traduit
avec `--idle` : `neo_idle()` endort le processeur (`wfi`) quand aucune image n'est passée depuis le
tour précédent, ce qui laisse au moins un tour par image, comme une Neo Geo chargée. Charge moyenne :
42 à 51 % au lieu de 89 à 100 %.

## Outils de mise au point

- `pc/bench.c` : banc PC (cadence déterministe par `M68K_TICK`, rendu de référence de l'écran Neo Geo
  en PPM, manettes scriptées, signalement des adresses non traduites, `BENCH_SOUND=1` trace les
  commandes son si compilé avec `-DNEO_SOUND_TRACE`).
- `tools/discover.py FRAMES [SCRIPT]` : traduit, construit le banc, exécute, et ajoute à
  `entries.txt` chaque adresse atteinte, jusqu'à ce qu'une séance passe. Avec `--sim`, la même
  boucle avec la ROM dans le simulateur (`tools/run.js`). Parties scriptées : `tools/play.txt`
  (banc), `tools/play_sim.txt` (Player vs Computer), `tools/play_puzzle.txt` (Puzzle Game jusqu'au
  classement), `tools/play_2p.txt` (deux joueurs) ; sans script, la démo (9 000 images passent).

## Le son

**Musiques originales.** Le pilote son du jeu (Z80) garde un état qui dépend de tout l'historique
des commandes : le même ordre ne joue pas la même chose selon ce qui l'a précédé (après `14 1A`, la
musique `2E` est muette ; dans la vraie partie, elle joue autrement que rendue seule). Des clips
pré-rendus ne peuvent donc pas reproduire ses musiques en jeu. Comme le portage servira de base à
une version 4 joueurs, les musiques sont remplacées par des **compositions originales** de style
arcade (marimba, cuivres, basse, percussions), synthétisées par `tools/sound/make_music.py` comme la
musique d'AUBE : menus (`19 1A`), carte (`19 1C`), partie contre l'ordinateur (`2E`), Puzzle Game
(`19 10`), game over (`21`). Mono, 16 bits, 22 050 Hz, 8,9 Mo ; chaque morceau est rendu deux fois
et la seconde passe est gardée, pour que sa fin rejoigne son début (réverbération comprise) et
qu'il boucle dans le matériel sans couture.

**Bruitages originaux.** Les bruitages du jeu sont remplacés aussi, par des sons faits avec les
mêmes instruments (`tools/sound/make_sfx.py`, 1 Mo) : chaque événement du pilote reçoit un son
choisi d'après ce que fait le jeu quand il l'envoie (relevé contre la manette et l'écran dans les
séances) : `1A 42` rotation du lanceur (clic), `1A 21` tir, `1A 24` bulle qui touche ou s'accroche,
`1A 22` éclatement, `1A 25`/`2A` chute de bulles, `1A 26`/`28`/`29` gros combo, `1A 2B`/`2C`
validation, `1A 27` curseur, `1A 2E`/`2F` début de manche et READY GO, `1A 30` fin de manche,
`19 21` manche gagnée, `02` animation NEOGEO. La table a le même format qu'avant.

```sh
python3 tools/sound/make_music.py        # music.bin, music_tab.bin
python3 tools/sound/make_sfx.py          # sounds.bin, sounds_tab.bin
```

Les outils qui rendaient les sons d'origine avec le vrai pilote Z80 et un YM2610 restent dans
`tools/sound/` (`build.sh`, `render.cpp`, `make_sounds.py`, `cuts.py`, `period.py`) : ils ont servi à
comprendre le pilote et ne sont plus nécessaires pour construire la ROM.

- **Protocole** (désassemblage du pilote) : le gestionnaire de NMI range chaque octet dans une file
  de 64 entrées ; la boucle principale les lit un par un. `0A`, `0E`, `14`, `15` et `18`–`1E` lisent
  un octet de paramètre : `1A 42` joue le son `42` dans la case de `1A` (le tir). Une table du pilote
  (`$2FF4`) classe les commandes seules. `tools/sound/events.py`, et `neort.c` en C, découpent les
  commandes de la même façon.
- **Sons du jeu** : `tools/sound/sounds.txt` (des numéros seulement), relevés sur les séances
  scriptées avec une ROM qui trace les commandes (`-DNEO_SOUND_TRACE`). Marqués `music:` : remplacés
  par `make_music.py` ; marqués `silent` : muets dans le jeu (rendus seuls, hors contexte, le pilote
  joue des minutes d'ADPCM-B saturé).
- **Console** (`main.c`) : la voix 0 joue la musique (un événement de musique la remplace, tout
  `14 xx` l'arrête : le jeu en envoie un à chaque changement d'écran) ; les voix 1 à 7 jouent les
  bruitages (la leur s'ils rejouent, sinon une libre, sinon le plus ancien) ; `MUSIC_VOL` et
  `SFX_GAIN` règlent l'équilibre (crêtes de 0,36 à 0,45 sur les séances) ; un son absent est
  signalé `SOUND xx yy missing`.
- **Taille** : la ROM fait 23,2 Mo (carte SD).
