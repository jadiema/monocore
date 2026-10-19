# AUBE — écran d'accueil de la MONOCORE-1

Un écran d'accueil épuré, dans l'esprit des démarrages de la PlayStation ou de l'Amiga : une aube
se lève sur une plaine où se dresse un monolithe noir, des poussières de lumière montent dans la
lueur, puis le nom de la console apparaît sur l'accord final, suivi de « INSÉREZ UNE CARTOUCHE ».
La musique est une courte pièce orchestrale (cordes, chœur, cors, célesta, harpe, timbales, environ
30 s), de ré mineur vers ré majeur. START rejoue la séquence.

- **Image** : tout passe par la palette. `scene.png` ne contient que la position de chaque pixel
  (bande du ciel et niveau de lueur, plaine, monolithe, collines) ; `main.c` recalcule les 256
  couleurs à chaque image selon le temps, d'où le lever de soleil sans redessiner un pixel.
- **Son** : la musique est synthétisée à l'avance par `make_music.py` (numpy, scipy) et jouée par
  deux voix PCM 16 bits à 22 050 Hz (gauche et droite), soit 2,6 Mo dans la ROM.
- Les fichiers produits (`*.png`, `music_*.raw`) sont dans le dépôt : `build.py` suffit pour
  construire la ROM. `make_assets.py` et `make_music.py` ne servent qu'à les refaire.

    python3 ../../sdk/build.py .          # -> build/aube.mnc

Pour en faire l'écran d'accueil de la console, copier `build/aube.mnc` sous le nom
`games/MONOCORE/boot.rom` sur la carte SD de la MiSTer.
