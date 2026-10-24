# Making your own MONOCORE-1 ROM

*English translation of `GUIDE.md`; the French text is the reference.*

A MONOCORE-1 ROM is a 32-bit RISC-V program (RV32IM) followed by its data. It talks to the hardware only through the registers described in the specification, which lets it run as it is in the simulator and on the FPGA core, without reprogramming the FPGA.

## 1. Installing the tools

You need three things, all free:

- **LLVM** (clang, ld.lld, llvm-objcopy): it can produce RISC-V without a dedicated toolchain.
  - Linux: `sudo apt install clang lld llvm` (or your distribution's equivalent).
  - macOS: `brew install llvm lld`, then add `$(brew --prefix llvm)/bin` to the `PATH`.
  - Windows: install LLVM from the project's website, or work in WSL with the Linux command.
- **Python 3** with **Pillow** (`pip install pillow`) to convert the images.
- **make**.

Check: `clang -print-targets | grep riscv32` must print a line.

## 2. Starting from the example

Copy the `examples/hello` folder under another name, then run `make` in it. The file `build/hello.mnc` is your ROM. Drop it on the simulator page (or click "Load a ROM").

The folder holds:

| File | Role |
|---|---|
| `hello.c` | the program |
| `data.S` | includes the converted graphics in the ROM (`.incbin`) |
| `robot.png` | a 16 × 16 pixel sprite |
| `Makefile` | name, title, sources and conversion rules |

The kit (`sdk/`) provides:

| File | Role |
|---|---|
| `monocore.h` | all the registers, as C macros and structures |
| `crt0.S` | the ROM header, RAM initialisation, the interrupt entry |
| `link.ld` | code and constants in ROM, variables and stack in WRAM |
| `mini.c` | `memset`, `memcpy`… which the compiler may need |
| `mkrom.py` | writes title, size, rotation and default DIP into the header |
| `font.png` | an 8 × 8 font ready to convert (ASCII 32 to 127) |
| `tools/png2chr.py` | PNG → graphics (`.chr`) and palette (`.pal`) |
| `tools/wav2pcm.py` | WAV → 8-bit, 16-bit or OKI ADPCM sample |

## 3. Structure of a program

A game fits in a loop synchronised on the vertical retrace:

```c
#include "monocore.h"

static volatile uint32_t vblanks;

void irq_handler(void) {                 // called by crt0.S on every interrupt
  if (IRQ_STATUS & IRQ_VBLANK) { IRQ_STATUS = IRQ_VBLANK; vblanks++; }
}

int main(void) {
  // ... palettes, layers, sprites ...
  DISP_CTRL = DISP_ON | DISP_LAYER(0) | DISP_SPRITES;
  IRQ_ENABLE = IRQ_VBLANK;
  irq_global_enable();
  for (;;) {
    uint32_t v = vblanks;
    while (vblanks == v) wfi();          // sleeps until the next frame
    // read IN_P1, move the objects, write the sprites, play the sounds
  }
}
```

`irq_handler` is mandatory: `crt0.S` calls it for each interrupt, after saving the registers.

## 4. Graphics

Convert your images with `png2chr.py`:

```sh
# sprites: 8x8 cells in row order, 16 colours (0 = transparent)
python3 sdk/tools/png2chr.py hero.png build/hero
# layer of 16x16 tiles with a map and duplicate tiles removed
python3 sdk/tools/png2chr.py level1.png build/level1 --tile 16 --map
# 256 colours
python3 sdk/tools/png2chr.py title.png build/title --bpp 8
```

Then include the files produced in `data.S` and declare them in C:

```c
extern const uint8_t hero_chr[], hero_pal[];
dma_copy(PALETTE + 3 * 16, hero_pal, 32);   // palette 3
SPR_CHR = (uint32_t)hero_chr;
```

A sprite of `w × h` cells uses cells `tile + cy × w + cx`. A 32 × 32 pixel image converted to a sprite therefore gives 4 × 4 cells, to be displayed with `w = 4, h = 4`.

For a layer, the map (`.map`) holds one tile number per cell; each tilemap entry is `TILE(number, palette)`, possibly with `T_HFLIP`, `T_VFLIP` or `T_PRIO`.

## 5. Sound

```sh
python3 sdk/tools/wav2pcm.py explosion.wav build/explosion.raw adpcm
# -> prints the number of samples and the STEP value matching the WAV's rate
```

```c
volatile pcm_voice_t *v = &PCM[3];
v->ctrl = 0;
v->start = (uint32_t)explosion_raw; v->length = 15872; v->loop = PCM_NOLOOP;
v->step = PCM_STEP(16000); v->format = PCM_ADPCM; v->vol_l = v->vol_r = 200;
v->ctrl = 1;                            // starts the voice
```

For music, the simplest is to clock a sequencer with timer 0 (for example at 240 Hz) and to drive the PSG and PCM channels from `irq_handler`, as the test ROM does.

## 6. Rules for staying compatible with the FPGA

1. **Synchronise on interrupts** (VBLANK, LINE, timers), never on a number of cycles. The simulator estimates CPU time; the real processor, with its caches, will not count exactly the same.
2. **Keep CPU headroom.** The simulator's monitor shows the load per frame: above 70 %, plan for optimisation.
3. **Respect the hardware limits**: 128 KB of WRAM (variables and stack), 64 sprites, 1280 pixels and 512 words of sprite graphics per line, 8 PCM voices, 4096 colours, 4 layers. The monitor reports sprite overflows.
4. **Only use documented registers.** `DEBUG_OUT` only exists in the simulator: on the FPGA, writes to it are ignored.
5. **Never dereference address 0 in C.** It is the start of the ROM, but the compiler treats it as a null pointer and may remove the code that reads it. Go through a symbol (`assets`, `hero_chr`…).
6. **Compile for RV32IM** (`-march=rv32im -mabi=ilp32`), without the C (compressed instructions) or F (floating point) extensions. For fractional computations, use fixed point.

## 7. Going further: the low-level "mc" SDK

This guide reaches the hardware through its registers, and the programmer's manual uses the `easy` library, which is deliberately simple (a text layer, a map, 64 sprites of 16 × 16, 32 × 32 or 64 × 64). In between, the **mc** SDK gives access to the whole console in C, without assembly, for complex games and ports. Just add a line to `project.txt`:

```
library = mc
```

then include `mc.h`. You then get:

- **interrupts in C**: `mc_on_vblank`, two timers (`mc_on_timer`), and up to 64 **raster effects** per frame (`mc_raster_add`), exact to the line;
- all of the **video**: resolution from 224 to 384 × 192 to 288 (`mc_video_mode`), four layers of 8 or 16 pixel tiles in 4 or 8 bpp (`mc_layer`), line-by-line scrolling, 128 KB of **CHR RAM** for graphics produced at run time (`mc_chr_upload`);
- **512 sprites** managed through a shadow table in WRAM, sent by DMA at each `mc_frame()`;
- direct access to PCM and PSG **sound** (`mc_pcm_play`, `mc_psg`), and a small sequencer (`mc_tune`);
- a **small libc**: `malloc`/`free`, `<string.h>`, `<stdlib.h>`, `<stdio.h>` (`printf` writes to the debug output), `<ctype.h>`, `<assert.h>`;
- 16.16 **fixed-point maths**: `mc_sin`, `mc_cos`, `mc_atan2`, `mc_isqrt`;
- the `data = file.bin` key in `project.txt`, to embed raw files (levels, data of a ported game);
- the keys `rotation = 0` to `3` (quarter turns of the picture, for vertical games) and `dip = 0x…` (the default DIP switches, offered by the MiSTer OSD and the simulator) in `project.txt`, written into the ROM header.

Skeleton:

```c
#include "mc.h"

int main(void) {
  mc_init();
  mc_text_layer(0, 3);                  // text on layer 0, in front of everything
  mc_text(0, 2, 2, "HELLO", 0);
  for (;;) {
    mc_frame();                         // next frame: sprites, pads, music
    if (mc_pad_pressed(1) & IN_START) { /* ... */ }
  }
}
```

The full reference is in `sdk/SDK_MC.md`. These examples use it:

| Example | What it shows |
|---|---|
| `examples/test_sdk` | the minimum to check: raster, timers, `malloc`, `printf` |
| `examples/boot` | the console's welcome screen: copper bars, logo, raster sky, scrolling text, music |
| `examples/horizon` | a pseudo-3D race: the road is chosen and shifted line by line, objects are zoomed according to their distance |
| `examples/nebula` | a complete shoot'em up: sprites and scenery generated at start-up, three-layer parallax, perspective ground, 54 raster effects, up to 233 sprites, aimed shots, zoomed particles |

## 8. On the MiSTer

Copy the core into `_Console` and your `.mnc` files into `games/MONOCORE` on the MiSTer's SD card, start the MONOCORE-1 core, then choose the ROM in the OSD menu (F12 → Load a ROM). The MiSTer framework loads the ROM into SDRAM; the bitstream does not change.

A file named exactly **`games/MONOCORE/boot.rom`** is loaded automatically when the core starts (this is how the welcome screen, `examples/boot`, starts). To make the console boot straight into the core, add `bootcore=lastexactcore` to `MiSTer.ini`, commenting out `bootcore_timeout`.
