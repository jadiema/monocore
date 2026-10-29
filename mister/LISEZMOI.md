# MONOCORE-1 pour MiSTer — premier essai sur la carte

Ce dossier est le core MiSTer complet : la console (`../rtl`), son contrôleur SDRAM, le chargement des ROM et la sortie vidéo, dans le framework officiel `Template_MiSTer` (dossier `sys/`, inchangé).

## Ce qu'il faut

- Le DE10-Nano avec MiSTer installé (carte SD à jour) et **une barrette SDRAM MiSTer** (32 Mo ou plus) : la ROM du jeu est chargée dedans.
- Quartus Prime **Lite 17.0** avec le Cyclone V.
- Le dossier `monocore` complet (le projet lit les sources dans `../rtl`).

## Compiler

1. Ouvrir `MONOCORE.qpf` dans Quartus (*File > Open Project*).
2. *Processing > Start Compilation* (15 à 40 minutes).
3. Le fichier produit est `output_files/MONOCORE.rbf`.

Des avertissements par centaines sont normaux (framework MiSTer et largeurs de signaux). Si la compilation s'arrête sur une erreur, renvoyer `output_files/MONOCORE.flow.rpt` et le `.rpt` de l'étape qui a échoué (`.map.rpt` ou `.fit.rpt`). Renvoyer aussi, dans tous les cas, `output_files/MONOCORE.fit.summary` et `output_files/MONOCORE.sta.summary` (occupation et timing).

## Installer sur la carte SD

1. Copier `MONOCORE.rbf` dans le dossier `_Console` (ou `_Other`) de la carte SD, sous le nom `MONOCORE_20261017.rbf` par exemple : MiSTer l'affichera dans ce menu.
2. Créer le dossier `games/MONOCORE` et y copier les ROM `.mnc` (la ROM de test : `rom/build/testrom.mnc`).
3. Optionnel : copier aussi `testrom.mnc` sous le nom **`boot.rom`** dans `games/MONOCORE` ; elle se chargera toute seule au lancement du core.

## Utiliser

- Lancer le core, puis menu OSD (touche F12 ou bouton OSD) : **Charger une ROM** et choisir un `.mnc`. La console démarre dès la fin du chargement.
- Manettes : directions, boutons 1 à 6, Start, Pièce (à assigner une fois dans le menu « Define joystick buttons » du core).
- **Reset** dans le menu redémarre le jeu (la ROM reste en mémoire).

## Cartouche (port USER)

Une cartouche est une ou deux Flash SPI **W25Q64** (8 Mo chacune, 16 Mo au total) branchées sur le
port USER. Elle contient un fichier `.mnc` tel quel : la première puce porte ses 8 premiers Mo, la
seconde le reste. À la lecture, le core vérifie l'en-tête (`MNC1`, taille de 16 Mo au plus), puis
copie la ROM dans la SDRAM, la console restant en reset pendant la copie, comme pour un chargement
depuis la carte SD. Une cartouche invalide ou absente ne touche pas au jeu en cours : la LED
utilisateur clignote.

| Ligne | Broche USB 3 du port USER | Signal | Côté cartouche |
|---|---|---|---|
| `USER_IO0` | 3 (D+) | CLK | broche 6 des deux puces, pull-up 2,2 kΩ (1 kΩ pour 12 MHz) |
| `USER_IO1` | 2 (D−) | DI / IO0 | broche 5 des deux puces, pull-up 2,2 kΩ |
| `USER_IO2` | 8 (TX−) | /CS0 | broche 1 de la puce 0, pull-up 2,2 kΩ |
| `USER_IO3` | 7 (GND_D) | détection | reliée à la masse par la cartouche |
| `USER_IO4` | 6 (RX+) | DO / IO1 | broche 2 des deux puces |
| `USER_IO5` | 5 (RX−) | /CS1 | broche 1 de la puce 1, pull-up 2,2 kΩ |
| `USER_IO6` | 9 (TX+) | — | non connectée |

Alimentation : 5 V sur la broche 1, masse sur la broche 4, régulateur 3,3 V sur la cartouche. Sur
chaque puce, /WP (3) et /HOLD (7) sont tirés au 3,3 V (10 kΩ), avec 100 nF entre VCC (8) et GND (4).
Le port est à collecteur ouvert : le FPGA ne fait que tirer les lignes à 0, ce sont les pull-ups qui
les remontent, d'où une horloge limitée. **Le switch SW1 de la DE10-Nano doit être en bas** : levé, il
envoie le son HDMI sur `USER_IO2`, `USER_IO4` et `USER_IO5`.

Options OSD :

- **Lire la cartouche** : lecture immédiate.
- **Cartouche : Automatique / Manuelle**. En automatique, la cartouche est lue au démarrage, environ
  1,3 s après le dernier chargement depuis la carte SD (`boot.rom` compris), et dès qu'on l'enfiche.
  Un `.mnc` chargé ensuite depuis l'OSD reste prioritaire.
- **Horloge cartouche : 3, 6 ou 12 MHz**. 3 MHz convient aux pull-ups de 2,2 kΩ ; 6 et 12 MHz
  demandent des pull-ups plus faibles (1 kΩ) et un câblage court, c'est-à-dire un vrai PCB.
- **Lecture cartouche : Double / Simple**. Double (commande `0x3B`) lit deux bits par coup d'horloge,
  sur DO et DI ; Simple (`0x03`) n'utilise que DO, deux fois plus lent, en cas de doute sur le câblage.

Durée de lecture en lecture double : environ 0,74 Mo/s à 3 MHz, 3 Mo/s à 12 MHz (16 Mo en 22 s ou 6 s ;
L'Aigle d'Or, 372 Ko, en une demi-seconde à 3 MHz).

Préparer les images à graver (programmateur TL866, puce W25Q64) :

    python3 tools/cart/cart_split.py jeu.mnc     # -> jeu_puce0.bin, et jeu_puce1.bin au-delà de 8 Mo

## Si l'image est fausse ou si rien ne démarre

Le seul réglage qui dépend de la carte est l'instant où le contrôleur lit les données de la SDRAM. L'option OSD **Lecture SDRAM : Normale / Retardée** le décale d'un cycle. Si la ROM de test ne démarre pas, affiche des couleurs aberrantes ou des graphismes en désordre, passer sur **Retardée**, puis **Reset**. Noter quelle position fonctionne : c'est une information précieuse.

Ce qui manque encore : la rotation d'écran pour les jeux verticaux (l'image n'est pas tournée) et le réglage des DIP switches dans l'OSD (les valeurs par défaut de la ROM sont utilisées). La NVRAM, elle, est sauvegardée dans `saves/MONOCORE/<rom>.sav` (`mc_nvsave.sv`), à l'ouverture de l'OSD.

## Ce qu'il est utile de me renvoyer

- Une photo de l'écran sur la ROM de test (menu, puis les écrans des plans, des sprites et du son), et si le son sort.
- La position de l'option « Lecture SDRAM » qui fonctionne.
- `MONOCORE.fit.summary` et `MONOCORE.sta.summary`.
