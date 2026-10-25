# AUBE — écran d'accueil de la MONOCORE-1

Un écran d'accueil épuré, dans l'esprit des démarrages de la PlayStation ou de l'Amiga : une aube
se lève sur un lac où se dresse un monolithe noir, devant une ligne de collines, des poussières de lumière montent dans la
lueur, puis le nom de la console apparaît sur l'accord final, suivi de « INSÉREZ UNE CARTOUCHE ».
La musique est une courte pièce orchestrale (cordes, chœur, cors, célesta, harpe, timbales, environ
30 s), de ré mineur vers ré majeur. START rejoue la séquence.

- **Image** : 360 × 270 pixels, soit exactement 4 fois l'écran en 1080p (1440 × 1080) : le scaler de la MiSTer
  n'interpole rien et chaque pixel reste net. La scène est la peinture `monolithe.png`, réduite et
  ramenée à 230 couleurs par `make_assets.py` (165 pour le ciel et les collines, 65 pour le lac),
  plus 12 niveaux pour le bord du monolithe. Tout passe par la palette : `scene_colors.h` donne pour
  chaque couleur sa teinte de nuit et le moment où elle prend la lumière (les couleurs vives près du
  soleil d'abord) ; `main.c` recalcule les couleurs à chaque image, d'où le lever de soleil sans
  redessiner un pixel. Le texte n'ayant pas de transparence, chaque tuile de texte a sa propre
  palette, mélangée avec la couleur de la peinture derrière elle.
- **Son** : la musique est synthétisée à l'avance par `make_music.py` (numpy, scipy) et jouée par
  deux voix PCM 16 bits à 22 050 Hz (gauche et droite), soit 2,6 Mo dans la ROM.
- Les fichiers produits (`scene.png`, `logo.png`, `prompt.png`, `mote.png`, `scene_colors.h`, `music_*.raw`) sont dans le dépôt : `build.py` suffit pour
  construire la ROM. `make_assets.py` et `make_music.py` ne servent qu'à les refaire.

    python3 ../../sdk/build.py .          # -> build/aube.mnc

Pour en faire l'écran d'accueil de la console, copier `build/aube.mnc` sous le nom
`games/MONOCORE/boot.rom` sur la carte SD de la MiSTer.
