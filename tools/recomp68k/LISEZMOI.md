# recomp68k : traducteur statique 68000 → C → RV32IM

Traduit du code machine 68000 en C, que clang compile ensuite pour la MONOCORE-1. Rien n'est émulé :
chaque instruction devient du C, comme avec `examples/aigle/tools/recomp6809.py` pour le 6809.

```sh
python3 tools/recomp68k/recomp68k.py image.bin 0 code.c --map codemap.json --vectors [ENTRÉE ...]
```

| Fichier | Rôle |
|---|---|
| `m68k.py` | décodeur 68000 (toutes les instructions, tous les modes d'adressage) |
| `recomp68k.py` | découverte du code depuis les points d'entrée, puis génération du C |
| `m68k_rt.h` | interface avec le portage : `m68k_t`, `m68k_run()`, `m68k_external()`, `m68k_stop()` |
| `test/` | banc de test différentiel contre Musashi |

## Code produit

- Une fonction C par tranche de 4 Ko d'adresses 68000 (`--chunk`). Les registres y sont des
  variables locales, les branchements des `goto`. Les retours, les sauts indirects et les sauts vers
  une autre tranche passent par un aiguillage sur l'adresse 68000.
- La pile 68000 reste dans la mémoire émulée : `jsr` empile l'adresse de retour 68000 et `rts` la
  dépile. Un code qui change de pile (noyau de tâches) fonctionne donc tel quel.
- Les drapeaux ne sont calculés que si une instruction suivante peut les lire (vivacité sur le graphe
  de contrôle ; inconnu après `rts`, un saut indirect ou une exception).
- `M68K_TICK(c, pc)` (vide par défaut) est appelé sur les branchements arrière et sur l'aiguillage : un banc de
  test PC s'en sert pour cadencer les images de façon déterministe.
- Interruptions : prises sur les branchements arrière, après une écriture de SR et sur `STOP`, quand
  `c->ipl` (posé par le portage) dépasse le masque. Exceptions : `trap`, `trapv`, `chk`, division par
  zéro, instructions illégales et lignes A et F.
- Une adresse sans code traduit appelle `m68k_external()` : le portage y remplace le BIOS, ou
  signale une adresse atteinte pendant le jeu, à ajouter aux points d'entrée avant de retraduire.
- Le portage fournit `m68k_port.h`, avec les accès mémoire `RD8/RD16/RD32/WR8/WR16/WR32` (adresses
  de 24 bits, accès mot aux adresses paires).
- `--alias SRC:DST:LEN` traduit du code que le programme recopie en RAM avant de l'exécuter.

## Validation

```sh
python3 tools/recomp68k/test/fuzz.py 4000 1 10     # 10 séries de 4000 tests
```

Le banc génère des séquences aléatoires (instructions de tout type, branchements, boucles `dbcc`,
`bsr`/`rts`, interruptions des niveaux 1 à 6, écritures de SR avec passage en mode utilisateur,
`rte`), les traduit, puis les exécute sur Musashi (licence MIT, récupéré dans `test/build/`, jamais
versionné) et en code traduit à partir du même état. Il compare les registres, SR, la pile inactive,
l'adresse d'arrivée et 64 Ko de RAM. Résultat actuel : 0 différence sur plus de 45 000 tests.

Écarts connus de Musashi, exclus du tirage : Musashi décode les lignes A et F comme des instructions
FPU même en 68000, et prend la NMI (niveau 7) dès le début de l'exécution. Pour `DIVS` de
`$80000000` par −1, le traducteur suit le vrai 68000 (débordement) et non Musashi.
