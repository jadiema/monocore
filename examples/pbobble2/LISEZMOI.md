# Puzzle Bobble 2 / Bust-A-Move Again (Neo Geo, Taito 1995) sur MONOCORE-1

Portage pour un usage personnel, à partir de sa propre copie du jeu (set MAME `pbobbl2n`). Comme
pour L'Aigle d'Or, le jeu est **traduit** : le code 68000 est recompilé en C puis en RV32IM par
`tools/recomp68k`, et le matériel Neo Geo est remplacé par une bibliothèque d'exécution écrite pour
la MONOCORE-1. Rien n'est émulé sur la console. Les fichiers tirés du jeu (données, programme, code
traduit, ROM construite) restent hors du dépôt (`.gitignore`) : ne jamais pousser `pbobble2.mnc`.

**État** : le jeu se joue dans le simulateur (démo, Player vs Computer, Puzzle Game, deux joueurs),
une session de test de plus de 15 minutes sans arrêt. Il est muet : le son est en cours. Pas encore
essayé sur la MiSTer.

## Construire la ROM

Depuis `examples/pbobble2/`, avec ta copie du jeu (`pbobbl2n.zip`) :

```sh
pip install numpy pillow capstone
python3 tools/prepare.py pbobbl2n.zip      # données tirées de ta copie
python3 tools/m68kmap.py                    # carte du code 68000
python3 tools/translate.py                  # programme traduit : build/tr/pb2_code.c (environ une minute)
python3 ../../sdk/build.py .                # build/pbobble2.mnc (13,5 Mo)
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
  32 bits qui pointe dans la ROM et se décode proprement, environ 6 500, en cache dans
  `build/check/harvest.txt`. La récolte trouve d'avance la moitié des adresses dynamiques ; ses faux
  positifs sont des données traduites comme du code, qui ne s'exécutent jamais.
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
| décors : suites de colonnes de même hauteur espacées de 16 pixels, chaînées ou non | plans 1 à 3 (tuiles 16 × 16 lues en ROM) |
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

## Le son (en cours)

`tools/sound/build.sh` construit `build/snd/render` (ymfm, BSD-3, et un cœur Z80, MIT, récupérés à
la compilation, jamais commités ; le script corrige le cœur pour transmettre l'octet haut des
adresses d'E/S, qui choisit les banques sur la Neo Geo). Il fait tourner hors ligne le vrai pilote
Z80 du jeu avec un YM2610 : `render m1.bin v.bin out/c 6 42` rend une commande, `render … -trace
trace.txt` une trace de partie. Une partie n'envoie que 19 commandes différentes ; `42`, `24` et
`21` démarrent des sons longs (musiques probables), `19` un bruitage, `1A` et `14` aucun son seul.

Suite : lire la table de commandes du pilote pour classer musiques, bruitages et commandes de
contrôle, pré-rendre les clips, puis les jouer sur les voix PCM depuis `neort.c` quand le jeu écrit
en `0x320000`.
