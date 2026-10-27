# MONOATELIER

Le lanceur de l'atelier : une seule ROM avec MONOPIXEL, MONOSFX, MONOTRACKER et le jeu
PLATEFORMES, qui partagent une seule NVRAM. On y fait un jeu entièrement sur la console : le niveau,
les bruitages et la musique, puis on y joue. Tout est expliqué dans `sdk/ATELIER.md`.

    python3 ../../tools/mkatelier.py     # -> build/monoatelier.mnc (environ 6 Mo)

Sur MiSTer, `monoatelier.mnc` va dans `games/MONOCORE`, avec une souris et un clavier. F1, F2, F3
ouvrent les éditeurs, F5 lance le jeu, et F12 dans chacun ramène ici.

`python3 ../../sdk/build.py .` construit le lanceur seul (`build/atelier.mnc`). Il signale alors
qu'il n'est pas dans un atelier, ce qui ne sert qu'à le mettre au point.
