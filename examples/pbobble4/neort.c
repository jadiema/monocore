// Neo Geo runtime: see neort.h. Addresses and BIOS variables follow the NeoGeoDev wiki.
#include "neort.h"
#ifdef NEO_ATTACK_LOG
#include <stdio.h>
#endif

const uint8_t *neo_rom;
uint32_t neo_rom_size;
uint8_t neo_ram[0x10000], neo_sram[NEO_SRAM_SIZE];
uint16_t *neo_vram;
uint16_t neo_pal[2][0x1000];
neo_state_t neo;

// BIOS variables in work RAM
#define BIOS_SYSTEM_MODE  0x10FD80
#define BIOS_MVS_FLAG     0x10FD82
#define BIOS_COUNTRY_CODE 0x10FD83
#define BIOS_P1STATUS     0x10FD94      // then PREVIOUS, CURRENT, CHANGE, REPEAT, TIMER; P2 at +6
#define BIOS_STATCURNT    0x10FDAC
#define BIOS_STATCHANGE   0x10FDAD
#define BIOS_USER_REQUEST 0x10FDAE
#define BIOS_USER_MODE    0x10FDAF
#define BIOS_START_FLAG   0x10FDB4
#define BIOS_SSP          0x10F300
#define BIOS_VBL_WAIT     0x10FE8C      // set by code that waits for the next frame, cleared by SYSTEM_INT1
// BIOS entry points
#define SYSTEM_INT1    0xC00438
#define SYSTEM_RETURN  0xC00444
#define SYSTEM_IO      0xC0044A
#define CREDIT_CHECK   0xC00450
#define CREDIT_DOWN    0xC00456
#define FIX_CLEAR      0xC004C2
#define LSP_1ST        0xC004C8
#define AFTER_START    0xC0F000         // private: where PLAYER_START returns to the BIOS

// The Z80 driver reads the commands one by one: 0A, 0E, 14 and 18-1E take the next byte as their
// parameter, 15 takes two (15 a b: sound b of the slot a, for the second player; given to the
// platform as the event a b), 00 is ignored, 03 resets the driver at once (tools/sound/events.py).
static void sound_byte(uint32_t v) {
  if (neo.sound_left) {
    if (neo.sound_prefix == 0x15 && neo.sound_left == 2) { neo.sound_first = (uint8_t)v; neo.sound_left = 1; return; }
    uint32_t p = neo.sound_prefix; neo.sound_left = 0; neo.sound_prefix = 0;
    if (p == 0x15) neo_platform_sound(neo.sound_first, v); else neo_platform_sound(p, v);
    return;
  }
  if (v == 0) return;
  if (v == 0x15) { neo.sound_prefix = 0x15; neo.sound_left = 2; return; }
  if (v == 0x0A || v == 0x0E || v == 0x14 || (v >= 0x18 && v <= 0x1E)) { neo.sound_prefix = (uint8_t)v; neo.sound_left = 1; return; }
  neo_platform_sound(0, v);
}

static inline uint32_t ram8(uint32_t a) { return neo_ram[(a & 0xFFFF) ^ 1]; }
static inline void wram8(uint32_t a, uint32_t v) { neo_ram[(a & 0xFFFF) ^ 1] = v; }
static inline uint32_t rom32(uint32_t a) {
  const uint16_t *w = (const uint16_t *)(neo_rom + a);
  return ((uint32_t)w[0] << 16) | w[1];
}
static inline uint32_t mem16(uint32_t a) {          // RAM word (for the BIOS code below)
  return *(uint16_t *)(neo_ram + (a & 0xFFFE));
}
static inline uint32_t mem32(uint32_t a) { return (mem16(a) << 16) | mem16(a + 2); }
static inline void wmem16(uint32_t a, uint32_t v) { *(uint16_t *)(neo_ram + (a & 0xFFFE)) = v; }
static inline void wmem32(uint32_t a, uint32_t v) { wmem16(a, v >> 16); wmem16(a + 2, v); }

static void update_ipl(m68k_t *c) {
  c->ipl = neo.timer_pending ? 2 : neo.vbl_pending ? 1 : 0;
}
static m68k_t *cpu;

// --- video RAM ---
static inline uint32_t vram_index(uint32_t a) { return (a & 0x8000) ? 0x8000 + (a & 0x7FF) : (a & 0x7FFF); }

static uint32_t lspc_read(uint32_t reg) {
  switch (reg & 6) {
  case 0: case 2: return neo_vram[vram_index(neo.vram_addr)];
  case 4: return neo.vram_mod;
  default: {                                         // line counter (bits 15-7) and auto-animation
    uint32_t line = neo_platform_line() + 0xF8;      // the counter runs from 0xF8 to 0x1FF
    if (line > 0x1FF) line -= 0x108;
    return (line << 7) | ((neo.frame / ((neo.lspc_mode >> 8) + 1)) & 7);
  }
  }
}

static void lspc_write(uint32_t reg, uint32_t v) {
  switch (reg & 0xE) {
  case 0x0: neo.vram_addr = v; break;
  case 0x2: {
    uint32_t i = vram_index(neo.vram_addr);
    neo_vram[i] = v;
#ifdef NEO_READ_TRACE
    { extern void neo_scb_source(uint32_t i, uint32_t v); neo_scb_source(i, v); }
#endif
#ifdef NEO_VRAM_OWNER
    { extern void neo_vram_owner(uint32_t i, uint32_t task); neo_vram_owner(i, neo_ram[0x34C4] | neo_ram[0x34C5] << 8); }   // debug (pc/bench.c)
#endif
    if (i >= 0x7000 && i < 0x7500) neo_platform_fix((i - 0x7000) >> 5, i & 31, v);
    neo.vram_addr = (neo.vram_addr & 0x8000) | ((neo.vram_addr + neo.vram_mod) & 0x7FFF);
    break;
  }
  case 0x4: neo.vram_mod = v; break;
  case 0x6: neo.lspc_mode = v; break;
  case 0xC:                                           // interrupt acknowledge
    if (v & 2) neo.timer_pending = 0;
    if (v & 4) neo.vbl_pending = 0;
    if (cpu) update_ipl(cpu);
    break;
  default: break;                                     // timer: not used by this game
  }
}

// --- hardware map ---
uint32_t neo_rd8(uint32_t a) {
  a &= 0xFFFFFF;
  if (a < 0x100000) return a < neo_rom_size ? neo_rom[a ^ 1] : 0xFF;
  if (a < 0x200000) return ram8(a);
  switch (a & 0xFF0000) {
  case 0x300000: return (a & 1) ? 0xFF : (~neo.p1 & 0xFF);              // P1 / DIP switches (off)
  case 0x320000: return (a & 1) ? 0x07 : neo.sound_reply;               // coins and service released
  case 0x340000: return (a & 1) ? 0xFF : (~neo.p2 & 0xFF);
  case 0x380000: return (a & 1) ? 0xFF : (~neo.status_b & 0x0F) | 0x70; // bit 7 = 0: AES
  case 0x3C0000: { uint32_t w = lspc_read(a); return (a & 1) ? (w & 0xFF) : (w >> 8); }
  }
  if (a >= 0x400000 && a < 0x800000) {
    uint32_t w = neo_pal[neo.pal_bank][(a >> 1) & 0xFFF]; return (a & 1) ? (w & 0xFF) : (w >> 8);
  }
  if (a >= 0xD00000 && a < 0xE00000) return neo_sram[a & (NEO_SRAM_SIZE - 1)];
  return 0xFF;                                         // memory card, BIOS ROM: absent
}

uint32_t neo_rd16(uint32_t a) {
  a &= 0xFFFFFE;
  if (a >= 0x3C0000 && a < 0x3D0000) return lspc_read(a);
  if (a >= 0x400000 && a < 0x800000) return neo_pal[neo.pal_bank][(a >> 1) & 0xFFF];
  return (neo_rd8(a) << 8) | neo_rd8(a + 1);
}

void neo_wr8(uint32_t a, uint32_t v) {
  a &= 0xFFFFFF; v &= 0xFF;
  if (a >= 0x100000 && a < 0x200000) { wram8(a, v); return; }
  switch (a & 0xFF0000) {
  case 0x300000: return;                               // watchdog
  case 0x320000:
    if (!(a & 1)) {
      neo.sound_cmd = v; neo.sound_reply = v;          // the Z80 driver answers with the command
      if (v == 3) { neo.sound_prefix = 0; neo.sound_left = 0; }
      sound_byte(v);
      neo.sound_log[neo.sound_count++ & 63] = v;
#ifdef NEO_SOUND_TRACE
      neo_sound_trace(v);
#endif
    }
    return;
  case 0x3A0000:
    switch (a & 0x1F) {
    case 0x0B: neo.fix_cart = 0; break;
    case 0x1B: neo.fix_cart = 1; break;
    case 0x0F: if (neo.pal_bank != 1) { neo.pal_bank = 1; neo_platform_bank(); } break;
    case 0x1F: if (neo.pal_bank != 0) { neo.pal_bank = 0; neo_platform_bank(); } break;
    }
    return;
  case 0x3C0000: lspc_write(a, v | (v << 8)); return;  // a byte write puts the byte on both halves
  }
  if (a >= 0x400000 && a < 0x800000) {
    uint16_t *p = &neo_pal[neo.pal_bank][(a >> 1) & 0xFFF];
    *p = (a & 1) ? (*p & 0xFF00) | v : (*p & 0xFF) | (v << 8);
    neo_platform_color((a >> 1) & 0xFFF, *p); return;
  }
  if (a >= 0xD00000 && a < 0xE00000) { neo_sram[a & (NEO_SRAM_SIZE - 1)] = v; return; }
}

void neo_wr16(uint32_t a, uint32_t v) {
  a &= 0xFFFFFE; v &= 0xFFFF;
  if (a >= 0x3C0000 && a < 0x3D0000) { lspc_write(a, v); return; }
  if (a >= 0x400000 && a < 0x800000) {
    neo_pal[neo.pal_bank][(a >> 1) & 0xFFF] = v; neo_platform_color((a >> 1) & 0xFFF, v); return;
  }
  neo_wr8(a, v >> 8); neo_wr8(a + 1, v);
}

// --- BIOS replacement (AES console, Europe) ---
static uint32_t user_entry(void) { return rom32(0x124); }        // jmp USER in the cartridge header
static uint32_t player_start(void) { return rom32(0x12A); }

static uint32_t call_user(m68k_t *c, uint32_t request) {
  neo.user_request = request;
  wram8(BIOS_USER_REQUEST, request);
  wram8(BIOS_SYSTEM_MODE, 0);
  c->a[7] = BIOS_SSP; c->srh = 0x27;
  return user_entry();
}

static uint32_t rts(m68k_t *c) { uint32_t pc = mem32(c->a[7]); c->a[7] += 4; return pc; }

static uint32_t rte(m68k_t *c) {
  uint32_t sr = mem16(c->a[7]), pc = mem32(c->a[7] + 2);
  c->a[7] += 6;
  c->x = (sr >> 4) & 1; c->n = (sr >> 3) & 1; c->z = (sr >> 2) & 1; c->v = (sr >> 1) & 1; c->c = sr & 1;
  c->srh = (sr >> 8) & 0xA7;          // the game stays in supervisor mode
  return pc;
}

static void read_inputs(void) {
  for (int p = 0; p < 2; p++) {
    uint32_t b = BIOS_P1STATUS + 6 * p, cur = p ? neo.p2 : neo.p1, prev = ram8(b + 2);
    wram8(b, 1);                       // a normal joystick is connected
    wram8(b + 1, prev);
    wram8(b + 2, cur);
    wram8(b + 3, cur & ~prev);
    uint32_t timer = ram8(b + 5), rep = cur & ~prev;
    if (cur && cur == prev) { if (++timer >= 16) { rep = cur; timer = 12; } } else timer = 0;
    wram8(b + 4, rep); wram8(b + 5, timer);
  }
  uint32_t prev = ram8(BIOS_STATCURNT), cur = neo.status_b & 0x0F;
  wram8(BIOS_STATCURNT, cur);
  wram8(BIOS_STATCHANGE, cur & ~prev);
}

void neo_reset(m68k_t *c) {
  cpu = c;
  for (uint32_t i = 0; i < sizeof neo_ram; i++) neo_ram[i] = 0;
  for (uint32_t i = 0; i < 0x8800; i++) neo_vram[i] = 0;
  for (uint32_t i = 0; i < 0x1000; i++) neo_pal[0][i] = neo_pal[1][i] = 0;
  neo_state_t z = {0}; neo = z;
  neo.fix_cart = 1; neo.vram_mod = 1;
  for (int i = 0; i < 8; i++) { c->d[i] = 0; c->a[i] = 0; }
  c->x = c->n = c->z = c->v = c->c = 0; c->ipl = 0; c->osp = 0;
  wram8(BIOS_MVS_FLAG, 0);             // AES
  wram8(BIOS_COUNTRY_CODE, 2);         // Europe: "Bust-A-Move Again"
  // The settings of the game (soft DIP, $10FD84: its table for the country, pointed to by the
  // header at $116 + 4 x country): the factory values, as a new MVS or AES backup RAM gives them
  // (a byte per option from $10FD8A: high nibble the default, low nibble the number of choices)
  if (neo_rom_size >= 0x200) {
    uint32_t t = rom32(0x116 + 4 * 2);
    if (t + 32 <= neo_rom_size)
      for (int n = 0; n < 10; n++) {
        uint32_t b = neo_rom[(t + 22 + n) ^ 1];
        if (!b) break;
        wram8(0x10FD8A + n, b >> 4);
      }
  }
  for (uint32_t s = 0; s < 0x200; s++) { neo_vram[0x8000 + s] = 0x0FFF; neo_vram[0x8200 + s] = 0; }
  c->pc = call_user(c, 0);
}

// The main loop of the game (2AEC-2B24) runs its task scheduler again and again while it waits for
// the next frame; once a turn found no new frame, the next one would find none either: sleep.
void neo_idle(void) {
  static uint32_t seen;
  if (neo.frame == seen) neo_platform_wait(seen);
  seen = neo.frame;
}

// pads of players 3 and 4, read by the 4-player patches (patches/four.s: RAW3, RAW4)
#define RAW34 0x10F610

void neo_vblank(m68k_t *c) {
  neo.frame++;
  wram8(RAW34, neo.p3); wram8(RAW34 + 1, neo.p4); wram8(RAW34 + 2, neo.st34);
  neo.vbl_pending = 1;
  update_ipl(c);
}

// --- the sending of the sprite list, in C (4-player version, patches/sprites.s) ---
// The vertical-blank routine of the game ($D36-$E18) writes every sprite of the frame list into
// the video RAM word by word through the LSPC registers; with four fields this was 11 % of the
// time of the console. This is the same work from $D40 to $E12, writing the video RAM directly
// (with the address rule of the LSPC), leaving the LSPC registers as the original leaves them.
#define NATIVE_SPRITES 0xC00F00
static uint32_t nv_addr, nv_mod;                    // the LSPC address and modulo, as the original sets them
static inline void nv_put(uint32_t v) {
  uint32_t i = vram_index(nv_addr);
  neo_vram[i] = (uint16_t)v;
  if (i >= 0x7000 && i < 0x7500) neo_platform_fix((i - 0x7000) >> 5, i & 31, v);
  nv_addr = (nv_addr & 0x8000) | ((nv_addr + nv_mod) & 0x7FFF);
}
static inline uint32_t gw16(uint32_t a) {           // a word anywhere (sprite definitions are in ROM)
  a &= 0xFFFFFF;
  if ((a & 0xF00000) == 0x100000) return mem16(a);
  if (a < neo_rom_size) return *(const uint16_t *)(neo_rom + (a & ~1u));
  return neo_rd16(a) & 0xFFFF;
}
// $F5A (and $FD0 when bit 0 of +4 is set: the columns from right to left): a sprite of several
// tiles, d0 = (rows - 1) << 8 | (columns - 1)
static void nv_block(uint32_t a0, uint32_t *a1, uint32_t d0, uint32_t *d3, uint32_t *d4) {
  uint32_t cols = d0 & 0xFF, rows = (d0 >> 8) & 0xFF, flip = gw16(a0 + 4) & 1;
  uint32_t first = flip ? (*d3 + (cols << 6)) & 0xFFFF : *d3, saved = first;
  uint32_t x3 = first, scb2 = gw16(a0 + 10), d7 = gw16(a0 + 4);
  for (uint32_t col = 0; col <= cols; col++) {
    uint32_t d4v = *d4;
    if ((d4v & 0xFE00) == 0x8000) {                // SCB2-4 of a sprite: their words directly
      uint32_t s = d4v & 0x1FF;
      neo_vram[0x8000 + s] = (uint16_t)scb2;
      neo_vram[0x8200 + s] = (uint16_t)(col ? 0x40 : (gw16(a0 + 6) + rows) & 0xFFFF);
      if (!col) neo_vram[0x8400 + s] = (uint16_t)gw16(a0 + 8);
      nv_addr = (d4v & 0x8000) | ((d4v + (col ? 0x400 : 0x600)) & 0x7FFF); nv_mod = 0x200;
    } else {
      nv_addr = d4v; nv_mod = 0x200;
      nv_put(scb2);
      nv_put(col ? 0x40 : (gw16(a0 + 6) + rows) & 0xFFFF);
      if (!col) nv_put(gw16(a0 + 8));
    }
    uint32_t n = (rows + 1) * 2;
    if (x3 + n <= 0x7000 && *a1 < neo_rom_size) {  // the tiles (SCB1, below the fix layer) from the ROM
      const uint16_t *src = (const uint16_t *)(neo_rom + (*a1 & ~1u));
      uint16_t *dst = neo_vram + x3;
      for (uint32_t r = 0; r <= rows; r++, src += 2) { dst[2 * r] = src[0]; dst[2 * r + 1] = (uint16_t)(d7 ^ src[1]); }
      *a1 += 4 * (rows + 1);
      nv_addr = x3 + n; nv_mod = 1;
    } else {
      nv_addr = x3; nv_mod = 1;
      for (uint32_t r = 0; r <= rows; r++) {
        nv_put(gw16(*a1)); uint32_t d2 = gw16(*a1 + 2); *a1 += 4;
        nv_put(d7 ^ d2);
      }
    }
    x3 = flip ? (x3 - 0x40) & 0xFFFF : (x3 + 0x40) & 0xFFFF;
    *d4 = (*d4 + 1) & 0xFFFF;
  }
  *d3 = flip ? (saved + 0x40) & 0xFFFF : x3;
}
uint32_t neo_list_last;                             // last sprite written by the list (debug, bench)
static void native_sprites(m68k_t *c) {
  uint32_t a5 = c->a[5], d3 = 0x1D80, d4 = 0x8076, a0, a1, d0;
  wmem16(mem32(a5 - 29066), 0xFFFF);                // end of the frame list
  for (a0 = 0x1001CE; !(mem32(a0) & 0x80000000u); a0 += 12) {
    a1 = mem32(a0); d0 = gw16(a1); a1 += 2;
    if (d0) { nv_block(a0, &a1, d0, &d3, &d4); continue; }
    if (d3 + 2 <= 0x7000 && (d4 & 0xFE00) == 0x8000) {   // one tile: SCB1, then SCB2-4, directly
      uint32_t s = d4 & 0x1FF;
      neo_vram[d3] = (uint16_t)gw16(a1);
      neo_vram[d3 + 1] = (uint16_t)(gw16(a0 + 4) ^ gw16(a1 + 2));
      neo_vram[0x8000 + s] = (uint16_t)gw16(a0 + 10);
      neo_vram[0x8200 + s] = (uint16_t)gw16(a0 + 6);
      neo_vram[0x8400 + s] = (uint16_t)gw16(a0 + 8);
      nv_addr = (d4 & 0x8000) | ((d4 + 0x600) & 0x7FFF); nv_mod = 0x200;
    } else {
      nv_addr = d3; nv_mod = 1;
      nv_put(gw16(a1));
      nv_put(gw16(a0 + 4) ^ gw16(a1 + 2));
      nv_addr = d4; nv_mod = 0x200;
      nv_put(gw16(a0 + 10)); nv_put(gw16(a0 + 6)); nv_put(gw16(a0 + 8));
    }
    d3 = (d3 + 0x40) & 0xFFFF;
    d4 = (d4 + 1) & 0xFFFF;
  }
  wmem16(mem32(a5 - 29062), 0xFFFF);                // the second list: blocks only
  for (a0 = 0x100DFE; !(mem32(a0) & 0x80000000u); a0 += 12) {
    a1 = mem32(a0); d0 = gw16(a1); a1 += 2;
    nv_block(a0, &a1, d0, &d3, &d4);
  }
  neo_list_last = d4 - 1 - 0x8000;
  wmem32(a5 - 29066, 0x1001CE); wmem32(a5 - 29062, 0x100DFE);
  d0 = ((mem16(a5 - 11000) ? 0x8160 : 0x817D) - d4) & 0xFFFF;   // the vertical positions of the
  d4 = (d4 + 0x200) & 0xFFFF;                       // sprites left over: 0 (dbf: d0 + 1 words)
  nv_addr = d4; nv_mod = 1;
  for (uint32_t n = d0 + 1; n; n--) nv_put(0);
  neo.vram_addr = nv_addr; neo.vram_mod = nv_mod;
  c->d[3] = (c->d[3] & 0xFFFF0000u) | d3;           // (registers are restored by $E14 anyway)
}

// --- the band of players 3 and 4 (patches/band.s), in C: before the sprite list is sent, the entries
// the scheduler noted for tasks 7 and 8 move 256 lines down (player 3), and 256 lines down and 256
// pixels right (player 4): bit 15 of the words +6 and +8 of an entry (12 bytes), in both lists
#define NATIVE_BAND 0xC00E00
static void band_range(uint32_t s, uint32_t e, int right) {
  for (; s < e; s += 12) { wmem16(s + 6, mem16(s + 6) ^ 0x8000); if (right) wmem16(s + 8, mem16(s + 8) ^ 0x8000); }
}
static void native_band(m68k_t *c) {
  band_range(mem32(0x10F644), mem32(0x10F648), 0);     // S7, E7
  band_range(mem32(0x10F64C), mem32(0x10F650), 1);     // S8, E8
  band_range(mem32(0x10F674), mem32(0x10F678), 0);     // S7B, E7B (second list)
  band_range(mem32(0x10F67C), mem32(0x10F680), 1);     // S8B, E8B
  for (uint32_t a = 0x10F644; a < 0x10F654; a += 4) wmem32(a, 0);
  for (uint32_t a = 0x10F674; a < 0x10F684; a += 4) wmem32(a, 0);
  wmem32(0x10F640, 0x1001CE); wmem32(0x10F670, 0x100DFE);   // LAST, LAST2: the lists start again
  c->a[0] = 0x1001CE;                                       // lea $1001CE,a0, as the patched site did
  c->n = c->z = c->v = c->c = 0;                            // the flags the 68000 routine left
}

// --- the attack of a player (patches/rules.s, from $620CA; a4 the attacker, d0 its strength), shared
// among all its opponents still in the round (a field, not out): each gets strength / n, the rest
// goes one by one to the players after the attacker. As the original for each one: +share to its
// attacks to come (+56 of its structure), half of them into its counter ($10900A / $10900C for
// players 1 and 2, as the tables $620FC and $62104 give them). With two players the only opponent
// gets it all, in the same order: the RAM is as with the game.
#define NATIVE_ATTACK 0xC00E10
static inline uint32_t mem8(uint32_t a) { return neo_ram[(a & 0xFFFF) ^ 1]; }
static void native_attack(m68k_t *c) {
  static const uint32_t st[4] = {0x10D5D2, 0x10D668, 0x10F400, 0x10F500};
  static const uint32_t counter[4] = {0x10900A, 0x10900C, 0x10F628, 0x10F62A};
  uint32_t me = mem16(c->a[4] + 2) & 3, total = c->d[0] & 0xFFFF, a0 = c->a[0], d0 = c->d[0];
  int in[4], n = 0;
  for (int k = 0; k < 4; k++) {
    in[k] = k != (int)me && (k < 2 || mem8(0x10F65C + k - 2)) && !mem8(0x10F660 + k);
    n += in[k];
  }
  c->d[1] = (c->d[1] & 0xFFFF0000u) | me;
  if (n) {
    uint32_t share = total / n, rest = total % n;
    for (int i = 1; i < 4; i++) {
      int k = (me + i) & 3;
      if (!in[k]) continue;
      uint32_t v = share + (rest ? (rest--, 1) : 0);
      wmem16(st[k] + 56, mem16(st[k] + 56) + v);
      d0 = (d0 & 0xFFFF0000u) | ((uint32_t)((int16_t)mem16(st[k] + 56) >> 1) & 0xFFFF);
      a0 = counter[k]; wmem16(a0, d0);
#ifdef NEO_ATTACK_LOG
      printf("ATTACK frame %u by P%u (%u): P%d +%u\n", neo.frame, me + 1, total, k + 1, v);
#endif
    }
  }
  c->d[0] = d0; c->a[0] = a0;
}

uint32_t neo_bios(m68k_t *c, uint32_t pc) {
  if (pc >= 0xC00F10 && pc < 0xC01000) return neo_native(c, pc);   // the rewritten routines, first
  switch (pc) {
  case NATIVE_BAND:
    native_band(c);
    return rts(c);
  case NATIVE_ATTACK:
    native_attack(c);
    return rts(c);
  case NATIVE_SPRITES:
    native_sprites(c);
    return rts(c);
  case SYSTEM_INT1:                    // vertical blank while the BIOS owns the machine
    neo.vbl_pending = 0; update_ipl(c);
    wram8(BIOS_VBL_WAIT, 0);
    read_inputs();
    return rte(c);
  case SYSTEM_RETURN: {                // the game gives the machine back after a request
    uint32_t next = 2;
    if (neo.user_request == 0 && neo_rom[0x114 ^ 1] == 1) next = 1;   // the game's own eye catcher
    return call_user(c, next);
  }
  case SYSTEM_IO:
    read_inputs();
    if (ram8(BIOS_STATCHANGE) & 0x05) {   // start pressed: ask the game (PLAYER_START)
      wram8(BIOS_START_FLAG, ((ram8(BIOS_STATCHANGE) & 1) ? 1 : 0) | ((ram8(BIOS_STATCHANGE) & 4) ? 2 : 0));
      c->a[7] -= 4; wmem32(c->a[7], AFTER_START);
      return player_start();
    }
    return rts(c);
  case AFTER_START:
    return rts(c);                     // back to the caller of SYSTEM_IO
  case CREDIT_CHECK: case CREDIT_DOWN: // AES: no credits to count
    return rts(c);
  case FIX_CLEAR:
    for (uint32_t x = 0; x < 40; x++)        // tile $FF is empty (tile $20 of this game is not)
      for (uint32_t y = 0; y < 32; y++) { neo_vram[0x7000 + x * 32 + y] = 0x00FF; neo_platform_fix(x, y, 0x00FF); }
    return rts(c);
  case LSP_1ST:
    for (uint32_t s = 0; s < 0x200; s++) { neo_vram[0x8000 + s] = 0x0FFF; neo_vram[0x8200 + s] = 0; neo_vram[0x8400 + s] = 0; }
    return rts(c);
  }
  return neo_native(c, pc);                         // routines of the game rewritten in C (native.c)
}
