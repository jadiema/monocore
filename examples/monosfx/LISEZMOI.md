# MONOSFX

L'éditeur de bruitages de la MONOCORE-1, troisième outil après MONOTRACKER (la musique) et
MONOPIXEL (les images). Il s'utilise à la souris et au clavier, en 640 × 480 entrelacé, ce qui
demande le core en version 5.

Une banque contient jusqu'à 32 bruitages. Chacun est un son d'un canal PSG recalculé 60 fois par
seconde, comme dans sfxr : onde, note qui glisse, enveloppe, vibrato, arpège, répétition. Chaque
réglage s'entend aussitôt (**ECOUTE AUTO**), et le graphique montre la note (en cyan, échelle par
octaves) et le volume (en orange) sur toute la durée du son. Les jeux jouent les banques avec le
module `msfx` (`sdk/MSFX.md`). L'interface est le module `ui480`.

## Construire

    python3 ../../tools/msfx/make_demo.py     # demo.mfx (déjà fourni)
    python3 ../../sdk/build.py .              # -> build/monosfx.mnc

Dans le simulateur, MONOSFX est dans la liste des ROM intégrées. La souris et le clavier de la
console s'activent seuls, et **Bruitages .mfx** télécharge les banques sauvées.

## Utilisation

| Souris / touche | Rôle |
|---|---|
| clic sur un bruitage | le choisit et le joue ; flèches : choisir sans jouer |
| Espace | joue le bruitage |
| Entrée, clic sur le nom | le nommer (8 lettres) : les jeux le trouvent par ce nom |
| compteurs | clic : +, clic droit : −, Maj : par 10, molette |
| CARRE, TRIANGLE, DENTS, BRUIT | l'onde |
| SAUT, PIECE, TIR, BOUM, BONUS, AIE | un son de ce genre, tiré au hasard à chaque clic |
| HASARD, MUTER | n'importe quel son ; de petits changements de celui en cours |
| Ctrl+C, Ctrl+V, Suppr | copier, coller, effacer un bruitage |
| Ctrl+S | sauver dans la NVRAM (ouvrir ensuite l'OSD pour l'écrire sur la carte SD) |
| F1, F2, F3 | effets, banque (8 emplacements, démo, clavier AZERTY/QWERTY), aide |

La façon la plus rapide de travailler : cliquer plusieurs fois sur un générateur jusqu'à un son
proche de l'idée, l'affiner avec MUTER ou les compteurs, puis le nommer.

## Pour un jeu

    python3 ../../tools/msfx/msfx.py monosfx.sav 1 -o ../monjeu/bruits.mfx

puis `modules = msfx` et `data = bruits.mfx`, et `ms_play(&banque, ms_find(&banque, "SAUT"))`.
`examples/plateformes` en est l'exemple.
