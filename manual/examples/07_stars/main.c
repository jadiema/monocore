// ATTRAPE-ETOILES - the complete game of the MONOCORE-1 manual
#include "easy.h"

// pictures of sheet.png
#define SHIP      (sheet + 0)
#define STAR      (sheet + 1)
#define STAR2     (sheet + 2)
#define BOMB      (sheet + 3)
#define EXPLOSION (sheet + 4)
#define HEART     (sheet + 5)

// sprites in use
#define SPR_PLAYER  0
#define SPR_ITEM    1      // 1 to 6: the falling items
#define NUM_ITEMS   6
#define SPR_LIFE    10     // 10 to 12: the hearts

typedef struct {
  int active;
  int x, y;              // position in pixels
  int speed;             // pixels per frame
  int bomb;              // 1 = bomb, 0 = star
} item_t;

static item_t items[NUM_ITEMS];
static int player_x, score, best, lives, explosion;

static const char *tune =
  "E G C5 - B A G - A B C5 D5 E5 - - . "
  "E5 D5 C5 - B A G - F A G E D - - . ";

// ---------------------------------------------------------------- display
static void show_score(void) {
  print(1, 1, "SCORE");
  print_num(7, 1, score, 6, 1);
  print(25, 1, "RECORD");
  print_num(32, 1, best, 6, 2);
  for (int i = 0; i < 3; i++) {
    if (i < lives) sprite(SPR_LIFE + i, 8 + i * 18, 214, HEART);
    else hide_sprite(SPR_LIFE + i);
  }
}

static void hide_all(void) {
  for (int n = 0; n < 64; n++) hide_sprite(n);
}

// ---------------------------------------------------------------- items
static void drop_item(item_t *it) {
  it->active = 1;
  it->x = 8 + random(288);
  it->y = -16 - random(80);
  it->speed = 1 + random(2) + score / 300;     // faster and faster
  if (it->speed > 6) it->speed = 6;
  it->bomb = random(4) == 0;                   // one chance in four
}

static void move_items(void) {
  for (int i = 0; i < NUM_ITEMS; i++) {
    item_t *it = &items[i];
    it->y = it->y + it->speed;
    if (it->y > 240) drop_item(it);            // out at the bottom: drop it again from the top

    int picture = it->bomb ? BOMB : (((frame_count / 8 + i) % 2) ? STAR : STAR2);
    sprite(SPR_ITEM + i, it->x, it->y, picture);

    if (collide(SPR_ITEM + i, SPR_PLAYER)) {
      if (it->bomb) {
        lives = lives - 1;
        explosion = 30;                        // 30 frames of explosion
        play_sample(0, boom, 230);
      } else {
        score = score + 10;
        tone(1, 1200, 120);                    // a short "ding"
      }
      drop_item(it);                           // back to the top, out of the screen
      hide_sprite(SPR_ITEM + i);               // until it is shown there, at the next frame
    }
  }
}

// ---------------------------------------------------------------- screens
static void title_screen(void) {
  cls();
  hide_all();
  print_ink(11, 7, "ATTRAPE-ETOILES", 1);
  print(6, 11, "ATTRAPE LES ETOILES : 10 POINTS");
  print(6, 13, "EVITE LES BOMBES    : -1 VIE");
  print_ink(11, 27, "UN JEU MONOCORE-1", 0);
  print(25, 1, "RECORD");
  print_num(32, 1, best, 6, 2);
  play(2, tune, 10, 90);

  for (;;) {
    wait_frame();
    if ((frame_count / 30) % 2 == 0) print_ink(12, 18, "APPUIE SUR START", 6);
    else                             print    (12, 18, "                ");
    for (int i = 0; i < 4; i++)                // four stars dancing under the rules
      sprite(SPR_ITEM + i, 152 + (i - 2) * 50 + 25, 118 + ((frame_count / 3 + i * 3) % 8), STAR);
    scroll_map(frame_count / 2, 0);
    if (pressed(1, PAD_START)) break;
  }
  stop(2);
}

static void game(void) {
  cls();
  hide_all();
  score = 0; lives = 3; explosion = 0;
  player_x = 152;
  for (int i = 0; i < NUM_ITEMS; i++) drop_item(&items[i]);

  while (lives > 0) {
    wait_frame();
    if (held(1, PAD_LEFT)  && player_x > 4)   player_x -= 3;
    if (held(1, PAD_RIGHT) && player_x < 300) player_x += 3;
    if (frame_count % 8 == 0) quiet(1);        // ends the "ding"

    if (explosion > 0) {
      explosion--;
      sprite(SPR_PLAYER, player_x, 196, EXPLOSION);
    } else {
      sprite(SPR_PLAYER, player_x, 196, SHIP);
      move_items();                            // the game pauses during the explosion
    }
    if (score > best) best = score;
    show_score();
    scroll_map(frame_count, 0);
  }
}

static void game_over(void) {
  for (int i = 0; i < NUM_ITEMS; i++) hide_sprite(SPR_ITEM + i);
  print_ink(12, 12, "PARTIE TERMINEE", 4);
  for (int t = 0; t < 180; t++) {              // 3 seconds, or START
    wait_frame();
    if (pressed(1, PAD_START)) break;
  }
}

int main(void) {
  init();                                      // loads sheet.png and landscape.png
  background(0, 0, 0);
  best = 500;
  for (;;) {
    title_screen();
    game();
    game_over();
  }
}
