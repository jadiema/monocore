# Puzzle Bobble 2 / Bust-A-Move Again (Neo Geo, Taito 1995) sur MONOCORE-1

Portage en cours, pour un usage personnel, à partir de sa propre copie du jeu (set MAME
`pbobbl2n`). Comme pour L'Aigle d'Or, le jeu est **traduit** : le code 68000 est recompilé en C
puis en RV32IM, le matériel Neo Geo est remplacé par une petite bibliothèque d'exécution écrite pour
la MONOCORE-1. Rien n'est émulé sur la console. Les fichiers tirés du jeu restent hors du dépôt
(`.gitignore`).

## Préparer les données

```sh
pip install numpy pillow capstone
python3 tools/prepare.py pbobbl2n.zip --sheets   # p1.bin, p1_be.bin, spr.bin, spr_map.bin, fix.bin
python3 tools/m68kmap.py                         # carte du code -> build/check/codemap.txt et .json
```

`--sheets` écrit des planches de tuiles dans `build/check/` pour contrôler le décodage.

| Fichier | Contenu |
|---|---|
| `p1.bin` | programme 68000 tel que MAME le range (mots de 16 bits petit-boutistes) : c'est directement la forme mémoire du runtime (accès octet à `adresse ^ 1`, accès long = deux mots) |
| `p1_be.bin` | le même programme gros-boutiste, pour le désassembleur et le traducteur |
| `spr.bin` | tuiles de sprites 16 × 16 au format 4 bpp de la MONOCORE (SPEC §4.3), dédupliquées : 34 246 tuiles sur 81 920, 4,2 Mo ; tuile 0 vide |
| `spr_map.bin` | `uint16` par numéro de tuile Neo Geo → numéro dans `spr.bin` |
| `fix.bin` | 4 096 tuiles 8 × 8 du plan fixe, 4 bpp |

## Ce que dit la carte du code (étape 2)

- Le jeu n'appelle le BIOS qu'en 5 points : `SYSTEM_INT1`, `SYSTEM_RETURN`, `SYSTEM_IO`,
  `CREDIT_CHECK` et `FIX_CLEAR` (`LSP_1ST` et `MESS_OUT` apparaissent dans des zones que l'analyse
  n'a pas encore atteintes). Ils seront réécrits en C (`neort.c`), avec la boucle du BIOS qui appelle
  `USER` selon `BIOS_USER_REQUEST` ; le BIOS d'origine n'est pas nécessaire.
- **Noyau multitâche** : `trap #1` crée une tâche (16 au plus), `trap #5` (42 appels) sauvegarde
  tous les registres, range `a7` dans le bloc de la tâche et passe à la suivante. Chaque tâche a sa
  propre pile 68000. La traduction doit donc garder la pile 68000 dans la mémoire émulée (`jsr`
  empile l'adresse de retour 68000, `rts` la dépile et passe par l'aiguillage), comme le fait déjà
  `recomp6809.py` : le changement de tâche se réduit alors à changer `a7` et fonctionne sans
  modification.
- Le timer (niveau 2) ne fait qu'acquitter l'interruption : pas d'effet raster à prévoir.
- Une routine est copiée en RAM puis exécutée (`D3E90 : jmp $7C00(a5)`, copie jusqu'au `rts`) : le
  traducteur la traduira depuis sa source en ROM.
- L'analyse statique trouve environ 67 Ko de code et 71 tables de sauts. Le reste est atteint par des
  adresses calculées pendant le jeu. Plutôt qu'un émulateur, le code traduit appellera
  `unknown_target(pc)` pour toute adresse absente de la carte : sur le banc PC il l'écrit et
  s'arrête, et on ajoute l'adresse aux points d'entrée avant de retraduire, jusqu'à ce que les
  parties de test n'en trouvent plus.

## Suite

3. Fait : `tools/recomp68k/` (voir son `LISEZMOI.md`), validé contre Musashi. Le programme se traduit
   avec `python3 ../../tools/recomp68k/recomp68k.py p1_be.bin 0 build/tr/pb2_code.c --map
   build/check/codemap.json --vectors` : 18 162 instructions, 50 fonctions C, 488 Ko de RV32IM en -O2,
   sans avertissement. La découverte du reste du code se fera avec le runtime (étape 4).
4. En cours. `neort.c` : carte mémoire Neo Geo, VRAM du LSPC, palettes, entrées, registre son et BIOS
   remplacé (AES Europe : `SYSTEM_INT1`, `SYSTEM_RETURN`, `SYSTEM_IO` avec `PLAYER_START`,
   `CREDIT_CHECK`, `FIX_CLEAR`, `LSP_1ST`). Banc PC `pc/bench.c` : cadence déterministe par
   `M68K_TICK`, rendu de référence de l'écran Neo Geo en PPM, manettes scriptées, signalement des
   adresses non traduites. `python3 tools/discover.py 1800` traduit, compile, exécute et ajoute à
   `entries.txt` (des adresses seulement) chaque adresse atteinte, jusqu'à ce qu'une séance passe.
   Le jeu recopie deux routines en RAM avant de les exécuter (`D3EFE→100000`, `D3E94→10FC00`,
   déclarées dans `discover.py`). Sur 1 800 images, l'animation SNK, le logo Taito, la démo et les
   écrans de fin s'affichent correctement ; avec la partie scriptée de `tools/play.txt`
   (`python3 tools/discover.py 2400 "$(cat tools/play.txt)"`), le choix du mode, de la difficulté et
   le Round 1 contre l'ordinateur se jouent (92 points d'entrée découverts). Reste `main.c`, la couche MONOCORE (ombre de la VRAM en
   CHR RAM, sprites et plan fixe vers plans et sprites, palettes RGB555).
5. Son : commandes `0x320000` vers les voix PCM et PSG, musiques FM pré-rendues hors ligne.
6. Validation : banc PC, simulateur, MiSTer de dev.
