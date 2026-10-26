# MONOCORE-1 — virtual console specification

Version 0.5 — October 2026 (4 pads, analog inputs, keyboard and mouse; single 50 MHz clock)

*English translation of `SPEC.md`; the French text is the reference.*

## 1. Principle

MONOCORE-1 is a frozen console, designed for a single bitstream on MiSTer (DE10-Nano, Cyclone V 5CSEBA6). It emulates no machine: arcade games are **translated** to it (static recompilation of the original CPU code to RV32IM, rewriting of the accesses to the video and sound hardware into the generic blocks described here, conversion of the graphics and samples).

Guiding choices:

- **A single fast CPU**: a 32-bit RISC-V at 50 MHz. It is the simplest target for a translator (LLVM knows it, regular instruction set) and it easily absorbs the main CPU *and* the sound CPU of an 8- or 16-bit arcade board (Z80, 6502, 6809, 68000 from 1 to 12 MHz).
- **"Superset" video**: four tile layers and 512 zoomable sprites cover what most 1978–1995 boards do (Namco, Sega System 16, Capcom CPS1, Neo Geo, Midway, Konami…). Rendering is done **line by line**, so raster effects (scroll changes during the frame) remain possible.
- **Generic sound**: 8 PCM voices (including OKI ADPCM) and 8 PSG channels, plus a slot reserved for a YM2151-compatible FM block.
- **No system**: no OS, no BIOS. The ARM processor of the DE10-Nano is only used by the MiSTer framework to load the ROM from the SD card and show the OSD; the console itself does not depend on it.

## 2. Processor

| Item | Value |
|---|---|
| Instruction set | RV32IM, machine mode only, no MMU |
| Core | Hazard3 (Apache 2.0), configured as RV32IM, fast multiplier, divider at 2 bits per cycle |
| Frequency | 50 MHz, single clock for the whole console (the Hazard3 core does not go beyond about 53 MHz on this FPGA) |
| Caches | 8 KB instruction and 4 KB read-only data, in front of the ROM (SDRAM); the WRAM is in internal memory, uncached |
| Interrupts | a single external line (MEI, `mcause` = 0x8000000B), see §6 |
| CSR | `mstatus` (MIE/MPIE), `mie` (MEIE), `mip` (MEIP), `mtvec` (direct mode), `mepc`, `mcause`, `mscratch`, `mcycle[h]`, `minstret[h]`, `mcountinhibit` (the counters may start inhibited: `crt0.S` enables them) |
| `wfi` | puts the CPU to sleep until an interrupt enabled in `mie`, even if `mstatus.MIE` = 0 |

Timing model of the simulator: 1 cycle per instruction, +1 for a load, +2 for a taken jump or branch, +2 for a multiplication, +16 for a division or remainder. It has been calibrated against the simulation of the core: a 2 to 14 % difference depending on the kind of code (0.94 instructions per cycle measured on a CRC32, about 47 million per second). It is an estimate: a program synchronises on interrupts, never on a number of cycles.

At reset: `pc` = `0x100` (fixed vector, right after the header), interrupts disabled, `DISP_CTRL` = 0 (display off). Reset clears the registers but **not the contents of the memories** (WRAM, palette, sprites, tilemaps, CHR RAM, registers of the sound voices and channels): on MiSTer, loading a new ROM leaves them as the previous game left them. The SDK start-up code (`sdk/crt0.S`) clears them by DMA before `main` (about 2 ms); the NVRAM is kept.

## 3. Memory map

| Address | Size | Contents | Access |
|---|---|---|---|
| `0x0000_0000` | 32 MB max | ROM (code, data, graphics, samples), in SDRAM (MiSTer module of 32 MB or more) | read |
| `0x1000_0000` | 128 KB | work RAM (WRAM), FPGA internal memory | read/write |
| `0x1800_0000` | 32 KB | NVRAM (scores, settings; saved) | read/write |
| `0x2000_0000` | 4 KB | video registers | r/w |
| `0x2001_0000` | 8 KB | palette: 4096 colours × 16 bits | r/w |
| `0x2002_0000` | 8 KB | sprite table: 512 × 16 bytes | r/w |
| `0x2004_0000` | 64 KB | tilemaps: 4 layers × 16 KB | r/w |
| `0x2008_0000` | 8 KB | per-line scroll tables: 4 × 2 KB | r/w |
| `0x200C_0000` | 128 KB | CHR RAM (graphics the game can modify) | r/w |
| `0x3000_0000` | 4 KB | sound registers | r/w |
| `0x4000_0000` | 4 KB | inputs, interrupts, timers, DMA | r/w |

All spaces accept 8-, 16- and 32-bit accesses, little-endian. Graphics are read by the video from ROM or CHR RAM, sound samples from ROM only (neither the WRAM nor the CHR RAM has a free port for them). "Base" registers simply hold a CPU address; a base outside these two areas disables the layer or the sprites, and a word read beyond the end of the area is 0 (transparent).

## 4. Video

### 4.1 Picture format

- Adjustable native resolution: width 224 to 384 pixels, height 192 to 288 lines (`SCREEN_W`, `SCREEN_H`, taken into account at the next frame). Default: 320 × 240.
- Total number of lines per frame: `SCREEN_H + 24` (24 lines of vertical blanking).
- A line lasts `4 × ⌊50 000 000 / (240 × total lines)⌋` CPU cycles (a whole number of pixels at 12.5 MHz), i.e. 3,156 cycles and 60.01 Hz with 240 lines (2,668 cycles with 288 lines, the shortest line). The height is taken into account at the start of each frame.
- Output rotation 0°, 90°, 180°, 270° for vertical games (Pac-Man, Galaga… are described as 224 × 288 then rotated by 90°). The rotation is done by the output scaler of the MiSTer framework.
- Colours: palette of 4096 RGB555 entries (`0RRRRRGGGGGBBBBB`, R in bits 14–10).

### 4.2 Registers (`0x2000_0000`)

| Offset | Name | Description |
|---|---|---|
| 0x000 | `DISP_CTRL` | bit 0 display; bits 1–4 layers 0–3; bit 5 sprites; bits 8–9 rotation (0, 90, 180, 270° clockwise) |
| 0x004 | `SCREEN_W` | visible width (224–384) |
| 0x008 | `SCREEN_H` | visible height (192–288) |
| 0x00C | `BG_COLOR` | palette index of the background colour |
| 0x010 | `LINE_CMP` | line that triggers the LINE interrupt (compared on 32 bits: an off-screen value, e.g. 0x7FFF, disables it) |
| 0x014 | `VCOUNT` | current line (read only); 0 to `SCREEN_H−1` visible |
| 0x018 | `FRAME` | frame counter (read only) |
| 0x01C | `STATUS` | bit 0 in vertical blanking; bit 1 sprite overflow during the previous frame |
| 0x020 | `SPR_CHR` | base address of the sprite graphics |
| 0x024 | `SPR_COUNT` | number of sprite table entries scanned (0–512) |
| 0x028 | `SPR_PEAK` | statistic: maximum number of sprites on one line during the previous frame |
| 0x100 + n×0x20 | `Ln_CTRL` | layer n: bit 0 16 × 16 tiles (otherwise 8 × 8); bit 1 8 bpp (otherwise 4 bpp); bits 2–3 map width (0 = 32, 1 = 64, 2 = 128 tiles); bits 4–5 height (same, width × height ≤ 4096); bit 6 per-line scroll; bits 8–9 layer priority (0–3) |
| +0x04 | `Ln_CHR` | base address of the layer graphics |
| +0x08 | `Ln_SCROLLX` | horizontal scroll (signed 16 bits) |
| +0x0C | `Ln_SCROLLY` | vertical scroll (signed 16 bits) |

### 4.3 Graphics

- 4 bpp: 2 pixels per byte, low nibble = left pixel. 8 × 8 tile = 32 bytes; 16 × 16 = 128 bytes; rows from top to bottom.
- 8 bpp: 1 byte per pixel. 8 × 8 tile = 64 bytes; 16 × 16 = 256 bytes.
- Pixel value 0 is always transparent.
- Final colour index: `(palette × 16 + pixel) & 0xFFF`. In 4 bpp a palette has 16 colours; in 8 bpp the palette field shifts the 256-colour window.

### 4.4 Tilemaps

One 32-bit entry per tile, stored row by row (`0x2004_0000 + layer × 0x4000`):

| Bits | Field |
|---|---|
| 15–0 | tile number (relative to `Ln_CHR`) |
| 23–16 | palette |
| 24 | horizontal flip |
| 25 | vertical flip |
| 26 | high priority (+1, see §4.7) |

The map wraps around in both directions.

### 4.5 When rendering happens

Each visible line y is computed at the start of its own period (when `VCOUNT` becomes y), then displayed during the next line; this one-line delay is invisible to software. The computation uses the state of the video registers and memories (palette included) **at the start of the line**: during this computation, a CPU or DMA write to the video block (`0x2000_0000`) is held until it ends. A write made during line y therefore always takes effect at line y + 1, whether it is a scroll, a palette colour or a tile. Reads are never delayed.

The duration of the computation depends on the contents: about 400 cycles for a line of plain text, a little under 900 for the menu of the test ROM (two layers, one zoomed 8 bpp sprite). It always stays below the duration of a line: the worst case measured (4 layers of 16 × 16 tiles in 8 bpp read from ROM and 8 bpp shrunk sprites at the maximum budget) takes about 1,550 cycles, for 2,668 cycles in the shortest line.

### 4.6 Per-line scroll

If bit 6 of `Ln_CTRL` is set, screen line y of layer n adds two offsets read at `0x2008_0000 + n × 0x800 + y × 4`: a horizontal `int16` then a vertical `int16`. This is what racing-game roads, waves and the "rowscroll" of Capcom or Konami boards use. For more exotic effects, the LINE interrupt lets you change any register between two lines.

### 4.7 Sprites

Table of 512 entries of 16 bytes (`0x2002_0000`):

| Offset | Size | Field |
|---|---|---|
| 0 | int16 | X (top-left corner, may be negative) |
| 2 | int16 | Y |
| 4 | u16 | first 8 × 8 cell (relative to `SPR_CHR`, in units of 32 bytes in 4 bpp, 64 in 8 bpp) |
| 6 | u8 | palette |
| 7 | u8 | bit 0 H flip; bit 1 V flip; bits 2–4 priority (0–7); bit 5 8 bpp; bit 7 active |
| 8 | u8 | width in cells (1–32) |
| 9 | u8 | height in cells (1–32) |
| 10 | u16 | reserved |
| 12 | u16 | horizontal zoom (8.8; 0x100 = 1.0; 0 means 1.0) |
| 14 | u16 | vertical zoom |

Cell (cx, cy) of a sprite is `tile + cy × width + cx`. A sprite is therefore up to 256 × 256 pixels before zoom.

**Zoom.** The sprite covers `⌊width × 8 × ZOOMX / 256⌋` × `⌊height × 8 × ZOOMY / 256⌋` pixels on screen. The screen pixel at (dx, dy) from the top-left corner shows source pixel `(⌊dx × 256 / ZOOMX⌋, ⌊dy × 256 / ZOOMY⌋)`, then the flips apply (`sx' = width × 8 − 1 − sx`). The hardware computes these divisions exactly.

**Priorities.** Each pixel gets a priority from 0 to 7: `2 × layer priority + tile priority bit` for a layer, the entry's priority for a sprite. The highest wins. On a tie: a sprite goes in front of a layer, a lower-numbered layer in front of a higher-numbered one, a lower-numbered sprite in front of a higher-numbered one.

**Hardware limits per line**: 64 sprites, 1280 sprite pixels displayed and 512 words of graphics. A sprite counts for `min(displayed width, words in a source row)` words, i.e. `width` in 4 bpp and `2 × width` in 8 bpp (in cells); this budget only hinders heavily shrunk sprites, which read one word per pixel. Beyond these limits, the highest-numbered sprites are dropped and bit 1 of `STATUS` becomes 1. These limits come from the line-buffer budget (see §9); they cover most original boards (16 to 96 sprites per line; the busiest, like the Neo Geo, top out at 96 sprites of 16 pixels, i.e. 1536 pixels, and may therefore lose sprites on the most crowded lines).

## 5. Sound

48 kHz stereo output, 16 bits. A sample is computed at cycle `⌈k × 50 000 000 / 48 000⌉` (k = 1, 2…) after reset, i.e. every 1,041.67 cycles. All the computation is integer and defined below to the bit: the simulator and the core produce exactly the same samples from the same register writes. Computing a sample usually takes under 350 cycles after its instant, and at most about 750 cycles (8 ADPCM voices at the maximum step), always less than the interval between two samples; a CPU or DMA write to the sound block during this computation waits for its end, so a write always applies whole to a given sample, never to only some of the voices.

### 5.1 PCM voices (8 voices, `0x3000_0000 + v × 0x20`, v = 0 to 7; `0x3000_0100`–`0x3000_01FF` is reserved)

| Offset | Name | Description |
|---|---|---|
| 0x00 | `START` | sample address, **in ROM** (beyond the end of the ROM, bytes are 0) |
| 0x04 | `LENGTH` | length in samples |
| 0x08 | `LOOP` | loop restart index; `0xFFFFFFFF` = no loop; ignored in ADPCM |
| 0x0C | `STEP` | playback step 16.16 (0x10000 = 48 kHz; 0x8000 = 24 kHz…) |
| 0x10 | `VOL_L` (u8) | left volume 0–255 |
| 0x11 | `VOL_R` (u8) | right volume |
| 0x12 | `FORMAT` (u8) | 0 = signed 8 bits; 1 = signed 16 bits; 2 and above = OKI MSM6295 ADPCM (4 bits, high nibble first) |
| 0x14 | `CTRL` | write 1 = start from the beginning, 0 = stop; read bit 0 = playing |
| 0x18 | `POS` | current position (read only) |

For each output sample, a playing voice:

1. stops if `POS ≥ LENGTH`;
2. reads its 16-bit value `s`: signed byte × 256 in 8 bits; little-endian signed word at `START + 2 × POS` in 16 bits; in ADPCM, decodes the nibbles up to the position (16 at most per output sample) and takes the 12-bit signal × 16;
3. adds `(s × VOL_L) >> 8` and `(s × VOL_R) >> 8` to the left and right sums (arithmetic shift);
4. advances: `sum = fraction + STEP`, `POS += sum >> 16`, `fraction = sum & 0xFFFF`;
5. if `POS ≥ LENGTH`: in PCM with a valid loop (`LOOP < LENGTH`), `POS −= LENGTH − LOOP`, then stops if `POS` is still outside the sample; otherwise stops.

OKI ADPCM is the format of the MSM6295, found on many 1990s boards: their samples load as they are. The MSM6295 does not loop, hence no loop in ADPCM.

### 5.2 PSG channels (8 channels, `0x3000_0400 + c × 0x10`)

| Offset | Name | Description |
|---|---|---|
| 0x00 | `FREQ` | frequency in 1/16 Hz (20 useful bits, up to 65,535 Hz) |
| 0x04 | `WAVE` (u8) | 0 square, 1 triangle, 2 sawtooth, 3 noise (other value: silence) |
| 0x05 | `DUTY` (u8) | square duty cycle out of 256 (0 means 128, i.e. 50 %) |
| 0x06 | `VOL_L` (u8) | left volume |
| 0x07 | `VOL_R` (u8) | right volume |

A channel whose two volumes or whose `FREQ` are zero is frozen (its phase does not advance). Otherwise, at each sample, its 32-bit phase advances by `⌊FREQ × 366 503 875 / 65 536⌋` (2³² / 768 000 in fixed point) modulo 2³², then, with `t = phase >> 16`:

| Wave | Value `s` |
|---|---|
| square | `+16 384` if `phase < DUTY × 2²⁴`, otherwise `−16 384` |
| triangle | `t − 16 384` if `t < 32 768`, otherwise `49 152 − t` |
| sawtooth | `(t >> 1) − 16 384` |
| noise | `±16 384` from a 15-bit LFSR (`bit = l₀ ⊕ l₁`, `l = (l >> 1) ∣ (bit << 14)`, starting at 1), advanced one step each time the phase crosses a half period |

then `(s × VOL) >> 8` is added to each side. These channels cover the AY-8910, SN76489 and Namco wave generators; the translator converts their period registers to `FREQ`.

### 5.3 Mixing and miscellaneous

Each side of the output is `⌊sum × MASTER_VOL × 77 / 65 536⌋`, clamped to ±32,767 (the 77/256 ≈ 0.3 factor leaves headroom for the 16 sources).

| Address | Name | Description |
|---|---|---|
| `0x3000_0F00` | `MASTER_VOL` | master volume 0–255 (default 255) |
| `0x3000_1000` | reserved for FM | YM2151-compatible block (address/data registers), planned for later; not simulated |

## 6. Inputs, interrupts, timers, DMA (`0x4000_0000`)

| Offset | Name | Description |
|---|---|---|
| 0x00 | `IN_P1` | bit 0 up, 1 down, 2 left, 3 right, 4–9 buttons 1–6, 10 start, 11 coin (1 = pressed) |
| 0x04 | `IN_P2` | same for player 2 |
| 0x08 | `IN_SYS` | bit 0 service, bit 1 test, bit 2 tilt |
| 0x0C | `DIP` | 32 bits of DIP switches, set in the OSD |
| 0x10 | `IRQ_STATUS` | bit 0 VBLANK, 1 LINE, 2 TIMER0, 3 TIMER1, 4 DMA; write 1 to acknowledge |
| 0x14 | `IRQ_ENABLE` | source mask |
| 0x20 | `T0_RELOAD` | timer 0 period in CPU cycles |
| 0x24 | `T0_CTRL` | bit 0 run |
| 0x28 | `T1_RELOAD` | timer 1 |
| 0x2C | `T1_CTRL` | |
| 0x30 | `CYCLE_LO` | cycle counter, low 32 bits |
| 0x34 | `CYCLE_HI` | high 32 bits |
| 0x40 | `DMA_SRC` | source (or 32-bit pattern in fill mode) |
| 0x44 | `DMA_DST` | destination |
| 0x48 | `DMA_LEN` | length in bytes |
| 0x4C | `DMA_CTRL` | write bit 0 start, bit 1 fill mode; read bit 0 busy |
| 0x60 | `DEBUG_OUT` | each byte written = one character on the debug console (absent from the final FPGA) |
| 0x70 | `HW_VERSION` | hardware version: 2 since players 3 and 4, analog and keyboard were added (1 before), 3 since the mouse was added |
| 0x80 | `IN_P3` | player 3, same format as `IN_P1` |
| 0x84 | `IN_P4` | player 4 |
| 0x90 + 4n | `ANALOG_Pn` | pad n (0-3 = players 1-4): left stick X (bits 7–0) and Y (15–8), right stick X (23–16) and Y (31–24), signed from −127 to +127, 0 at rest; positive Y is down |
| 0xA0 + 4n | `PADDLE_Pn` | paddle from 0 to 255 (bits 7–0); bits 31–16: spinner position, a 16-bit counter that accumulates its movements |
| 0xB0 + 4k | `KEY_STATE0`–`KEY_STATE7` | keyboard: 256 bits, 1 = key down; the key with PS/2 code (set 2) *c* is bit *i* = c (bit 7 of *c* at 0), + 128 for an extended code (E0 prefix); code 0x83 (F7) takes the free slot i = 0x02 |
| 0xD0 | `KEY_EVENT` | read: oldest unread keyboard event, bit 31 valid, bit 9 press (0 = release), bit 8 extended, bits 7–0 PS/2 code; write (any value): removes that event. Queue of 16 events; once full, new ones are lost |
| 0xD4 | `MOUSE_XY` | mouse: 16-bit position counters, X (bits 15–0, positive to the right) and Y (bits 31–16, positive downwards), accumulating the moves received (each between −256 and +255 per PS/2 packet) and wrapping to 0 on overflow; software gets the move by difference |
| 0xD8 | `MOUSE_BTN` | bit 0 left button, 1 right, 2 middle (1 = pressed); bits 15–8: wheel counter (8 bits, accumulates the signed notches, positive when turned away from the user, as in Linux); bit 31: at least one packet received since reset (a mouse is plugged in) |

Players 3 and 4, analog and keyboard come from the MiSTer framework: pads 3 and 4 from `hps_io`, analog sticks, paddles and spinners of pads 1 to 4, the board's USB keyboard (PS/2 codes). A ROM that wants to use them checks that `HW_VERSION` ≥ 2. The mouse (USB, or the board's PS/2 mouse) also comes from `hps_io` (`ps2_mouse`); a ROM that wants it checks that `HW_VERSION` ≥ 3. Reading these registers has no side effect: only writing to `KEY_EVENT` advances the queue.

VBLANK fires at the start of line `SCREEN_H`, LINE at the start of line `LINE_CMP`. The CPU interrupt line is `(IRQ_STATUS & IRQ_ENABLE) != 0`.

Timers are typically used to clock the translated sound driver at the original rate (often 60 to 250 Hz). DMA is used for ROM → VRAM transfers or to clear memory. The CPU stays suspended on the write to `DMA_CTRL` until the transfer ends, which avoids any race between the two. It transfers 32-bit words when the source (for a copy) and the destination are aligned, otherwise bytes. Measured cost: fill 1 cycle per word; copy 3 cycles per word, plus about 2.5 per word read from ROM (about 0.7 byte per cycle from ROM and 1.3 bytes per cycle between internal memories).

## 7. ROM format (`.mnc`)

The ROM is the raw image of the `0x0000_0000` space. The first 256 bytes form the header:

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | signature `MNC1` |
| 0x04 | 4 | header version (1) |
| 0x08 | 4 | entry point: must be `0x100` (the CPU always starts there) |
| 0x0C | 4 | ROM size |
| 0x10 | 4 | bits 0–1 default rotation, used by the front end (OSD, simulator) to orient the screen before the game writes `DISP_CTRL` |
| 0x14 | 4 | default DIP switches, offered by the front end |
| 0x20 | 32 | title (UTF-8, zero-padded) |
| 0x40 | 32 | origin (e.g. the name of the translated MAME game) |

On MiSTer, a ROM is loaded from the SD card (OSD, or `boot.rom` at start-up) or from a **cartridge** plugged into the USER port: one or two 8 MB SPI Flash chips (16 MB in total) holding the `.mnc` file as it is. The core checks its header then copies it into the SDRAM; for the game, both loads are identical (see `mister/LISEZMOI.md`).

The code starts at `0x100`. A translated ROM typically holds: the recompiled 68000/Z80 code, a small runtime library (interrupt handling, translation of hardware writes), the graphics converted to the §4.3 format and the samples in the §5.1 format.

## 8. Intended porting chain

1. Disassembly and static recompilation of the original CPU(s) to RV32IM (same method as for the JavaScript translation of Pigskin, with another output).
2. Replacement of the accesses to the original video and sound by writes to the MONOCORE blocks (found during translation, rewritten as calls to the runtime library).
3. Conversion of the graphics ROMs and samples.
4. Checking by comparing traces (RAM, picture, sound) with MAME, in the simulator then on the board.

## 9. Estimated FPGA budget (Cyclone V 5CSEBA6)

| Block | ALM | M10K (Kbit) | Note |
|---|---|---|---|
| RV32IM CPU + caches | ~4,000 | 120 | Hazard3, 3 stages; 8 KB + 4 KB caches |
| Video (4 layers, sprites, palette) | ~8,000 | ~700 | tilemaps 512, palette 64, sprites 64, line tables 64, line buffers |
| WRAM | — | 1,024 | 128 KB |
| CHR RAM | — | 1,024 | 128 KB |
| NVRAM and sound registers | — | 320 | 32 KB + 8 KB |
| Sound | ~2,500 | 20 | 8 PCM voices + 8 PSG, multipliers in DSP |
| SDRAM controller + arbitration | ~2,000 | 40 | ROM in SDRAM |
| MiSTer framework (scaler, OSD, HDMI) | ~10,000 | ~1,000 | |
| **Total** | **~27,000 / 41,910 (≈ 65 %)** | **~4,300 / 5,570** | |

SDRAM bandwidth (16 bits, ~200 MB/s): at worst, 4 layers in 8 bpp at 384 pixels + 512 words of sprites per line ≈ 3.5 KB per line × 18,700 lines/s ≈ 65 MB/s for the video, the rest for the CPU (smoothed by its caches) and the sound.

Rendering a line draws the sprites at one pixel per cycle: 1280 sprite pixels, plus the layers and the palette, fit in one line period (at least 2,668 cycles; the worst case measured takes about 1,550 cycles), hence the limits of §4.7.

## 10. Out of scope for v0.1

- FM block (YM2151, then YM2203/YM2612): slot reserved at `0x3000_1000`.
- Transparency and shadows (shadow/highlight of Sega and Capcom boards).
- Layer rotation and deformation (mode 7, Konami-style rotating sprites).
- Vector display.
