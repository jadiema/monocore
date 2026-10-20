// Neo Geo runtime: see neort.h. Addresses and BIOS variables follow the NeoGeoDev wiki.
#include "neort.h"

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
  for (uint32_t s = 0; s < 0x200; s++) { neo_vram[0x8000 + s] = 0x0FFF; neo_vram[0x8200 + s] = 0; }
  c->pc = call_user(c, 0);
}

void neo_vblank(m68k_t *c) {
  neo.frame++;
  neo.vbl_pending = 1;
  update_ipl(c);
}

uint32_t neo_bios(m68k_t *c, uint32_t pc) {
  switch (pc) {
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
  return 0;
}
