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
3. 4 aires en mémoire : tâches 7 et 8, structures 3 et 4, manettes 3 et 4 ; affichage provisoire.
4. L'écran à 4 aires dans `main.c`.
5. Les règles à 4 : choix du mode, attaques partagées, fin de partie, menus.
