# Version 4 joueurs : conception

Ce document suit la transformation de Puzzle Bobble 2 en jeu à 4 joueurs. Il ne contient que des
adresses et des observations ; le rapport détaillé (`tools/map4p.py` → `build/check/map4p.txt`)
cite le code du jeu et reste hors du dépôt.

## Règles choisies

- 4 joueurs, chacun sur sa manette (`mc_pad` 1 à 4), chacun dans son aire.
- **Une attaque est partagée entre tous les adversaires encore en jeu** : les bulles qu'un joueur
  envoie sont réparties entre les autres.
- Écran : 4 aires réduites côte à côte (bulles d'environ 12 pixels grâce au zoom des sprites),
  cadres et décor dessinés pour cet écran (les plans de la MONOCORE ne se réduisent pas).

## Méthode

Le jeu est modifié **en 68000**, avant la traduction : de petites routines écrites en assembleur
(`m68k-linux-gnu-as`, paquet `binutils-m68k-linux-gnu`) placées dans la zone libre de la ROM du jeu
(les 128 derniers Ko du programme sont vides), et une liste de correctifs appliquée par
`prepare.py`. Seuls les correctifs entrent dans le dépôt ; le jeu modifié est ensuite traduit,
découvert et vérifié comme le portage à 2 joueurs.

## Ce que fait le jeu pour 2 joueurs (étude de faisabilité)

- **L'aire d'un joueur est une tâche** du noyau multitâche : la même routine (`$74F16`) est
  lancée deux fois (`$22B50`–`$22B84`), comme tâche n° 5 (joueur 1) et n° 6 (joueur 2, seulement
  si l'indicateur « deux joueurs » `-10200(a5)` vaut 1). `$10D5CE` tient un drapeau « actif » par
  joueur.
- **Chaque aire a sa structure de 150 octets** (`$96`) : `$10D5D2` pour le joueur 1, `$10D668`
  pour le joueur 2. La routine la choisit d'après le numéro de la tâche en cours (`-19260(a5)`), à
  trois endroits (`$74D2A`, `$74EE8`, `$74F1E`) ; tout le reste du code de l'aire passe ensuite par
  le registre `a4`. L'angle du lanceur est un mot à `+8`.
- **Les autres données par joueur** sont rangées avec un pas fixe (objets du lanceur à `$100248` /
  `$10041C`, deux mots à `$100FF4` / `$100FF8`, un octet à `$10263D` / `$10243D`) et ne sont jamais
  adressées de façon fixe dans le code : elles passent par des registres.
- **Les manettes** des joueurs 1 et 2 sont recopiées dans des variables du jeu (`-28684(a5)`,
  `-28680(a5)`) par une seule routine (`$246C`).
- **L'indicateur « deux joueurs »** est posé au choix du mode (`$2740E`–`$2748A`) et testé à 19
  endroits du code : départ, fin de manche, affichage, envoi des attaques. Ce sont les points à
  étudier pour la logique à 4.
- En plus de sa structure, chaque joueur a un tampon de 2 Ko (pointé en `126(a4)` : `$10D6FE`,
  `$10DEFE`), initialisé en `$74D2A` ; il faudra deux structures et deux tampons de plus (4,3 Ko).
- La RAM ne sert qu'à 17 % (11 Ko sur 64 changent pendant une partie) : il y a de la place pour deux
  structures de plus.

Ces résultats viennent de parties du banc PC (`pc/bench.c`, actions `ramdump=` et
`BENCH_FOOTPRINT=1`) qui ne diffèrent que par l'appui d'un joueur : le banc est déterministe, tout
octet qui diffère dépend de cet appui.

## Inventaire pour l'étape 3

- **Manettes.** Le jeu tient un bloc de 4 octets par manette (`-28688(a5)` Start/Select,
  `-28684(a5)` joueur 1, `-28680(a5)` joueur 2 : état courant inversé, précédent, fronts), rempli
  par `$246C`/`$24BE` depuis les variables du BIOS. La lecture de manette de l'aire (`$62264`) passe
  par trois tables à **4 entrées** (`$62496`, `$624A6`, `$624B6`) : le moteur de Taito prévoit 4
  joueurs, mais les entrées 3 et 4 pointent vers `$100FFC` / `$101000`, que le jeu utilise pour
  autre chose. Correctif : rediriger ces entrées vers de la RAM libre, et remplir les blocs 3 et 4
  depuis les manettes 3 et 4 de la MONOCORE (dans `$246C`).
- **RAM libre.** Attention : « ne change jamais pendant une partie » ne suffit pas.
  `$10598C`–`$108F98` est la **table des objets** du jeu (320 entrées de 44 octets depuis
  `-10088(a5)`, presque vides), et `$10B140`–`$10D4D4` la fin d'un tableau de 16 Ko commençant en
  `$1094D4` (256 × 64 octets) : écrire dans la table des objets crée un faux objet que le jeu
  exécute. La RAM retenue n'est ni désignée par le code (aucun `d16(a5)`, adresse absolue ni base
  de tableau) ni modifiée pendant les parties : la **RAM de travail du BIOS d'origine**
  (`$10F300`–`$10FBFF`, que le portage n'utilise pas ; la pile du jeu descend depuis `$10F300`) pour
  les structures, blocs de manette, manettes brutes, scores et compteurs des joueurs 3 et 4
  (`$10F400`–`$10F62B`), et `$1041D6`–`$105476` pour leurs deux tampons (`$104200`, `$104A00`).
- **Le numéro de joueur `2(a4)` est lu à 79 endroits** :
  - une dizaine de **tables indexées à 2 entrées** (`$4337A`, `$44D38`, `$459D0`, `$5FD28`, `$5FD76`,
    `$620CA`, `$485E4`, `$62B24`, `$74E16`) : à étendre à 4 entrées, sinon les joueurs 3 et 4
    liraient hors de la table ;
  - environ 65 **aiguillages à deux voies** (`tst.w 2(a4)` puis `bne`/`beq`) : position à l'écran
    (`#$20` pour l'aire gauche, `#$B0` pour la droite), orientation, palettes. Les joueurs 3 et 4
    prendraient la voie du joueur 2 et seraient dessinés sur son aire.
- **Le drapeau en `(a4)`** n'est pas « actif » : 0 = aire jouée par une personne, 1 = par
  l'ordinateur ; les chemins de création le posent (`21966(a5)`, `21968(a5)`) avant de lancer les
  tâches. Les aires sont créées à plusieurs endroits, un par mode (`$22B48`, `$22B94`, `$22BD4` pour
  deux joueurs, `$22C68`…) ; `-10200(a5)` veut dire « deux aires », y compris contre l'ordinateur.
- **Le cœur de l'attaque** : `$620D4` ajoute les bulles envoyées à `56(a0)` de la structure adverse,
  prise dans une table indexée par le joueur (`$620FC`) ; `$459DC` lit le champ `+$14` de
  l'adversaire (`$45A0E`).
- **Initialisation.** `$74D2A` initialise les deux structures (drapeau personne/ordinateur, numéro, pointeur de
  tampon en `126(a4)`, puis `$74D68`) ; `$74EE8` écrit une valeur dans `98(a4)` des deux ; `$22B50`
  crée les tâches 5 et 6.

## Étapes

1. Cartographie (faite pour l'essentiel, `tools/map4p.py`).
2. Outillage des correctifs 68000 : fait. `patches/*.s` (assembleur GNU) : une section par adresse
   (`.p_074F1E`, `.p_0F0000`…), une ligne `| expect ADRESSE LONGUEUR crc32 0x…` par site modifié
   (les octets d'origine sont vérifiés avant d'écrire). `tools/patch68k.py` assemble, lie chaque
   section à son adresse (les étiquettes et adresses entre correctifs fonctionnent), écrit dans le
   programme et vérifie que la zone libre (`0x0E8000`–`0x0FFFFF`, remplie de `0xE5`) l'est encore ;
   `prepare.py` l'appelle. Premier correctif (`patches/field.s`) : la structure du joueur est
   calculée d'après le numéro de la tâche (table indexée par `a4`, sans registre de données ni
   pile), au lieu de « tâche 5 : joueur 1, sinon joueur 2 ». Vérifié au bit près : la RAM est
   identique, image par image, à celle du portage à 2 joueurs sur 2 401 images, à un joueur contre
   l'ordinateur comme à deux joueurs.
3. a. 4 aires en mémoire : **fait** (`patches/four.s`). Quand les joueurs 1 et 2 sont deux
      personnes, l'aire du joueur 2 crée en démarrant les tâches 7 et 8 ; les structures et
      tampons 3 et 4 sont initialisés avec ceux de 1 et 2 ; les manettes 3 et 4 de la MONOCORE
      (`main.c` → `neort.c`, octets `$10F610`/`$10F611`) remplissent leurs blocs comme `$246C` ;
      les six tables indexées par le joueur ont 4 entrées ; les joueurs 3 et 4 s'affrontent pour
      l'instant (tables d'adversaire). Vérifié sur le banc : 6 000 images à quatre joueurs au
      hasard sans erreur, les 4 tâches d'aire tournent en manche, chaque lanceur suit sa manette
      et chaque grille se remplit ; contre l'ordinateur, la RAM est identique à celle du portage
      à 2 joueurs hors de la RAM des joueurs 3 et 4 (les tâches 7 et 8 n'y sont pas créées). Les
      joueurs 3 et 4 sont encore dessinés sur l'aire du joueur 2, et les manches ne connaissent
      que les joueurs 1 et 2 (étape 5).
   b. La bande invisible : **fait** (`patches/band.s`). Chaque tâche ajoute ses sprites à la
      liste de l'image (`$1001CE`, pointeur d'écriture `-29066(a5)`, 106 routines le font), que la
      routine d'image (`$D36`) envoie dans la VRAM ; le noyau fait tourner les tâches l'une après
      l'autre, donc les entrées d'une tâche se trouvent entre le pointeur quand la tâche
      précédente rend la main et celui où elle la rend. L'ordonnanceur (`$2A54`, où aboutissent
      tous les appels système) les note pour les tâches 7 et 8 ; avant l'envoi de la liste, leurs
      entrées sont déplacées : joueur 3 de 256 lignes vers le bas, joueur 4 de 256 lignes et 256
      pixels (bit 15 des mots de position verticale `+6` et horizontale `+8` d'une entrée de
      12 octets). Les joueurs 3 et 4 dessinent tous deux du côté du joueur 2 ; dans la bande,
      leurs plages horizontales ne se recouvrent pas. Mesuré dans la VRAM : 42 sprites du joueur
      3 et 41 du joueur 4 dans la bande, l'écran Neo Geo ne montre plus que les joueurs 1 et 2 ;
      contre l'ordinateur, la RAM reste identique au portage à 2 joueurs (le crochet garde ses
      registres hors de la pile des tâches). Une seconde liste (`$100DFE`, `-29062(a5)`) sert aux
      grands objets ; à traiter si des sprites des joueurs 3 et 4 y passent.
      Outils de mesure ajoutés au banc (compilés à la demande) : `-DNEO_VRAM_OWNER` (tâche qui
      écrit chaque sprite), `-DNEO_RAM_OWNER` (tâches qui écrivent chaque mot de RAM, depuis
      `OWNER_FROM`), `-DNEO_READ_TRACE` (dernière adresse lue avant chaque position de sprite).
4. L'écran à 4 aires dans `main.c` : **fait**, en **2 × 2 accolées** (384 × 288 : joueur 1 en haut à
   gauche, 2 en haut à droite, 3 en bas à gauche, 4 en bas à droite ; colonnes à 72 et 192, rangées
   à 0 et 144), à l'échelle **11/16** (bulles de 11 pixels) : on montre les lignes 16 à 223 de l'aire
   Neo Geo, tout sauf son en-tête (à 3/4, les lignes 20 à 211 coupaient le bas du lanceur et le
   dragon) ; 152 × 208 pixels donnent 104 × 143. **Dragons** : le joueur 1 a Bub (palette 16), les
   joueurs 2, 3 et 4 Bob (palette 21, qui ne sert qu'au dragon, mesuré) ; les tuiles en palette 21
   des aires 3 et 4 prennent les palettes 212 et 213, copies de la 21 faites à chaque image où
   seules les couleurs bleues prennent la teinte du joueur : rouge, jaune. Les panneaux ont les
   couleurs des dragons (vert, bleu, rouge, jaune). Noms, scores, WINNER ou OUT dans deux panneaux, dans
   les marges de 72 pixels : joueurs 1 et 3 à gauche, 2 et 4 à droite, chacun en face de son aire
   (écrits d'abord en haut des aires, ils cachaient les premières bulles). Enlever une rangée de bulles ne libérerait de la place qu'en
   remontant aussi le lanceur (modification profonde du jeu). D'abord quatre aires côte à côte en
   384 × 224 : chaque ligne traversait alors quatre aires, et les lignes chargées dépassaient la
   limite de 64 sprites ; en 2 × 2, une ligne n'en traverse que deux, à la même taille de bulles.
   Détails : Quand les tâches 7 et 8 sont les aires des joueurs 3
   et 4 (octets `FIELD7` / `FIELD8`, posés au démarrage de l'aire, effacés quand la tâche se
   termine : le jeu se sert de ces numéros de tâche pour autre chose sur d'autres écrans), l'écran
   passe à 384 pixels :
   - **aires** : chaque tuile de sprite de la liste de l'image est rattachée à son aire d'après sa
     position (écran Neo Geo gauche/droite, bande), recadrée sur les 152 pixels utiles d'une
     moitié d'écran, réduite à 5/8 (une tuile de 16 pixels fait exactement 10 pixels : pas de
     jointure) et placée dans sa colonne de 96 pixels ;
   - **décor** : les plans ne se réduisent pas ; les sprites fixes (en dessous du sprite 118, où
     commence la liste de l'image) de l'aire gauche sont composés à 5/8 dans 12 × 18 cellules de
     8 × 8 en 8 bpp en fin de CHR RAM, que le plan 1 montre derrière chacune des quatre aires.
     (D'abord quatre sprites : derniers de la table, ils étaient les premiers ignorés sur les
     lignes de plus de 64 sprites, et le décor disparaissait quand les aires se remplissaient.)
     Le décor n'est recomposé que lorsqu'un autre décor arrive (empreinte : palettes, hauteurs et
     réductions de ses sprites, sans leurs positions ni leurs tuiles : la ville défile et s'anime,
     et la recomposer à chaque pas coûtait 2,8 millions de cycles toutes les 12 images), et un
     décor couvrant moins d'un quart de ses lignes témoins ne remplace pas celui affiché : à la fin
     d'une manche des joueurs 1 et 2, le jeu efface ou déplace son décor alors que les aires 3 et 4
     vivent encore, et le fond devenait noir. Ses pixels désignent une fenêtre de palettes (240–254) où sont recopiées à chaque
     image les palettes du décor (9 au plus mesurées) : couleurs exactes et fondus du jeu. Une
     empreinte des sprites fixes détecte un changement de décor ; la composition prend environ
     2,8 millions de cycles, une fois par décor ;
   - **scores** : sur le plan 0, avec la police du plan fixe du jeu (codes ASCII, tuile vide
     `$FF`), « PLAYER n » et le score (entier 32 bits) de chacun dans sa couleur (palettes
     208–211). Le jeu n'utilise jamais les palettes 208–254 (mesuré) ; ses écritures y sont
     ignorées pendant l'écran à quatre et restaurées ensuite, avec son plan fixe.
   Vérifié dans le simulateur (`tools/play_4p.txt`) : les quatre aires, leur décor et leurs scores,
   un changement de décor entre deux manches, le retour à l'écran à deux joueurs.
   **Vitesse.** Sur la MiSTer, la partie à quatre ralentissait : mesuré dans le simulateur (`main.c`
   donne toutes les 10 s la charge et le nombre d'images à l'heure), le jeu ne tenait que 300 images
   sur 600 (30 images/s), le travail d'une image dépassant un peu le budget (833 000 cycles à
   50 MHz). Profil réel (`tools/prof.js` échantillonne le compteur ordinal du simulateur après
   chaque ligne, `tools/prof.py` rattache les adresses aux lignes du code traduit — construction
   avec `-gline-tables-only`, puis aux routines du 68000) : l'envoi de la liste de sprites à la
   VRAM par l'interruption d'image (`$D36`–`$E18`, mot par mot à travers les registres LSPC
   émulés) prenait 11 % du temps. Il est maintenant fait en C (`neort.c`, `native_sprites`,
   appelé par `patches/sprites.s` à l'adresse `$C00F00` que le runtime traite comme le BIOS) :
   VRAM et registres LSPC identiques à l'original à chaque image (2 600 images à quatre et contre
   l'ordinateur, `BENCH_VHASH=1`). Résultat : 562 à 575 images sur 600 à quatre (environ 57
   images/s), charge 84–87 %.
5. Les règles à 4 — **fin de manche : faite** (`patches/rules.s`). Le joueur dont les bulles
   atteignent la ligne (état 12, `$62048`–`$620AC`) mène la fin de manche : il gèle son adversaire
   (`108 = 6`, état 0), joue son LOST (13, 14), puis donne l'état 10 à l'adversaire (WON : 10, 11)
   et se met à 0 ; le gagnant, à la fin de son WON, prend le numéro de l'adversaire pour le
   résultat et termine la manche (`$22160`). L'adversaire venait de deux tables à 2 entrées
   (`$61E68`, `$62252`) que les joueurs 3 et 4 lisaient au-delà (écritures au hasard : trouvées en
   cherchant dans le code traduit, et non plus par décodage linéaire, toutes les tables lues avec
   le numéro de joueur). Maintenant : un joueur qui perd est éliminé (`OUT`, remis à zéro à chaque
   manche) ; le gel et le WON vont au dernier joueur restant, seulement quand il n'en reste qu'un ;
   à deux joueurs, c'est le jeu d'origine (RAM identique contre l'ordinateur). Vérifié sur le banc
   (`-DNEO_WATCH`, `WATCH=adresses` : chaque écriture d'un mot surveillé, avec la tâche) et dans le
   simulateur : les joueurs perdent l'un après l'autre, les autres continuent, le dernier gagne.
   **Bilan de manche** : il ne connaît que deux côtés (le vainqueur en `-10290(a5)`, le perdant en
   `-10288(a5)`, les manches gagnées dans des tables à 2 entrées comme `-10236(a5)`), et ses
   bandeaux WON/LOST sont des lettres-objets (`$32570` → `$32682`, `$327AE`) qui relisent le côté
   à chaque image. À quatre, il reçoit un résultat à deux joueurs valide (l'autre côté du
   vainqueur) et ne dessine pas ses bandeaux ; l'écran de `main.c` écrit WINNER et OUT sous le
   score de chaque joueur. Reste le texte « PASSAGE TIME » du bilan, sur les aires 1 et 2.
   **Le match** : le jeu compte les manches gagnées du joueur 1 contre le joueur 2 (`$22188` :
   compteurs à 2 entrées `-10244(a5)`, `-10252(a5)`) et annonce PLAYER n WON à la fin du match ;
   avec le résultat à deux joueurs donné au bilan, c'était un faux vainqueur (PLAYER 2 quand le
   joueur 3 gagne). Dans une manche à quatre (`FOURROUND`), les manches gagnées ne comptent pas :
   les manches s'enchaînent, les panneaux disent qui gagne chacune. À deux joueurs, la RAM est
   identique au portage d'origine (partie à deux personnes, quatre images sur 5 600).
   **Rejoindre avec Start** : fait. Les Start des manettes 3 et 4 arrivent au jeu (`$10F612`,
   `neort.c`) ; la routine des manettes note qui a rejoint (`JOIN3`, `JOIN4`) ; au démarrage de
   l'aire du joueur 2 d'une partie de deux personnes, seuls les joueurs inscrits ont une aire (on
   joue à deux, trois ou quatre) ; une inscription vaut pour la manche suivante et jusqu'au retour
   à la démonstration (les deux premières aires jouées par l'ordinateur), pas une manche contre
   l'ordinateur (le joueur 2 peut arriver ensuite en challenger). Un joueur absent a le décor seul
   et PRESS START dans son panneau.
   Les règles à 4 restantes : choix du mode, attaques partagées, fin de manche et de partie, menus ; et ce
   que l'écran à quatre montre encore : les joueurs 3 et 4 ont l'apparence du joueur 2 (côté
   droit), les en-têtes de score du jeu (sprites) restent en haut des aires 1 et 2, les scores des
   joueurs 3 et 4 sont à vérifier (d'autres chemins de score peuvent viser les adresses des
   joueurs 1 et 2), la limite de 64 sprites par ligne est à surveiller (`SPR_PEAK`).

## Routines réécrites en C

Le jeu est traduit mécaniquement (`build/tr/pb2_code.c`). Les routines les plus coûteuses ou les
plus utiles aux règles à quatre sont peu à peu réécrites à la main en C : c'est le code du jeu,
réécrit, et il reste **hors du dépôt**, dans `examples/pbobble4/private/` (exclu par `.gitignore`) :
`private/native.c` (les routines) et `private/native.s` (les correctifs 68000 qui sautent vers une
adresse que le runtime confie à `neo_native`). `native.c` l'inclut s'il est là
(`__has_include`) ; `tools/patch68k.py` applique aussi `private/*.s`. Sans ce dossier, la
construction utilise le code traduit, et tout fonctionne de même. Chaque routine réécrite est
vérifiée sur le banc : RAM et VRAM identiques au code traduit, image par image
(`BENCH_HASH=1 BENCH_VHASH=1`), à quatre et contre l'ordinateur.

Fait (charge pendant une manche à quatre : 86 % → 78 %) :
- le dessin des objets d'une aire dans la liste de sprites (`$4468A`, pour chaque bulle à chaque
  image) : 86 % → 81 % ;
- le chemin rapide du moteur d'animation (`$6FDFC` : état partagé des bulles en versus, décompte du
  minuteur ; quand il expire, le code traduit reprend le script en `$6FE46` ou `$6FE34`, ajoutés à
  `entries.txt`, car un script peut appeler n'importe quelle routine du jeu) et la fin de
  l'animation d'une bulle (`$6EFD2`, sa palette) : 81 % → 78 %.
Chacune : RAM et VRAM identiques au code traduit sur 3 000 images, à quatre et contre l'ordinateur.

Puis : la table des 320 objets (`$2361C`, parcourue entière à chaque image, presque vide) saute
ses entrées vides en C et rend la main au code traduit pour chaque objet (`$23622`) ou en fin de
table (`$23636`), avec les drapeaux du `tst.l` d'origine ; la bande des joueurs 3 et 4 (notre
code) est en C dans `neort.c` (`native_band`) ; les routines natives passent avant l'aiguillage
du BIOS émulé. Charge à quatre 68 % → 66 %, à deux 46 % → 43 %. (Une fausse alerte en route : la
référence de comparaison était un banc compilé avant la dernière correction des inscriptions ; la
référence doit toujours être reconstruite depuis l'état publié.)

**Le traducteur** (public, `tools/recomp68k`) : chaque fonction C du code traduit (une plage de
4 Ko) avait un `switch` d'aiguillage sur une centaine d'adresses éparses (une recherche
dichotomique), par lequel passent tous les retours, les sauts indirects et les appels qui changent
de fonction (1 740 appels à adresse fixe sur 2 300 : des blocs plus grands n'en supprimeraient
qu'une partie, et compileraient bien plus lentement). Il est remplacé par une table de sauts (une
entrée par adresse paire, étiquettes en valeurs `&&L_...`) : un accès et un saut. Charge à quatre
78 % → 68 %, à deux 55 % → 46 % ; RAM et VRAM identiques sur 3 000 images (pbobble4 à quatre et
contre l'ordinateur, pbobble2), banc différentiel contre Musashi sans échec.
