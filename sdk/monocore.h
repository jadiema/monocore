// MONOCORE-1 hardware definitions (see SPEC.md)
#pragma once
#include <stdint.h>

#define REG32(a) (*(volatile uint32_t *)(a))
#define REG16(a) (*(volatile uint16_t *)(a))
#define REG8(a)  (*(volatile uint8_t *)(a))

// ---------------------------------------------------------------- memory map
#define ROM_BASE      0x00000000u
#define WRAM_BASE     0x10000000u
#define NVRAM_BASE    0x18000000u
#define GPU_BASE      0x20000000u
#define PALETTE_BASE  0x20010000u
#define SPRITES_BASE  0x20020000u
#define TILEMAP_BASE  0x20040000u
#define LINETAB_BASE  0x20080000u
#define CHRRAM_BASE   0x200C0000u
#define APU_BASE      0x30000000u
#define IO_BASE       0x40000000u

// ---------------------------------------------------------------- video
#define DISP_CTRL   REG32(GPU_BASE + 0x000)
#define SCREEN_W    REG32(GPU_BASE + 0x004)
#define SCREEN_H    REG32(GPU_BASE + 0x008)
#define BG_COLOR    REG32(GPU_BASE + 0x00C)
#define LINE_CMP    REG32(GPU_BASE + 0x010)
#define VCOUNT      REG32(GPU_BASE + 0x014)
#define FRAME       REG32(GPU_BASE + 0x018)
#define GPU_STATUS  REG32(GPU_BASE + 0x01C)
#define SPR_CHR     REG32(GPU_BASE + 0x020)
#define SPR_COUNT   REG32(GPU_BASE + 0x024)
#define SPR_PEAK    REG32(GPU_BASE + 0x028)

#define DISP_ON       (1u << 0)
#define DISP_LAYER(n) (1u << (1 + (n)))
#define DISP_SPRITES  (1u << 5)
#define DISP_ROT(r)   ((uint32_t)(r) << 8)

#define LAYER_CTRL(n)    REG32(GPU_BASE + 0x100 + (n) * 0x20)
#define LAYER_CHR(n)     REG32(GPU_BASE + 0x104 + (n) * 0x20)
#define LAYER_SCROLLX(n) REG32(GPU_BASE + 0x108 + (n) * 0x20)
#define LAYER_SCROLLY(n) REG32(GPU_BASE + 0x10C + (n) * 0x20)

#define LC_TILE16      (1u << 0)
#define LC_8BPP        (1u << 1)
#define LC_MAPW(c)     ((uint32_t)(c) << 2)   // 0=32, 1=64, 2=128 tiles
#define LC_MAPH(c)     ((uint32_t)(c) << 4)
#define LC_LINESCROLL  (1u << 6)
#define LC_PRIO(p)     ((uint32_t)(p) << 8)

#define TILEMAP(n)     ((volatile uint32_t *)(TILEMAP_BASE + (n) * 0x4000))
#define TILE(t, pal)   ((uint32_t)(t) | ((uint32_t)(pal) << 16))
#define T_HFLIP        (1u << 24)
#define T_VFLIP        (1u << 25)
#define T_PRIO         (1u << 26)

typedef struct { int16_t dx, dy; } linescroll_t;
#define LINESCROLL(n)  ((volatile linescroll_t *)(LINETAB_BASE + (n) * 0x800))

#define PALETTE        ((volatile uint16_t *)PALETTE_BASE)
#define RGB555(r, g, b) ((uint16_t)((((r) >> 3) << 10) | (((g) >> 3) << 5) | ((b) >> 3)))

typedef struct {
  int16_t x, y;
  uint16_t tile;
  uint8_t pal, attr;
  uint8_t w, h;
  uint16_t reserved;
  uint16_t zoomx, zoomy;
} sprite_t;
#define SPRITES ((volatile sprite_t *)SPRITES_BASE)
#define SA_HFLIP   (1u << 0)
#define SA_VFLIP   (1u << 1)
#define SA_PRIO(p) ((uint8_t)((p) << 2))
#define SA_8BPP    (1u << 5)
#define SA_ON      (1u << 7)

// ---------------------------------------------------------------- sound
typedef struct {
  uint32_t start, length, loop, step;
  uint8_t vol_l, vol_r, format, pad0;
  uint32_t ctrl;
  uint32_t pos;
  uint32_t pad1;
} pcm_voice_t;
#define PCM ((volatile pcm_voice_t *)APU_BASE)
#define PCM_VOICES 8
#define PCM_S8 0
#define PCM_S16 1
#define PCM_ADPCM 2
#define PCM_NOLOOP 0xFFFFFFFFu
#define PCM_STEP(hz) ((uint32_t)(((uint64_t)(hz) << 16) / 48000))

typedef struct {
  uint32_t freq;      // 1/16 Hz
  uint8_t wave, duty, vol_l, vol_r;
  uint32_t pad[2];
} psg_chan_t;
#define PSG ((volatile psg_chan_t *)(APU_BASE + 0x400))
#define PSG_SQUARE 0
#define PSG_TRIANGLE 1
#define PSG_SAW 2
#define PSG_NOISE 3
#define MASTER_VOL REG32(APU_BASE + 0xF00)

// ---------------------------------------------------------------- I/O
#define IN_P1       REG32(IO_BASE + 0x00)
#define IN_P2       REG32(IO_BASE + 0x04)
#define IN_SYS      REG32(IO_BASE + 0x08)
#define DIPSW       REG32(IO_BASE + 0x0C)
#define IRQ_STATUS  REG32(IO_BASE + 0x10)
#define IRQ_ENABLE  REG32(IO_BASE + 0x14)
#define T0_RELOAD   REG32(IO_BASE + 0x20)
#define T0_CTRL     REG32(IO_BASE + 0x24)
#define T1_RELOAD   REG32(IO_BASE + 0x28)
#define T1_CTRL     REG32(IO_BASE + 0x2C)
#define CYCLE_LO    REG32(IO_BASE + 0x30)
#define CYCLE_HI    REG32(IO_BASE + 0x34)
#define DMA_SRC     REG32(IO_BASE + 0x40)
#define DMA_DST     REG32(IO_BASE + 0x44)
#define DMA_LEN     REG32(IO_BASE + 0x48)
#define DMA_CTRL    REG32(IO_BASE + 0x4C)
#define DEBUG_OUT   REG32(IO_BASE + 0x60)

#define IN_UP     (1u << 0)
#define IN_DOWN   (1u << 1)
#define IN_LEFT   (1u << 2)
#define IN_RIGHT  (1u << 3)
#define IN_B(n)   (1u << (3 + (n)))   // n = 1..6
#define IN_START  (1u << 10)
#define IN_COIN   (1u << 11)

#define IRQ_VBLANK (1u << 0)
#define IRQ_LINE   (1u << 1)
#define IRQ_TIMER0 (1u << 2)
#define IRQ_TIMER1 (1u << 3)
#define IRQ_DMA    (1u << 4)

#define CPU_HZ 50000000u

static inline void irq_global_enable(void) { __asm__ volatile("csrs mie, %0\n csrsi mstatus, 8" :: "r"(0x800)); }
static inline void irq_global_disable(void) { __asm__ volatile("csrci mstatus, 8"); }
static inline void wfi(void) { __asm__ volatile("wfi"); }

static inline void dma_copy(volatile void *dst, const void *src, uint32_t len) {
  DMA_SRC = (uint32_t)src; DMA_DST = (uint32_t)dst; DMA_LEN = len; DMA_CTRL = 1;
}
static inline void dma_fill(volatile void *dst, uint32_t pattern, uint32_t len) {
  DMA_SRC = pattern; DMA_DST = (uint32_t)dst; DMA_LEN = len; DMA_CTRL = 3;
}

// implemented by the program: called from the trap entry for each external interrupt
void irq_handler(void);
