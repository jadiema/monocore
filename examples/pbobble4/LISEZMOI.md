# Puzzle Bobble 2 / Bust-A-Move Again en 4 joueurs, sur MONOCORE-1

La version **4 joueurs** du portage `examples/pbobble2` (qui reste le portage fidèle à 2 joueurs).
Le jeu d'origine ne gère que 2 joueurs ; celui-ci en accepte jusqu'à 4, chacun sur sa manette.

## Jouer

1. **À l'écran titre (ou pendant la démonstration), n'importe quelle manette appuie sur Start** (rien
   ne l'indique à l'écran, pour laisser le titre du jeu intact) : un
   menu s'ouvre (son mode d'emploi y est écrit), que toutes les manettes peuvent régler : haut /
   bas choisit la ligne, gauche / droite change le chiffre ou la difficulté ; en VERSUS à 3 ou 4
   joueurs, la ligne STAGE choisit le stage de la première manche (RANDOM : la suite du jeu,
   sans rien forcer ; le jeu passe ensuite au stage suivant à chaque manche), la ligne SCREEN
   choisit l'écran (LINE : les aires côte à côte, 2 X 2), au départ
   celui du DIP switch 1 ; en ARENA (2 à 4 joueurs), la ligne TEAMS (à 4 joueurs) joue en équipes, P1 + P2 contre P3 + P4 (les points
   s'additionnent, les totaux s'affichent sous les scores, l'équipe au plus grand total gagne),
   la ligne CPU donne les derniers dragons à l'ordinateur (de 0 à
   joueurs - 1, leur score s'affiche C3, C4… ; LEVEL règle leur force : EASY vise à 6° près et
   réfléchit une seconde, NORMAL 3° et une demi-seconde, HARD juste et tout de suite), la ligne
   TIME la durée (1, 2, 3 ou 5 minutes) ; un tir qui s'accroche sur la ligne du bas coûte 50 points et 2 secondes sans tirer,
   et chaque bulle que le plafond pousse sur cette ligne coûte 10 points au joueur dont le
   lanceur est en dessous ; un joueur qui ne tire pas pendant 8 secondes voit
   HURRY! clignoter au-dessus de son lanceur, et 3 secondes plus tard son tir part tout seul ; la ligne COLORS choisit la règle (MONO : chacun tire sa couleur et ne fait éclater que
   la sienne ; MULTI : six couleurs du jeu, chacun tire des bulles de toutes les couleurs, la
   suivante annoncée dans son panneau NEXT, tout groupe de 3 éclate pour celui qui l'a formé), la
   ligne STAGE choisit le décor (RANDOM, ou l'un des 8 stages
   du versus : BEACH, GEARS, SUNSET, TOWN, TEMPLE, WALL, STATUES, FOREST) et la ligne GUIDE règle
   l'aide à la visée : le **nombre de joueurs** (1 à 4) et la
   **difficulté** (EASY, NORMAL, HARD : le réglage DIFFICULTY du jeu, LEVEL 2, 4 ou 7 sur 8) ;
   Start ou le bouton 1 lance la partie, le bouton 2 referme le menu. Avec 1 joueur, c'est la
   partie contre l'ordinateur du jeu (la manette 1 joue) ; avec 2, 3 ou 4, la partie « joueur
   contre joueur » où les manches s'enchaînent, les joueurs 3 et 4 inscrits selon le nombre
   choisi, et les menus du jeu (GAME SELECT, GAME TYPE SELECT) validés tout seuls jusqu'à la
   première manche, derrière un écran de chargement (le mode choisi, GET READY) ; pendant ce temps
   les manettes ne vont pas au jeu, le lancement se passe toujours de la même façon. Sur la MiSTer, le numéro d'une manette dépend de l'ordre où l'on a appuyé
   sur ses boutons.
   Le menu propose aussi **MODE : ARENA** (première ligne) : un mode à nous, écrit en C
   (`arena.h`) avec les graphismes du jeu. Les joueurs jouent sur **un seul grand plateau** ;
   chacun tire sa couleur (joueur 1 vert, 2 bleu, 3 rouge, 4 jaune) et ne fait éclater que les
   groupes de 3 de sa couleur ; une bulle qui touche une autre couleur s'y accroche ; les bulles
   en vol rebondissent sur les murs et entre elles ; les bulles décrochées tombent (points au
   joueur qui les a fait tomber) ; le plafond descend toutes les 20 s ; **2 minutes, le meilleur
   score gagne** (READY... GO! au départ, le classement des joueurs à la fin). Gauche / droite visent (l'aiguille du lanceur suit), le bouton 1 tire ; la ligne GUIDE du
   menu règle l'aide à la visée : OFF (l'aiguille seule, par défaut), SHORT (quelques points dans
   la direction), FULL (le trajet complet, avec ses rebonds, jusqu'aux bulles, comme le guide du
   mode PRACTICE du jeu). Le décor du stage en fond, en un seul
   paysage (celui de l'écran du jeu sans les cadres des aires : le ciel et ses nuages qui
   défilent, le paysage, animés par le jeu qui continue de tourner sans ses aires), les lanceurs et dragons du jeu (Bub pour le joueur 1, Bob en bleu, rouge et jaune), ses bruitages
   (tir, accrochage, éclatement, chute, plafond, dernières secondes, fanfare) et la musique du
   stage. Le jeu lance une manche à deux (pour
   charger ses graphismes), puis l'arène prend la main ; à la fin, la console redémarre.
2. **Un joueur 3 ou 4 peut encore arriver en cours de partie avec son Start** : son aire apparaît
   au début de la manche suivante (P3 READY / P4 READY en bas de l'écran pendant une partie à
   deux, READY dans son panneau à l'écran à quatre). Les inscriptions valent jusqu'au retour à
   la démonstration.
3. Chacun vise avec sa croix et tire avec le bouton 1.

**Deux dispositions**, au DIP switch 1 (menu OSD de la MiSTer, ou les DIP du simulateur), même en
pleine partie :
- **allumé (par défaut), 4 × 1** (pour un écran 16:9 ; bouton « 16:9 » du simulateur) ; à trois
  joueurs, trois colonnes centrées (une quatrième apparaît dès qu'un joueur 4 rejoint) : les quatre aires côte à côte, 22 % plus grandes, l'image
  étirée en 16:9 par la MiSTer (rapport d'aspect plein écran dans l'OSD, ou `MiSTer.ini`) ; le jeu
  compense l'étirement (10/16 en largeur, 14/16 en hauteur), les bulles restent rondes. Noms et
  scores au-dessus des aires, OUT / WINNER et manches gagnées en dessous. Chaque ligne traverse
  alors les quatre aires : les bulles y sont dessinées sur 12 lignes (au lieu de 14) pour que
  leurs rangées ne se chevauchent pas, et le matériel reste sous ses 64 sprites par ligne, sauf
  quelques images de l'explosion de fin de manche.
- **éteint, 2 × 2** (pour un écran 4:3) : décrit ci-dessous.

**L'écran à quatre en 2 × 2** (384 × 288) : les aires en 2 × 2, au centre (joueur 1 en haut à gauche, 2 en
haut à droite, 3 en bas à gauche, 4 en bas à droite), à 11/16 de leur taille ; à gauche et à
droite, un panneau par joueur : son nom, son score, puis OUT quand il a perdu et WINNER! pour le
vainqueur de la manche. Les dragons ont la couleur de leur joueur : vert, bleu, rouge, jaune.

**Les règles à quatre** :
- **attaques partagées** : les bulles qu'un joueur fait tomber attaquent tous les adversaires encore
  en jeu, la force divisée entre eux ;
- **le dernier joueur restant gagne la manche** : un joueur dont les bulles atteignent la ligne est
  éliminé (OUT), les autres continuent ;
- les manches s'enchaînent, les scores s'additionnent ; le match « joueur 1 contre joueur 2 » du
  jeu ne compte pas les manches à quatre : chaque panneau affiche le nombre de manches gagnées
  par son joueur (WINS), remis à zéro au retour à la démonstration.

À deux joueurs, tout est comme dans le jeu d'origine (vérifié : la RAM du jeu est la même, image
par image).

**Dans le simulateur** (`dist/monocore.html`), au clavier : joueur 1 flèches, Z X C, Start 1 ;
joueur 2 J I K L, N M virgule, Start 2 ; joueur 3 E R F G, Q W V, Start 3 ; joueur 4 pavé
numérique 4 8 5 6, pavé 0, Start 4 (ou pavé +) ; ou jusqu'à 4 manettes. Les lettres sont celles
marquées sur le clavier (QWERTY ou AZERTY).

## Construire

Comme pour le portage d'origine (plus bas), depuis ce dossier, avec en plus l'assembleur 68000 des
binutils GNU (`sudo apt install binutils-m68k-linux-gnu`) : `prepare.py` applique au programme les
correctifs de `patches/*.s`. La ROM produite s'appelle `build/pbobble4.mnc`.

Des routines du jeu réécrites en C (plus rapides) sont dans `private/`, **hors du dépôt** (c'est le
code du jeu, réécrit) : sans ce dossier, la construction utilise le code traduit et tout marche
de même, un peu moins vite. Voir `DESIGN_4P.md` pour tout le détail de la version 4 joueurs.

---

# Le portage d'origine (Puzzle Bobble 2 / Bust-A-Move Again, Neo Geo, Taito 1995)

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
python3 ../../sdk/build.py .                # build/pbobble2.mnc (25,8 Mo)
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
musique d'AUBE : menus (`19 1A`), carte (`19 1C`, et `24` pour la carte des chemins), partie contre
l'ordinateur (`2E`, et `19 16` à deux joueurs), Puzzle Game (`19 10`), engrenages (`19 13` et `23`
au 2e monde du Puzzle Game, `19 15` à deux joueurs), game over (`21`). Mono, 16 bits, 22 050 Hz,
11,2 Mo ; chaque morceau est rendu deux fois
et la seconde passe est gardée, pour que sa fin rejoigne son début (réverbération comprise) et
qu'il boucle dans le matériel sans couture.

**Bruitages originaux.** Les bruitages du jeu sont remplacés aussi, par des sons faits avec les
mêmes instruments (`tools/sound/make_sfx.py`, 1 Mo) : chaque événement du pilote reçoit un son
choisi d'après ce que fait le jeu quand il l'envoie (relevé contre la manette et l'écran dans les
séances) : `1A 42` rotation du lanceur (clic), `1A 21` tir, `1A 24` bulle qui touche ou s'accroche,
`1A 22` éclatement, `1A 25`/`2A` chute de bulles, `1A 26`/`28`/`29` gros combo, `1A 2B`/`2C`
validation, `1A 27` curseur, `1A 2E`/`2F` début de manche et READY GO, `1A 30` fin de manche,
`19 21` et `19 23` manche gagnée, `02` animation NEOGEO, et, trouvés dans de longues parties
scriptées (`tools/play_long_*.txt`) : `1A 1A` déplacement sur la carte des chemins, `1A 4E` bulles
qui descendent, `1A 55` grande chute de bulles, `1A 56`/`57` attaque envoyée à l'adversaire. La
table a le même format qu'avant.

**Tous les sons possibles.** Le jeu demande ses sons par un numéro logique (`move.w #n, -18014(a5)`
puis `jsr $2ECE`), que `$2ECE` traduit par une table de 256 mots en `$2CCE` (et coupe pendant la démo
selon un réglage du BIOS) avant de ranger les deux octets dans la file d'envoi (`$2F04`). Le
programme utilise 80 numéros, soit 72 commandes différentes ; une trentaine, sans doute des voix et
réactions des personnages des manches avancées et de l'histoire, n'a encore été rencontrée dans
aucune partie : quand le jeu en demande une, la console de débogage affiche `SOUND xx yy missing`.

```sh
python3 tools/sound/make_music.py        # music.bin, music_tab.bin
python3 tools/sound/make_sfx.py          # sounds.bin, sounds_tab.bin
```

Les outils qui rendaient les sons d'origine avec le vrai pilote Z80 et un YM2610 restent dans
`tools/sound/` (`build.sh`, `render.cpp`, `make_sounds.py`, `cuts.py`, `period.py`) : ils ont servi à
comprendre le pilote et ne sont plus nécessaires pour construire la ROM.

- **Protocole** (désassemblage du pilote) : le gestionnaire de NMI range chaque octet dans une file
  de 64 entrées ; la boucle principale les lit un par un. `0A`, `0E`, `14`, `15` et `18`–`1E` lisent
  un octet de paramètre : `1A 42` joue le son `42` dans la case de `1A` (le tir) ; `15` en lit deux
  (`15 1A 42` : le même son pour le second joueur, traité comme `1A 42`). Une table du pilote
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
- **Taille** : la ROM fait 25,8 Mo (carte SD).

**À la fin d'une arène** : le plateau éclate en vague, le panneau des résultats s'affiche avec le
vainqueur en grand ; START rejoue aussitôt une partie identique (mêmes joueurs, réglages et stage),
BUTTON 2 (ou 20 secondes sans réponse) revient au menu, ouvert directement avec les derniers
réglages : seul le jeu redémarre, sans son animation de démarrage, pas la console.

**Les sauvegardes** : les réglages du menu sont retrouvés au démarrage, et le record de chaque
durée d'arène (le meilleur score d'un joueur humain) s'affiche dans le menu et à la fin des
parties (NEW! quand il est battu). Ils sont dans la NVRAM de la MONOCORE (offset 0, 44 octets,
avec un identifiant, une version et une somme de contrôle) : le simulateur la conserve ; sur la
MiSTer elle survit aux resets, et à l'extinction dès que le core l'écrira sur la carte SD.

**Le menu** : des bulles du jeu montent autour du panneau et éclatent en haut ; le panneau a la
hauteur des lignes du mode choisi.
