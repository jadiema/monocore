// MONOCORE-1 "mc": the low-level SDK, in C, with no assembly language.
//
// For writing or porting complex games: direct hardware access, interrupts in C (VBLANK, line,
// timers), raster effects, a shadow sprite table sent by DMA, CHR RAM, variable resolution,
// PCM/PSG sound, dynamic memory (malloc) and a small libc (string.h, stdlib.h, stdio.h).
// Selected in project.txt with "library = mc" (instead of the easy library).
// The raw registers of monocore.h can still be used alongside. Documentation: SDK_MC.md.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "monocore.h"

// ================================================================ system
void     mc_init(void);                     // call once, at the start of main()
void     mc_frame(void);                    // waits for the next frame, then: sprites (DMA), inputs, music
extern volatile uint32_t mc_frames;         // frames so far (counted by the VBLANK interrupt)
uint64_t mc_cycles(void);                   // CPU cycles since start-up (50 MHz)
uint32_t mc_micros(void);                   // microseconds since start-up (wraps after 71 minutes)

typedef void (*mc_handler_t)(void);
void mc_on_vblank(mc_handler_t fn);         // called at the start of each vertical blank (or NULL)
void mc_on_timer(int timer, uint32_t hz, mc_handler_t fn);   // timer 0 or 1; hz = 0 stops it
static inline void mc_irq_off(void) { irq_global_disable(); }
static inline void mc_irq_on(void)  { irq_global_enable(); }

// Raster effects: fn is called just before line "line" is rendered, and what it writes (palette,
// scrolling, registers) applies from that line on. Up to 64 effects per frame, added in any
// order; they stay active from one frame to the next.
#define MC_RASTER_MAX 64
int  mc_raster_add(int line, mc_handler_t fn);   // returns a number, or -1 when the table is full
extern volatile int mc_raster_line;         // inside fn: line of the effect being run
void mc_raster_clear(void);

// Debug output (DEBUG_OUT): shown by the simulators, ignored by the FPGA.
void mc_debug_putc(char c);
void mc_debug(const char *fmt, ...);        // reduced printf: %d %u %x %X %s %c %p %%, width and 0

// ================================================================ video
void mc_video_mode(int width, int height);  // 224-384 x 192-288; 0, 0 = 320 x 240
int  mc_screen_w(void);
int  mc_screen_h(void);
void mc_display(uint32_t flags);            // DISP_ON | DISP_LAYER(n) | DISP_SPRITES | DISP_ROT(r)

// colours: 256 palettes of 16 colours (4096 RGB555 entries); in 8 bpp, one palette = 16 x 16
#define MC_RGB(r, g, b) RGB555(r, g, b)
void mc_color(int index, uint16_t rgb555);                    // index 0-4095
void mc_palette(int pal, const uint16_t *colors, int count);  // from palette pal on (DMA)
void mc_background(int index);                                // background colour = PALETTE[index]

// layers (0-3): 8 or 16 pixel tiles, 4 or 8 bpp, maps of 32/64/128 tiles a side (4096 entries at most)
#define MC_TILE8   0
#define MC_TILE16  LC_TILE16
#define MC_4BPP    0
#define MC_8BPP    LC_8BPP
void mc_layer(int n, const void *chr, uint32_t flags, int map_w, int map_h, int prio);
void mc_layer_scroll(int n, int x, int y);
void mc_layer_linescroll(int n, bool on);
static inline volatile linescroll_t *mc_linescroll(int n) { return LINESCROLL(n); }
static inline volatile uint32_t *mc_tilemap(int n) { return TILEMAP(n); }
#define MC_TILE(tile, pal, flags) (TILE(tile, pal) | (flags))   // flags: T_HFLIP T_VFLIP T_PRIO
void mc_tile(int n, int x, int y, uint32_t entry);
void mc_tilemap_fill(int n, uint32_t entry);
void mc_tilemap_load(int n, const uint32_t *entries, int count);   // DMA

// CHR RAM: 128 KB of graphics that can be changed at run time (decompression, fonts, effects)
#define MC_CHRRAM      ((volatile uint8_t *)CHRRAM_BASE)
#define MC_CHRRAM_SIZE 0x20000u
void mc_chr_upload(uint32_t offset, const void *src, uint32_t bytes);   // DMA

// sprites: 512 entries, a shadow table in WRAM sent by DMA at each mc_frame()
// Size in 8-pixel cells (1-32); zoom 0x100 = 1, 0x200 = 2; priority 0-7.
// A sprite of priority p is drawn over layer pixels whose effective priority is <= p
// (effective priority of a layer = prio * 2 + the T_PRIO bit of the tile); between sprites,
// the lowest number is in front.
#define MC_SPRITES 512
extern sprite_t mc_spr[MC_SPRITES];
void mc_sprite_sheet(const void *chr);      // sprite graphics (ROM or CHR RAM)
void mc_sprite(int n, int x, int y, int cell, int w, int h, int pal, int prio);
void mc_sprite_zoom(int n, int zoomx, int zoomy);
void mc_sprite_flip(int n, bool h, bool v);
void mc_sprite_8bpp(int n, bool on);
void mc_sprite_hide(int n);
void mc_sprites_hide_all(void);
void mc_sprites_used(int count);            // entries scanned by the hardware (default 512)

// debug text with the SDK font (8x8 cells, 4 bpp) on any layer
void mc_text_layer(int n, int prio);        // sets layer n up for text (font palette in palette 0)
void mc_text(int n, int col, int row, const char *s, int pal);
void mc_text_clear(int n);

// ================================================================ sound
// 8 PCM voices (8/16-bit samples or OKI ADPCM, in ROM) and 8 PSG channels
void mc_pcm_play(int voice, const void *data, uint32_t samples, int format, uint32_t rate_hz,
                 uint32_t loop_start, int vol_l, int vol_r);   // loop_start = PCM_NOLOOP: once
void mc_pcm_stop(int voice);
uint32_t mc_pcm_pos(int voice);
void mc_psg(int ch, int wave, uint32_t freq_hz, int duty, int vol_l, int vol_r);   // PSG_SQUARE...
void mc_psg_freq16(int ch, uint32_t freq_1_16hz);
void mc_psg_volume(int ch, int vol_l, int vol_r);
void mc_master_volume(int vol);
// small sequencer (same syntax as play() in easy: "C D E - . C5 ..."), stepped by mc_frame()
void mc_tune(int ch, const char *notes, int frames_per_beat, int volume);
void mc_tune_stop(int ch);

// ================================================================ inputs
uint32_t mc_pad(int player);                // raw state (IN_UP ... IN_COIN), player 1 or 2
uint32_t mc_pad_pressed(int player);        // buttons that went down since the previous frame
uint32_t mc_pad_released(int player);
uint32_t mc_sys(void);                      // IN_SYS
uint32_t mc_dip(void);                      // DIP switches

// ================================================================ memory
// WRAM 128 KB: .data, .bss, heap (malloc), then the stack (MC_STACK bytes at the top).
#define MC_STACK 16384
size_t mc_heap_free(void);                  // free bytes in the heap
#define MC_NVRAM      ((volatile uint8_t *)NVRAM_BASE)   // 32 KB (not saved to the SD card yet)
#define MC_NVRAM_SIZE 0x8000u
static inline void mc_copy(volatile void *dst, const void *src, uint32_t n) { dma_copy(dst, src, n); }
static inline void mc_fill(volatile void *dst, uint32_t pattern, uint32_t n) { dma_fill(dst, pattern, n); }

// ================================================================ maths (16.16 fixed point)
typedef int32_t fix16;
#define FIX(x)        ((fix16)((x) * 65536))
#define FIX_INT(f)    ((int)((f) >> 16))
static inline fix16 fix_mul(fix16 a, fix16 b) { return (fix16)(((int64_t)a * b) >> 16); }
static inline fix16 fix_div(fix16 a, fix16 b) { return b ? (fix16)(((int64_t)a << 16) / b) : 0; }
fix16 mc_sin(int angle);                    // angle in 1024 steps per turn; result -1.0 to 1.0
fix16 mc_cos(int angle);
int   mc_atan2(int y, int x);               // angle in 1024 steps
uint32_t mc_isqrt(uint32_t v);
uint32_t mc_rand(void);                     // xorshift32
void     mc_srand(uint32_t seed);
