# GRAVITON — casse-tête gravitationnel

Un canon tire un projectile qui doit atteindre la cible violette. On ne peut pas viser
directement : on pose d'abord des **puits** (bleus, ils attirent) et des **repoussoirs**
(orange, ils repoussent), puis on tire. La trajectoire se courbe autour d'eux. Le nombre de puits
et de repoussoirs disponibles est fixé par le niveau (affiché en haut : `+ restants/total` et
`- restants/total`). Le tir est perdu s'il touche un mur blanc, tombe dans un puits, sort de
l'écran ou erre trop longtemps ; les murs cyan le font rebondir. Les zones hachurées sont interdites
aux puits. Les trois derniers tirs restent tracés à l'écran, du plus récent (clair) au plus ancien.

Inspiré du principe des jeux Flash de tir gravitationnel des années 2000 ; nom, niveaux,
graphismes et sons sont originaux.

![GRAVITON : écran titre, ZONE INTERDITE, GRAND FINAL résolu](apercu.png)

## Commandes

| Manette | Clavier | Souris | Action |
|---|---|---|---|
| croix, stick gauche | flèches | déplacement | curseur (accélère si on maintient) |
| B1 | Entrée | bouton gauche | poser un puits ; prendre / lâcher un puits ; saisir le canon (niveaux à angle libre) |
| B2 | Retour arrière | bouton droit | retirer le puits survolé, sinon changer de type (puits / repoussoir) |
| B3 | Espace | bouton du milieu | tirer ; pendant le vol, maintenu : accéléré |
| B4 / B5 | | molette | affiner l'angle du canon (niveaux à angle libre) |
| B2 pendant le vol | | | abandonner le tir |
| START | Échap | | menu (continuer, recommencer, choix du niveau) |

Toutes les manettes sont lues : n'importe quel joueur de la borne peut jouer. La ligne du bas
indique ce que fait B1 à l'endroit du curseur.

## Niveaux

16 niveaux, débloqués l'un après l'autre : PREMIER VIRAGE, LE MUR, REPOUSSOIR, DEMI-TOUR,
BILLARD, ZONE INTERDITE, L'ASTRE, CANON LIBRE, FRONDE, LE COULOIR, CHAMP DE MINES, LE COFFRE,
TROU NOIR, PING-PONG, LE LACET, GRAND FINAL. La progression et le record de chaque niveau (le
moins de tirs) sont gardés en NVRAM (`saves/MONOCORE/graviton.sav` sur MiSTer, à l'ouverture de
l'OSD).

## Fichiers

| Fichier | Contenu |
|---|---|
| `physics.h` | la physique, en entiers seulement (virgule fixe 16.16, 4 sous-pas par image, attraction en 1/r² adoucie sous 6 px, murs, rebonds, règles de pose) ; aucune division 64 bits |
| `sintab.h` | table de sinus (quart de tour, 1024 pas par tour) |
| `levels.h` | les niveaux (canon, angle fixe ou plage, vitesse, cible, puits disponibles, puits fixes, murs, zones interdites) |
| `main.c` | le jeu : deux plans en mode « bitmap » dans la CHR RAM (décor vectoriel, traces des tirs), sprites générés au démarrage, titre en lettres vectorielles, sons PSG |
| `tools/solve.c` | vérificateur de niveaux sur PC : tirages au hasard (mesure de difficulté) puis optimisation locale (preuve qu'une solution existe) |
| `tools/draw.py` | planche PNG des niveaux avec la trajectoire trouvée par `solve` |

`physics.h` est compilé tel quel par le jeu (RV32IM) et par `solve` (PC) : une solution trouvée
sur PC se rejoue à l'identique sur la console.

## Construire et vérifier

```sh
python3 ../../sdk/build.py .                    # -> build/graviton.mnc
cd tools && cc -O2 -o solve solve.c
./solve 0 20000 dump 300                         # niveau 1 : taux de réussite au hasard, une solution
./solve 15 10 dump 150 1 3                       # niveau 16 : graine 1, à partir de 3 puits
python3 draw.py /tmp/levels.png                  # planche des niveaux et des solutions trouvées
```

Arguments de `solve` : niveau (0 = premier, -1 = tous), tirages au hasard, `dump` pour écrire les
trajectoires (`/tmp/lvNN.txt`, lues par `draw.py`), redémarrages de l'optimisation, graine, nombre
minimal de puits essayés. Pour une version de test qui commence à un niveau donné, tous niveaux
ouverts : `cflags = -DSTART_LEVEL=n` dans `project.txt`.
