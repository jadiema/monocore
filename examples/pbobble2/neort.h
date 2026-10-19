// Neo Geo runtime for the translated 68000 program: memory map, LSPC video RAM, palettes, inputs,
// sound command latch and a replacement for the BIOS (AES mode). It does not depend on MONOCORE:
// the console layer (main.c) and the PC test bench (pc/bench.c) both sit on top of it.
#ifndef NEORT_H
#define NEORT_H
#include <stdint.h>
#include "m68k_rt.h"

// memories, 16-bit words stored little-endian (byte access at address ^ 1), like p1.bin
extern const uint8_t *neo_rom;        // program ROM (p1.bin), at 0x000000
extern uint32_t neo_rom_size;
extern uint8_t neo_ram[0x10000];      // work RAM, 0x100000-0x10FFFF (mirrored up to 0x1FFFFF)
extern uint8_t neo_sram[0x10000];     // backup RAM, 0xD00000-0xD0FFFF
extern uint16_t neo_vram[0x8800];     // LSPC: 32K words of slow VRAM + 2K words of fast VRAM
extern uint16_t neo_pal[2][0x1000];   // palette RAM, two banks of 4096 colours

typedef struct {
  uint16_t vram_addr, vram_mod, lspc_mode;
  uint8_t pal_bank, fix_cart;         // palette bank shown by the CPU, fix tiles from the cartridge
  uint8_t vbl_pending, timer_pending;
  uint8_t sound_cmd, sound_reply;
  uint8_t p1, p2, status_b;           // inputs set by the platform layer, 1 = pressed:
                                      // p1/p2 bits 0 up, 1 down, 2 left, 3 right, 4 A, 5 B, 6 C, 7 D;
                                      // status_b bits 0 P1 start, 1 P1 select, 2 P2 start, 3 P2 select
  uint8_t user_request;               // last BIOS_USER_REQUEST given to the game
  uint32_t frame;                     // frames since the reset
  uint32_t sound_log[64]; uint32_t sound_count;   // last sound commands (for the bench)
  uint32_t pal_dirty;                 // set when the palette changes (console layer)
} neo_state_t;
extern neo_state_t neo;

void neo_reset(m68k_t *c);            // hardware reset, then the BIOS starts the game (USER, request 0)
void neo_vblank(m68k_t *c);           // start of the vertical blank: raises the level 1 interrupt
uint32_t neo_bios(m68k_t *c, uint32_t pc);   // BIOS entry points; returns the next address, or 0
                                             // if pc is not a BIOS entry point

// provided by the platform layer: current raster line, 0-263 (0xF8 = start of the vertical blank)
uint32_t neo_platform_line(void);

// hardware accesses, out of the fast paths of m68k_port.h
uint32_t neo_rd8(uint32_t a);
uint32_t neo_rd16(uint32_t a);
void neo_wr8(uint32_t a, uint32_t v);
void neo_wr16(uint32_t a, uint32_t v);
#endif
