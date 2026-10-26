# PIANO DEFENDER

Des envahisseurs descendent sur les touches d'un piano : jouez la note de leur colonne pour les abattre.
Le clavier MIDI est la seule arme (le clavier de l'ordinateur la remplace au besoin).

## Jouer

| Où | Comment |
|---|---|
| MiSTer | `defender.mnc` dans `games/MONOCORE`, le core version 4 et le clavier MIDI branché (menu du core : *UART Mode* → *MIDI*, *MidiLink : USB*), voir `manual/MONOCORE-1_Guide_MIDI.pdf` |
| Simulateur | PIANO DEFENDER dans la liste des ROM intégrées ; cochez « clavier MIDI » sous l'écran (Firefox : page ouverte par `dist/simulateur.bat`) |

Le clavier à l'écran couvre deux octaves, du do 3 au si 4 (notes MIDI 48 à 71), une colonne de 16 pixels
par touche. Sans clavier MIDI : W X C V… pour l'octave 3, A Z E R… pour l'octave 4.

| Geste | Effet |
|---|---|
| une note | tire dans sa colonne, sur l'envahisseur le plus bas |
| un accord | tire dans plusieurs colonnes à la fois |
| frapper fort (vélocité ≥ 100) | perce d'un coup les envahisseurs blindés (roses) ; sinon il faut deux tirs |
| molette de modulation vers le haut, ou Espace | bombe : tous les envahisseurs à l'écran |
| Échap | retour au titre |

Chaque tir coûte de l'énergie (la barre en haut à gauche), chaque envahisseur abattu en rend : taper au
hasard mène à la surchauffe. Les points montent quand on abat les envahisseurs tôt (haut dans l'écran), et
la série les multiplie, jusqu'à ×5. Un envahisseur qui touche le sol coûte une vie ; il y en a cinq.

## Les vagues

| Vague | Notes |
|---|---|
| 1 | do, ré, mi |
| 2 | do à sol |
| 3 | la gamme de do (octave 4) |
| 4 | les touches blanches des deux octaves ; des accords apparaissent |
| 5 et plus | toutes les touches |

Les touches de la vague sont en jaune sur le clavier. Les envahisseurs arrivent sur les temps de la
musique (`groove.mts`, en la mineur, faite par `make_groove.py`) et suivent des lignes mélodiques dans la
gamme : en les abattant, on joue avec le morceau. Les blindés arrivent à la vague 2, les rapides (jaunes)
à la vague 3. Chaque vague terminée rapporte un bonus et une bombe (trois au plus).

## Comment c'est fait

Les tirs sont joués sur les pistes PSG 1 à 4, réservées (`mt_reserve`), avec un instrument défini dans le
programme ; les explosions sur la piste PCM 8. Le rythme des apparitions suit la ligne jouée par `mtrack`
(`mt_row`). Les envahisseurs sont des sprites de 16 × 16, les rayons et le sol une couche de tuiles, le
clavier et les textes une autre, le ciel étoilé une troisième qui défile lentement.
