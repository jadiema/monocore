# Module « mjeu » : les parties communes d'un petit jeu

`mjeu` s'appuie sur une banque de **MONOPIXEL** (module `mgfx`, `MGFX.md`). Il fournit ce que
presque chaque jeu réécrit : des objets qui se déplacent et se heurtent au décor, les collisions
entre objets, une caméra qui suit, l'affichage, des bruitages et une petite sauvegarde. Le niveau
se fait entièrement dans l'éditeur : le décor, **ce que sont les tuiles** (solide, plateforme,
échelle, eau, danger) et **les objets posés dedans** (départ du héros, pièces, ennemis…). Le
programme ne dit plus que ce que fait chaque sorte d'objet.

Dans `project.txt` : `library = mc`, `modules = mgfx mjeu`. L'exemple complet est
`examples/plateformes`, un modèle de jeu de plateformes d'environ 150 lignes à copier.

## Le principe

    static mg_bank_t bank; static mj_world_t w; static mj_obj_t obj[64];

    mg_open(&bank, niveau_file, niveau_size);
    mg_load(&bank, 0, 16, MG_LOAD_ALL);
    mj_world(&w, &bank, 0, 1);              // le décor 0 sur le plan 1
    int n = mj_from_map(&w, obj, 64);       // un objet par objet posé dans MONOPIXEL
    int HEROS = mg_sprite_find(&bank, "HEROS");
    for (;;) {
      mj_frame();                           // mc_frame() et les bruitages
      for (int i = 0; i < n; i++) {
        mj_obj_t *o = &obj[i];
        if (o->sprite == HEROS) { ...; mj_move(&w, o); }
        ...
      }
      mj_camera(&w, heros);
      mj_draw_all(&w, obj, n, 1, 4);        // sprites matériels 1 à n, priorité 4
    }

Un objet sait à quel lutin il correspond (`o->sprite`), et on reconnaît une sorte d'objet au nom
de son lutin. Pour ajouter une sorte d'objet, on dessine un lutin, on lui donne un nom, on le pose
dans le décor et on ajoute un cas dans le programme. Le paramètre réglé dans l'éditeur (`o->param`,
0 à 255) sert à varier les objets d'une même sorte : vitesse d'un ennemi, numéro d'une porte,
nombre de points…

## Les objets

| Champ | Contenu |
|---|---|
| `x`, `y` | coin haut gauche du lutin, en 1/16 de pixel (`MJ_FIX`) ; `mj_px`, `mj_py`, `mj_set_pos` en pixels |
| `vx`, `vy` | vitesse en 1/16 de pixel par image |
| `bx`, `by`, `bw`, `bh` | boîte de collision depuis le coin du lutin (par défaut le lutin entier ; `mj_box`) |
| `sprite`, `frame` | lutin de la banque, image montrée sans `MJ_ANIM` |
| `flags` | `MJ_ACTIVE` (existe), `MJ_GRAVITY` (tombe), `MJ_HFLIP` (tourné vers la gauche), `MJ_ANIM` (animé), `MJ_GHOST` (traverse le décor), `MJ_HIDDEN` (pas dessiné : clignotement), `MJ_DOWN` (descend à travers les plateformes pendant cette image) |
| `touch` | ce que `mj_move` a rencontré : `MJ_GROUND`, `MJ_CEILING`, `MJ_LEFT`, `MJ_RIGHT` |
| `param`, `type`, `timer`, `data[3]` | `param` vient de l'éditeur ; les autres sont libres pour le jeu |

Un objet disparaît quand on enlève `MJ_ACTIVE` : il n'est plus déplacé, plus dessiné et ne
touche plus rien.

## Fonctions

| Fonction | Rôle |
|---|---|
| `mj_world(w, banque, décor, plan)` | met le décor sur le plan et prépare le monde : taille, tuiles, caméra, gravité 4, chute maximale 96, bords gauche et droit en murs (`w->walls`) |
| `mj_from_map(w, objets, max)` | crée un objet par objet du décor (actif, animé, boîte = le lutin, miroir et paramètre de l'éditeur) |
| `mj_obj(o, w, lutin, x, y)` | crée un objet à la main (un tir, une explosion…) |
| `mj_move(w, o)` | applique la gravité et la vitesse ; les tuiles `MG_SOLID` arrêtent l'objet, les `MG_PLATFORM` le portent seulement s'il arrive d'en haut ; remplit `o->touch` |
| `mj_touches(w, o, attribut)` | une tuile de cet attribut sous la boîte ? (`MG_DANGER`, `MG_WATER`, `MG_LADDER`, `MG_USER_A`…) |
| `mj_overlap(a, b)` | les boîtes de deux objets actifs se chevauchent |
| `mj_patrol(w, o, vitesse, bords)` | fait marcher l'objet (appelle `mj_move`) et le fait se retourner contre un mur et, si `bords`, au bord d'un vide |
| `mj_attr(w, x, y)` | les attributs de la tuile sous un pixel du décor |
| `mj_camera(w, o)`, `mj_camera_at(w, x, y)` | la caméra suit l'objet (avec une zone morte au centre) ou se place ; elle reste dans le décor et fait défiler le plan |
| `mj_draw(w, o, sprite, prio)`, `mj_draw_all(w, objets, n, premier, prio)` | dessine aux positions de la caméra ; cache les objets inactifs ou hors de l'écran |
| `mj_sfx(canal, onde, f0, f1, images, volume)` | un bruitage sur un canal PSG : un son qui glisse de f0 à f1 Hz en s'éteignant (`PSG_SQUARE`, `PSG_TRIANGLE`, `PSG_SAW`, `PSG_NOISE`) ; on prend des canaux que la musique n'utilise pas |
| `mj_frame()` | `mc_frame()` puis fait avancer les bruitages ; à appeler à la place de `mc_frame()` |
| `mj_save(données, n)`, `mj_load(données, n)` | une sauvegarde de 4000 octets au plus au début de la NVRAM, avec une somme de contrôle (record, progression) ; `mj_load` renvoie false s'il n'y a rien de valable |

Les nombres aléatoires viennent de `mc_rand()`.

## Recettes du modèle de plateformes

- **Saut à hauteur variable.** Au sol, appuyer sur B1 donne `vy = -76`. Quand on lâche le bouton
  pendant la montée, `vy` est ramené à −24.
- **Descendre d'une plateforme.** Bas + B1 met `MJ_DOWN` pour une image.
- **Sauter sur un ennemi.** Le héros descend (`vy > 0`) et le bas de sa boîte est au-dessus du
  haut de celle de l'ennemi : l'ennemi disparaît et le héros rebondit. Sinon, une vie est perdue.
- **Clignotement après un coup.** On utilise `MJ_HIDDEN` une image sur deux pendant 1,5 s, et le
  héros est invulnérable pendant ce temps.
- **Scènes.** Une variable `scene` (titre, jeu, gagné, perdu) choisit ce que fait la boucle. On
  revient au titre par `mj_world` et `mj_from_map`, qui reconstruisent le niveau tel qu'il a été
  dessiné.

## Limites actuelles

Les collisions avec le décor se font sur des boîtes, sans pentes. Les vitesses doivent rester sous
une tuile par image (16 pixels), ce que la chute maximale garantit. Il n'y a pas encore d'échelles
« grimpables » toutes faites (`mj_touches(…, MG_LADDER)` dit seulement qu'on est devant), ni de
gestion de plusieurs niveaux en une banque : on peut déjà utiliser les 4 décors avec `mj_world`.
