// MONOCORE-1 « mc » : SDK bas niveau en C, sans assembleur.
//
// Pour porter des jeux complexes : accès direct au matériel, interruptions en C (VBLANK, ligne,
// timers), effets raster, table de sprites fantôme envoyée par DMA, CHR RAM, résolution variable,
// son PCM/PSG, mémoire dynamique (malloc) et une petite libc (string.h, stdlib.h, stdio.h).
// Se choisit dans project.txt avec « library = mc » (au lieu de la bibliothèque easy).
// Les registres bruts de monocore.h restent utilisables à côté.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "monocore.h"

// ================================================================ système
void     mc_init(void);                     // à appeler une fois, au début de main()
void     mc_frame(void);                    // attend la prochaine image, puis : sprites (DMA), entrées, musique
extern volatile uint32_t mc_frames;         // images écoulées (incrémenté par l'interruption VBLANK)
uint64_t mc_cycles(void);                   // cycles processeur depuis le démarrage (50 MHz)
uint32_t mc_micros(void);                   // microsecondes depuis le démarrage (boucle toutes les 71 min)

typedef void (*mc_handler_t)(void);
void mc_on_vblank(mc_handler_t fn);         // appelée à chaque début de pause verticale (ou NULL)
void mc_on_timer(int timer, uint32_t hz, mc_handler_t fn);   // timer 0 ou 1 ; hz = 0 l'arrête
static inline void mc_irq_off(void) { irq_global_disable(); }
static inline void mc_irq_on(void)  { irq_global_enable(); }

// Effets raster : fn est appelée juste avant que la ligne « line » soit calculée, et ses écritures
// (palette, défilement, registres) s'appliquent à partir de cette ligne. Jusqu'à 64 effets par
// image, ajoutés dans n'importe quel ordre ; ils restent actifs d'une image à l'autre.
#define MC_RASTER_MAX 64
int  mc_raster_add(int line, mc_handler_t fn);   // renvoie un numéro, ou -1 si la table est pleine
extern volatile int mc_raster_line;         // dans fn : ligne de l'effet en cours
void mc_raster_clear(void);

// Sortie de débogage (DEBUG_OUT) : visible dans le simulateur.
void mc_debug_putc(char c);
void mc_debug(const char *fmt, ...);        // printf réduit : %d %u %x %X %s %c %p %%, largeur et 0

// ================================================================ vidéo
void mc_video_mode(int width, int height);  // 224-384 x 192-288 ; 0, 0 = 320 x 240
int  mc_screen_w(void);
int  mc_screen_h(void);
void mc_display(uint32_t flags);            // DISP_ON | DISP_LAYER(n) | DISP_SPRITES | DISP_ROT(r)

// couleurs : 256 palettes de 16 couleurs (4096 entrées RGB555) ; en 8 bpp, une palette = 16 x 16
#define MC_RGB(r, g, b) RGB555(r, g, b)
void mc_color(int index, uint16_t rgb555);                    // index 0-4095
void mc_palette(int pal, const uint16_t *colors, int count);  // à partir de la palette pal (par DMA)
void mc_background(int index);                                // couleur de fond = PALETTE[index]

// plans (0-3) : tuiles 8 ou 16, 4 ou 8 bpp, carte de 32/64/128 tuiles de côté (4096 entrées au plus)
#define MC_TILE8   0
#define MC_TILE16  LC_TILE16
#define MC_4BPP    0
#define MC_8BPP    LC_8BPP
void mc_layer(int n, const void *chr, uint32_t flags, int map_w, int map_h, int prio);
void mc_layer_scroll(int n, int x, int y);
void mc_layer_linescroll(int n, bool on);
static inline volatile linescroll_t *mc_linescroll(int n) { return LINESCROLL(n); }
static inline volatile uint32_t *mc_tilemap(int n) { return TILEMAP(n); }
#define MC_TILE(tile, pal, flags) (TILE(tile, pal) | (flags))   // flags : T_HFLIP T_VFLIP T_PRIO
void mc_tile(int n, int x, int y, uint32_t entry);
void mc_tilemap_fill(int n, uint32_t entry);
void mc_tilemap_load(int n, const uint32_t *entries, int count);   // par DMA

// CHR RAM : 128 Ko de graphismes modifiables à l'exécution (décompression, polices, effets)
#define MC_CHRRAM      ((volatile uint8_t *)CHRRAM_BASE)
#define MC_CHRRAM_SIZE 0x20000u
void mc_chr_upload(uint32_t offset, const void *src, uint32_t bytes);   // par DMA

// sprites : 512 entrées, une table fantôme en WRAM, envoyée par DMA à chaque mc_frame()
// Taille en cellules de 8 pixels (1-32) ; zoom 0x100 = 1, 0x200 = 2 ; priorité 0-7.
// Un sprite de priorité p passe devant les pixels de plan de priorité effective <= p
// (priorité effective d'un plan = prio * 2 + bit T_PRIO de la tuile) ; à égalité entre
// sprites, le plus petit numéro est devant.
#define MC_SPRITES 512
extern sprite_t mc_spr[MC_SPRITES];
void mc_sprite_sheet(const void *chr);      // graphismes des sprites (ROM ou CHR RAM)
void mc_sprite(int n, int x, int y, int cell, int w, int h, int pal, int prio);
void mc_sprite_zoom(int n, int zoomx, int zoomy);
void mc_sprite_flip(int n, bool h, bool v);
void mc_sprite_8bpp(int n, bool on);
void mc_sprite_hide(int n);
void mc_sprites_hide_all(void);
void mc_sprites_used(int count);            // nombre d'entrées parcourues par le matériel (défaut 512)

// texte de débogage avec la police du SDK (cellules 8x8, 4 bpp) sur un plan au choix
void mc_text_layer(int n, int prio);        // configure le plan n pour le texte
void mc_text(int n, int col, int row, const char *s, int pal);
void mc_text_clear(int n);

// ================================================================ son
// 8 voies PCM (échantillons 8/16 bits ou ADPCM OKI, en ROM) et 8 voies PSG
void mc_pcm_play(int voice, const void *data, uint32_t samples, int format, uint32_t rate_hz,
                 uint32_t loop_start, int vol_l, int vol_r);   // loop_start = PCM_NOLOOP : une fois
void mc_pcm_stop(int voice);
uint32_t mc_pcm_pos(int voice);
void mc_psg(int ch, int wave, uint32_t freq_hz, int duty, int vol_l, int vol_r);   // PSG_SQUARE...
void mc_psg_freq16(int ch, uint32_t freq_1_16hz);
void mc_psg_volume(int ch, int vol_l, int vol_r);
void mc_master_volume(int vol);
// petit séquenceur (même syntaxe que play() d'easy : "C D E - . C5 ..."), joué par mc_frame()
void mc_tune(int ch, const char *notes, int frames_per_beat, int volume);
void mc_tune_stop(int ch);

// ================================================================ entrées
uint32_t mc_pad(int player);                // état brut (IN_UP ... IN_COIN), joueur 1 ou 2
uint32_t mc_pad_pressed(int player);        // boutons enfoncés depuis l'image précédente
uint32_t mc_pad_released(int player);
uint32_t mc_sys(void);                      // IN_SYS
uint32_t mc_dip(void);                      // DIP switches

// ================================================================ mémoire
// WRAM 128 Ko : .data, .bss, tas (malloc) puis pile (MC_STACK octets en haut).
#define MC_STACK 16384
size_t mc_heap_free(void);                  // octets libres dans le tas
#define MC_NVRAM      ((volatile uint8_t *)NVRAM_BASE)   // 32 Ko (sauvegarde sur SD : à venir)
#define MC_NVRAM_SIZE 0x8000u
static inline void mc_copy(volatile void *dst, const void *src, uint32_t n) { dma_copy(dst, src, n); }
static inline void mc_fill(volatile void *dst, uint32_t pattern, uint32_t n) { dma_fill(dst, pattern, n); }

// ================================================================ maths (virgule fixe 16.16)
typedef int32_t fix16;
#define FIX(x)        ((fix16)((x) * 65536))
#define FIX_INT(f)    ((int)((f) >> 16))
static inline fix16 fix_mul(fix16 a, fix16 b) { return (fix16)(((int64_t)a * b) >> 16); }
static inline fix16 fix_div(fix16 a, fix16 b) { return b ? (fix16)(((int64_t)a << 16) / b) : 0; }
fix16 mc_sin(int angle);                    // angle sur 1024 pas par tour ; résultat -1.0 à 1.0
fix16 mc_cos(int angle);
int   mc_atan2(int y, int x);               // angle sur 1024 pas
uint32_t mc_isqrt(uint32_t v);
uint32_t mc_rand(void);                     // xorshift32
void     mc_srand(uint32_t seed);
