# Puzzle Bobble 2 / Bust-A-Move Again (Neo Geo, Taito 1995) sur MONOCORE-1

Portage pour un usage personnel, à partir de sa propre copie du jeu (set MAME `pbobbl2n`). Comme
pour L'Aigle d'Or, le jeu est **traduit** : le code 68000 est recompilé en C puis en RV32IM par
`tools/recomp68k`, et le matériel Neo Geo est remplacé par une bibliothèque d'exécution écrite pour
la MONOCORE-1. Rien n'est émulé sur la console. Les fichiers tirés du jeu (données, programme, code
traduit, ROM construite) restent hors du dépôt (`.gitignore`) : ne jamais pousser `pbobble2.mnc`.

**État** : le jeu se joue dans le simulateur (démo, Player vs Computer, Puzzle Game, deux joueurs),
une session de test de plus de 15 minutes sans arrêt, et jusqu'au niveau 2 contre l'ordinateur.
Musiques et bruitages sont là. Pas encore essayé sur la MiSTer.

## Construire la ROM

Depuis `examples/pbobble2/`, avec ta copie du jeu (`pbobbl2n.zip`) :

```sh
pip install numpy pillow capstone
python3 tools/prepare.py pbobbl2n.zip      # données tirées de ta copie
python3 tools/m68kmap.py                    # carte du code 68000
python3 tools/translate.py                  # programme traduit : build/tr/pb2_code.c (environ une minute)
sh tools/sound/build.sh && python3 tools/sound/cuts.py && python3 tools/sound/make_sounds.py   # le son
python3 ../../sdk/build.py .                # build/pbobble2.mnc (31,2 Mo)
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
débogage. La charge CPU du simulateur reste élevée parce que le programme Neo Geo attend l'image
suivante dans une boucle active.

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

Le pilote son du jeu tourne sur le Z80 et pilote un YM2610, que la MONOCORE n'a pas : chaque son
est **rendu hors ligne** par le vrai pilote, puis joué sur les voix PCM. Rien n'est émulé sur la
console.

```sh
sh tools/sound/build.sh                 # build/snd/render (ymfm BSD-3 et un cœur Z80 MIT, récupérés, jamais commités)
python3 tools/sound/cuts.py              # quel son coupe quel son (quelques minutes)
python3 tools/sound/make_sounds.py       # sounds.bin et sounds_tab.bin (18,8 Mo), puis sdk/build.py
```

- **Protocole** (désassemblage du pilote) : le gestionnaire de NMI range chaque octet dans une file
  de 64 entrées ; la boucle principale les lit un par un. `0A`, `0E`, `14`, `15` et `18`–`1E` lisent
  un octet de paramètre : `1A 42` joue le son `42` dans la case de `1A` (le tir), `14 19` arrête la
  case de `19`. Une table du pilote (`$2FF4`) classe les commandes seules : `20`–`47` sont des
  musiques qui se remplacent, `60`–`63`, `7F`, `FE`, `FF` des bruitages ; `01`, `03`, `04`, `0D`, `20`
  arrêtent tout, `05` fait un fondu. `tools/sound/events.py` découpe un flux de commandes de la même
  façon, et `neort.c` aussi, en C.
- **Sons du jeu** : `tools/sound/sounds.txt` (des numéros seulement), relevés sur les séances
  scriptées avec une ROM qui trace les commandes (`-DNEO_SOUND_TRACE`). 27 événements. Trois d'entre
  eux (`1A 14`, `1A 19`, `1D 19`) sont muets dans le jeu, mais rendus seuls, hors de leur contexte,
  donnent des minutes d'ADPCM-B saturé : ils sont marqués `silent`.
- **Boucles** : `tools/sound/period.py` les cherche dans les écritures du pilote au YM2610
  (`render … -writes`), qui se répètent pas pour pas, alors que le son ne se répète pas au bit près.
  `2E` (la partie) reboucle toutes les 281 s, `21` toutes les 60 s ; les longs morceaux qui finissent
  d'eux-mêmes sont gardés 30 s (le jeu les coupe en 20 s au plus).
- **Format** : PCM 16 bits mono ; les sons courts (12 s au plus) à 22 050 Hz, les musiques et longs
  morceaux à 16 000 Hz pour que tout tienne dans 32 Mo. Chaque son est normalisé à sa crête, et son
  niveau dans le jeu est rendu par le volume de lecture. Les musiques bouclent dans le matériel
  (`LOOP`). Essayés d'abord et abandonnés : l'ADPCM OKI (rapport signal sur bruit de 14 dB sur le tir,
  21 dB sur la musique : le son grésille) et le PCM 8 bits (36 à 39 dB : un souffle audible) ; le
  PCM 16 bits donne 80 à 87 dB.
- **Console** (`main.c`) : chaque son prend une voix (la sienne s'il joue déjà, sinon une libre, sinon
  le bruitage le plus ancien) ; il coupe les sons dont il prend les canaux YM2610 dans le pilote
  (table mesurée par `cuts.py` : les sons de la case `19` se coupent entre eux, ceux de la case `1A` se
  superposent) ; un son absent de la table est signalé `SOUND xx yy missing` dans la console de
  débogage. Comparé au pilote sur la même partie, l'enveloppe du son a une corrélation de 0,91.
- **Taille** : la ROM passe à 31,2 Mo : elle tient sur la carte SD (32 Mo, de justesse), plus sur la
  cartouche (16 Mo).
