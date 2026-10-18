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

## Si l'image est fausse ou si rien ne démarre

Le seul réglage qui dépend de la carte est l'instant où le contrôleur lit les données de la SDRAM. L'option OSD **Lecture SDRAM : Normale / Retardée** le décale d'un cycle. Si la ROM de test ne démarre pas, affiche des couleurs aberrantes ou des graphismes en désordre, passer sur **Retardée**, puis **Reset**. Noter quelle position fonctionne : c'est une information précieuse.

Ce qui manque encore dans ce premier essai : la rotation d'écran pour les jeux verticaux (l'image n'est pas tournée), la sauvegarde de la NVRAM, et le réglage des DIP switches dans l'OSD (les valeurs par défaut de la ROM sont utilisées).

## Ce qu'il est utile de me renvoyer

- Une photo de l'écran sur la ROM de test (menu, puis les écrans des plans, des sprites et du son), et si le son sort.
- La position de l'option « Lecture SDRAM » qui fonctionne.
- `MONOCORE.fit.summary` et `MONOCORE.sta.summary`.
