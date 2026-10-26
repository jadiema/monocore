// MONOCORE-1 simulator core: RV32IM CPU, line-based GPU, APU, I/O.
// Works in the browser (window.Monocore) and in Node (module.exports).
// Behaviour follows SPEC.md v0.3.
(function (root) {
  'use strict';

  const CPU_HZ = 50000000;
  const FPS = 60;
  const AUDIO_HZ = 48000;
  const VBLANK_LINES = 24;
  const SPR_LINE_MAX = 64;
  const PCM_VOICES = 8;
  const SPR_PIX_MAX = 1280;
  const SPR_WORD_MAX = 512;    // graphics words per line (bound: min(width on screen, source row words))

  const WRAM_BASE = 0x10000000, WRAM_SIZE = 0x20000;      // 128 KB (FPGA block RAM)
  const RESET_PC = 0x100;
  const NVRAM_BASE = 0x18000000, NVRAM_SIZE = 0x8000;

  // Messages of the simulator, in French and English (Monocore.lang chooses; French by default)
  const MSG = {
    noMagic: ['Signature MNC1 absente : ce fichier n\'est pas une ROM MONOCORE-1', 'No MNC1 signature: this file is not a MONOCORE-1 ROM'],
    noHandler: ['Exception %1 à pc=0x%2 sans gestionnaire (mtvec = 0)', 'Exception %1 at pc=0x%2 with no handler (mtvec = 0)'],
    outOfMemory: ['Exécution hors mémoire à pc=0x%1', 'Execution outside memory at pc=0x%1'],
    misaligned: ['pc mal aligné : 0x%1', 'misaligned pc: 0x%1'],
  };
  function msg(key, ...args) {
    let s = MSG[key][Monocore.lang === 'en' ? 1 : 0];
    args.forEach((a, i) => { s = s.replace('%' + (i + 1), a); });
    return s;
  }

  // OKI MSM6295 ADPCM tables
  const OKI_STEP = [];
  for (let i = 0; i < 49; i++) OKI_STEP.push(Math.floor(16.0 * Math.pow(11.0 / 10.0, i)));
  const OKI_ADJ = [-1, -1, -1, -1, 2, 4, 6, 8];

  function mulhu(a, b) {
    a >>>= 0; b >>>= 0;
    const al = a & 0xffff, ah = a >>> 16, bl = b & 0xffff, bh = b >>> 16;
    const ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    const mid = (ll >>> 16) + (lh & 0xffff) + (hl & 0xffff);
    return (hh + (lh >>> 16) + (hl >>> 16) + Math.floor(mid / 65536)) | 0;
  }
  function mulh(a, b) { return (mulhu(a, b) - (a < 0 ? b : 0) - (b < 0 ? a : 0)) | 0; }
  function mulhsu(a, b) { return (mulhu(a, b) - (a < 0 ? b : 0)) | 0; }

  class Monocore {
    constructor(romBytes) {
      this.wram = new Uint8Array(WRAM_SIZE);
      this.wram32 = new Int32Array(this.wram.buffer);
      this.wramDV = new DataView(this.wram.buffer);
      this.nvram = new Uint8Array(NVRAM_SIZE);
      this.nvDV = new DataView(this.nvram.buffer);
      this.gpuRegs = new Uint8Array(0x1000); this.gpuDV = new DataView(this.gpuRegs.buffer);
      this.palette = new Uint8Array(0x2000); this.palDV = new DataView(this.palette.buffer);
      this.palRGBA = new Uint32Array(4096);
      this.sprites = new Uint8Array(0x2000); this.sprDV = new DataView(this.sprites.buffer);
      this.tilemaps = new Uint8Array(0x10000); this.tmDV = new DataView(this.tilemaps.buffer);
      this.tm32 = new Int32Array(this.tilemaps.buffer);
      this.lineTab = new Uint8Array(0x2000); this.ltDV = new DataView(this.lineTab.buffer);
      this.chrram = new Uint8Array(0x20000); this.chrDV = new DataView(this.chrram.buffer);
      this.apuRegs = new Uint8Array(0x2000); this.apuDV = new DataView(this.apuRegs.buffer);
      this.regs = new Int32Array(32);
      this.onDebug = null;      // callback(char)
      this.onError = null;      // callback(message)
      this.loadRom(romBytes);
    }

    loadRom(bytes) {
      let len = (bytes.length + 3) & ~3;
      this.rom = new Uint8Array(Math.max(len, 0x100));
      this.rom.set(bytes);
      this.romSize = this.rom.length;
      this.rom32 = new Int32Array(this.rom.buffer);
      this.romDV = new DataView(this.rom.buffer);
      const magic = String.fromCharCode(this.rom[0], this.rom[1], this.rom[2], this.rom[3]);
      if (magic !== 'MNC1') throw new Error(msg('noMagic'));
      const str = (o) => { let s = ''; for (let i = 0; i < 32 && this.rom[o + i]; i++) s += String.fromCharCode(this.rom[o + i]); return s; };
      this.header = {
        version: this.romDV.getUint32(4, true),
        entry: this.romDV.getUint32(8, true),
        size: this.romDV.getUint32(12, true),
        rotation: this.romDV.getUint32(16, true) & 3,
        dip: this.romDV.getUint32(20, true),
        title: new TextDecoder().decode(this.rom.subarray(0x20, 0x40)).replace(/\0+$/, ''),
        origin: new TextDecoder().decode(this.rom.subarray(0x40, 0x60)).replace(/\0+$/, ''),
      };
      this.reset();
    }

    reset() {
      this.regs.fill(0);
      this.pc = RESET_PC;   // fixed reset vector; the header entry field must be 0x100
      this.mstatus = 0; this.mie = 0; this.mtvec = 0; this.mepc = 0; this.mcause = 0; this.mscratch = 0; this.mtval = 0;
      this.cycles = 0; this.instret = 0;
      this.wfi = false; this.halted = false; this.brk = false;
      this.wram.fill(0);
      this.gpuRegs.fill(0); this.palette.fill(0); this.palRGBA.fill(0xff000000);
      this.sprites.fill(0); this.tilemaps.fill(0); this.lineTab.fill(0); this.chrram.fill(0);
      this.apuRegs.fill(0);
      this.gpuDV.setUint32(0x004, 320, true);
      this.gpuDV.setUint32(0x008, 240, true);
      this.apuDV.setUint32(0xF00, 255, true);
      // DISP_CTRL resets to 0: the header rotation only orients the display until the ROM sets it
      this.scrW = 320; this.scrH = 240;
      this.frame = 0; this.vcount = 0; this.inVblank = false;
      this.sprOverflow = false; this.sprOverflowCur = false; this.sprPeak = 0; this.sprPeakCur = 0;
      this.frameBuf = new Uint32Array(384 * 288);
      // I/O
      this.inputs = [0, 0, 0, 0, 0];          // IN_P1, IN_P2, IN_SYS, IN_P3, IN_P4
      this.analog = [0, 0, 0, 0];             // ANALOG_P1-4 (32 bits each)
      this.paddle = [0, 0, 0, 0];             // PADDLE_P1-4
      this.keyState = new Uint32Array(8);     // KEY_STATE0-7
      this.keyQueue = [];                     // KEY_EVENT queue (16 events at most)
      this.mouse = { x: 0, y: 0, wheel: 0, buttons: 0, seen: false };   // MOUSE_XY, MOUSE_BTN
      this.midiQueue = []; this.midiLost = false;                       // MIDI_IN (64 bytes at most)
      this.dip = this.header.dip >>> 0;
      this.irqStatus = 0; this.irqEnable = 0;
      this.timers = [{ reload: 0, on: false, count: 0 }, { reload: 0, on: false, count: 0 }];
      this.dma = { src: 0, dst: 0, len: 0 };
      // APU
      this.voices = [];
      for (let v = 0; v < PCM_VOICES; v++) this.voices.push({ playing: false, pos: 0, frac: 0, adIdx: 0, adSig: 0, adNext: 0, adVal: 0 });
      this.psg = [];
      for (let c = 0; c < 8; c++) this.psg.push({ phase: 0, lfsr: 1, noise: 1 });
      this.cycleAcc = 0; this.sampleCount = 0; this.nextSample = Math.ceil(CPU_HZ / AUDIO_HZ);
      this.stats = { busy: 0, frameCycles: 0, load: 0, instr: 0 };
      this.audioL = new Float32Array(2048); this.audioR = new Float32Array(2048); this.audioI = new Int16Array(4096); this.audioN = 0;
    }

    error(msg) {
      this.halted = true;
      if (this.onError) this.onError(msg);
    }

    // ---------------------------------------------------------------- memory
    // returns [Uint8Array, offset] for GPU/APU fetches, or null
    resolve(a) {
      a >>>= 0;
      // graphics and samples come from the ROM or the CHR RAM (the FPGA has no spare WRAM port)
      if (a < this.romSize) return [this.rom, a];
      if (a >= 0x200C0000 && a < 0x200E0000) return [this.chrram, a - 0x200C0000];
      return null;
    }

    read(a, size) { // size 1,2,4 ; returns unsigned for 1/2, int32 for 4
      a >>>= 0;
      const r = a >>> 28;
      if (r === 0) {
        if (a + size > this.romSize) return 0;
        if (size === 4) return (a & 3) ? this.romDV.getInt32(a, true) : this.rom32[a >>> 2];
        if (size === 1) return this.rom[a];
        return this.romDV.getUint16(a, true);
      }
      if (r === 1) {
        let dv, o;
        if (a >= WRAM_BASE && a + size <= WRAM_BASE + WRAM_SIZE) {
          o = a - WRAM_BASE;
          if (size === 4) return (o & 3) ? this.wramDV.getInt32(o, true) : this.wram32[o >>> 2];
          if (size === 1) return this.wram[o];
          return this.wramDV.getUint16(o, true);
        }
        if (a >= NVRAM_BASE && a + size <= NVRAM_BASE + NVRAM_SIZE) { dv = this.nvDV; o = a - NVRAM_BASE; return this.dvRead(dv, o, size); }
        return 0;
      }
      if (r === 2) {
        const o = a & 0xFFFFF;
        if ((a & 0x0FF00000) !== 0) return 0;
        if (o < 0x1000) return this.gpuRead(o, size);
        if (o >= 0x10000 && o < 0x12000) return this.dvRead(this.palDV, o - 0x10000, size);
        if (o >= 0x20000 && o < 0x22000) return this.dvRead(this.sprDV, o - 0x20000, size);
        if (o >= 0x40000 && o < 0x50000) return this.dvRead(this.tmDV, o - 0x40000, size);
        if (o >= 0x80000 && o < 0x82000) return this.dvRead(this.ltDV, o - 0x80000, size);
        if (o >= 0xC0000) return this.dvRead(this.chrDV, o - 0xC0000, size);
        return 0;
      }
      if (r === 3) {
        const o = a & 0x0FFFFFFF;
        if (o >= 0x2000) return 0;
        return this.apuRead(o, size);
      }
      if (r === 4) {
        const o = a & 0x0FFFFFFF;
        if (o >= 0x1000) return 0;
        const v = this.ioRead(o & ~3);
        const sh = (o & 3) * 8;
        if (size === 4) return v | 0;
        if (size === 2) return (v >>> sh) & 0xffff;
        return (v >>> sh) & 0xff;
      }
      return 0;
    }

    dvRead(dv, o, size) {
      if (o + size > dv.byteLength) return 0;
      if (size === 4) return dv.getInt32(o, true);
      if (size === 2) return dv.getUint16(o, true);
      return dv.getUint8(o);
    }
    dvWrite(dv, o, size, v) {
      if (o + size > dv.byteLength) return;
      if (size === 4) dv.setInt32(o, v, true);
      else if (size === 2) dv.setUint16(o, v & 0xffff, true);
      else dv.setUint8(o, v & 0xff);
    }

    write(a, size, v) {
      a >>>= 0;
      const r = a >>> 28;
      if (r === 1) {
        if (a >= WRAM_BASE && a + size <= WRAM_BASE + WRAM_SIZE) {
          const o = a - WRAM_BASE;
          if (size === 4 && !(o & 3)) this.wram32[o >>> 2] = v;
          else this.dvWrite(this.wramDV, o, size, v);
        } else if (a >= NVRAM_BASE && a + size <= NVRAM_BASE + NVRAM_SIZE) {
          this.dvWrite(this.nvDV, a - NVRAM_BASE, size, v); this.nvDirty = true;
        }
        return;
      }
      if (r === 2) {
        const o = a & 0xFFFFF;
        if ((a & 0x0FF00000) !== 0) return;
        if (o < 0x1000) { this.dvWrite(this.gpuDV, o, size, v); return; }
        if (o >= 0x10000 && o < 0x12000) {
          const p = o - 0x10000;
          this.dvWrite(this.palDV, p, size, v);
          const first = p >>> 1, last = (p + size - 1) >>> 1;
          for (let i = first; i <= last && i < 4096; i++) this.updatePal(i);
          return;
        }
        if (o >= 0x20000 && o < 0x22000) { this.dvWrite(this.sprDV, o - 0x20000, size, v); return; }
        if (o >= 0x40000 && o < 0x50000) { this.dvWrite(this.tmDV, o - 0x40000, size, v); return; }
        if (o >= 0x80000 && o < 0x82000) { this.dvWrite(this.ltDV, o - 0x80000, size, v); return; }
        if (o >= 0xC0000) { this.dvWrite(this.chrDV, o - 0xC0000, size, v); return; }
        return;
      }
      if (r === 3) {
        const o = a & 0x0FFFFFFF;
        if (o < 0x2000) this.apuWrite(o, size, v);
        return;
      }
      if (r === 4) {
        const o = a & 0x0FFFFFFF;
        if (o >= 0x1000) return;
        if (size === 4) this.ioWrite(o & ~3, v | 0);
        else {
          // narrow writes to I/O: merge into the 32-bit register
          const base = o & ~3, sh = (o & 3) * 8, mask = (size === 2 ? 0xffff : 0xff) << sh;
          const old = (base === 0x10) ? 0 : this.ioRead(base);
          this.ioWrite(base, (old & ~mask) | ((v << sh) & mask));
        }
      }
    }

    updatePal(i) {
      const c = this.palDV.getUint16(i * 2, true);
      let r = (c >>> 10) & 31, g = (c >>> 5) & 31, b = c & 31;
      r = (r << 3) | (r >>> 2); g = (g << 3) | (g >>> 2); b = (b << 3) | (b >>> 2);
      this.palRGBA[i] = (0xff000000 | (b << 16) | (g << 8) | r) >>> 0;
    }

    gpuRead(o, size) {
      const w = o & ~3;
      let v;
      if (w === 0x014) v = this.vcount;
      else if (w === 0x018) v = this.frame;
      else if (w === 0x01C) v = (this.inVblank ? 1 : 0) | (this.sprOverflow ? 2 : 0);
      else if (w === 0x028) v = this.sprPeak;
      else return this.dvRead(this.gpuDV, o, size);
      const sh = (o & 3) * 8;
      if (size === 4) return v | 0;
      return (v >>> sh) & (size === 2 ? 0xffff : 0xff);
    }

    ioRead(o) {
      switch (o) {
        case 0x00: return this.inputs[0];
        case 0x04: return this.inputs[1];
        case 0x08: return this.inputs[2];
        case 0x0C: return this.dip | 0;
        case 0x10: return this.irqStatus;
        case 0x14: return this.irqEnable;
        case 0x20: return this.timers[0].reload;
        case 0x24: return this.timers[0].on ? 1 : 0;
        case 0x28: return this.timers[1].reload;
        case 0x2C: return this.timers[1].on ? 1 : 0;
        case 0x30: return Math.floor(this.cycles) | 0;
        case 0x34: return Math.floor(this.cycles / 4294967296) | 0;
        case 0x40: return this.dma.src;
        case 0x44: return this.dma.dst;
        case 0x48: return this.dma.len;
        case 0x4C: return 0;
        case 0x70: return 0x00000004; // hardware version 4 (2: pads 3-4, analog, keyboard; 3: mouse; 4: MIDI)
        case 0xDC: return ((this.midiQueue.length ? 0x80000000 : 0) | (this.midiLost ? 0x40000000 : 0) |
                           (this.midiQueue.length << 16) | (this.midiQueue.length ? this.midiQueue[0] : 0)) | 0;
        case 0xD4: return ((this.mouse.y & 0xFFFF) << 16) | (this.mouse.x & 0xFFFF);
        case 0xD8: return ((this.mouse.seen ? 0x80000000 : 0) | ((this.mouse.wheel & 0xFF) << 8) | (this.mouse.buttons & 7)) | 0;
        case 0x80: return this.inputs[3];
        case 0x84: return this.inputs[4];
        case 0xD0: return this.keyQueue.length ? (0x80000000 | this.keyQueue[0]) | 0 : 0;
      }
      if (o >= 0x90 && o < 0xA0) return this.analog[(o - 0x90) >> 2] | 0;
      if (o >= 0xA0 && o < 0xB0) return this.paddle[(o - 0xA0) >> 2] | 0;
      if (o >= 0xB0 && o < 0xD0) return this.keyState[(o - 0xB0) >> 2] | 0;
      return 0;
    }

    // a key of the keyboard goes down or up: code = PS/2 set 2 code, ext = E0 prefix
    // one mouse packet: moves in counts (dy positive downwards), buttons bit 0 left, 1 right, 2 middle
    // one byte received on the MIDI input
    midiByte(b) { if (this.midiQueue.length < 64) this.midiQueue.push(b & 0xFF); else this.midiLost = true; }
    mouseEvent(dx, dy, buttons, wheel) {
      // 9-bit PS/2 moves: X from -256 to 255, Y sent upwards from -256 to 255 (so -255 to 256 downwards)
      const c = (v, lo, hi) => Math.max(lo, Math.min(hi, Math.round(v)));
      const m = this.mouse;
      m.x = (m.x + c(dx, -256, 255)) & 0xFFFF; m.y = (m.y + c(dy, -255, 256)) & 0xFFFF;
      m.wheel = (m.wheel + Math.max(-128, Math.min(127, wheel | 0))) & 0xFF;
      m.buttons = buttons & 7; m.seen = true;
    }
    keyEvent(code, ext, down) {
      code &= 0xff;
      const i = code === 0x83 ? 0x02 : ((ext ? 0x80 : 0) | (code & 0x7f));
      if (down) this.keyState[i >> 5] |= 1 << (i & 31); else this.keyState[i >> 5] &= ~(1 << (i & 31));
      if (this.keyQueue.length < 16) this.keyQueue.push((down ? 0x200 : 0) | (ext ? 0x100 : 0) | code);
    }

    ioWrite(o, v) {
      switch (o) {
        case 0x10: this.irqStatus &= ~v; this.brk = true; break;
        case 0x14: this.irqEnable = v & 0x1f; this.brk = true; break;
        case 0x20: case 0x28: { const t = this.timers[(o - 0x20) >> 3]; t.reload = v >>> 0; t.count = t.reload; break; }
        case 0x24: case 0x2C: { const t = this.timers[(o - 0x24) >> 3]; const on = !!(v & 1); if (on && !t.on) t.count = t.reload; t.on = on; this.brk = true; break; }
        case 0x40: this.dma.src = v; break;
        case 0x44: this.dma.dst = v; break;
        case 0x48: this.dma.len = v >>> 0; break;
        case 0x4C: if (v & 1) this.runDma(!!(v & 2)); break;
        case 0x60: if (this.onDebug) this.onDebug(String.fromCharCode(v & 0xff)); break;
        case 0xD0: this.keyQueue.shift(); break;
        case 0xDC: this.midiQueue.shift(); this.midiLost = false; break;
      }
    }

    runDma(fill) {
      const len = this.dma.len >>> 0;
      let src = this.dma.src >>> 0, dst = this.dma.dst >>> 0;
      if (fill) {
        const pat = this.dma.src | 0;
        let i = 0;
        for (; i + 4 <= len; i += 4) this.write(dst + i, 4, pat);
        for (; i < len; i++) this.write(dst + i, 1, pat >>> ((i & 3) * 8));
      } else {
        let i = 0;
        if (!((src | dst) & 3)) for (; i + 4 <= len; i += 4) this.write(dst + i, 4, this.read(src + i, 4));
        for (; i < len; i++) this.write(dst + i, 1, this.read(src + i, 1));
      }
      // the CPU waits for the transfer. Cost measured on the RTL (rtl/mc_core.v): a fill writes one
      // word or byte per cycle, a copy takes 3 cycles per word or byte, plus about 2.5 more per word
      // read from ROM (cache line fills from the SDRAM)
      const words = ((fill || !((src | dst) & 3)) ? Math.floor(len / 4) : 0), rest = len - words * 4;
      const n = words + rest;
      this.cycles += fill ? n : 3 * n + (src < this.romSize ? Math.floor(5 * words / 2) + rest : 0);
      this.irqStatus |= 0x10; this.brk = true;
    }

    // ---------------------------------------------------------------- APU
    apuRead(o, size) {
      if (o < PCM_VOICES * 0x20) {
        const v = o >> 5, f = o & 0x1f & ~3;
        if (f === 0x14) { const x = this.voices[v].playing ? 1 : 0; return size === 4 ? x : (o & 3) ? 0 : x; }
        if (f === 0x18) { const x = this.voices[v].pos; return size === 4 ? x | 0 : (x >>> ((o & 3) * 8)) & (size === 2 ? 0xffff : 0xff); }
      }
      return this.dvRead(this.apuDV, o, size);
    }

    apuWrite(o, size, v) {
      this.dvWrite(this.apuDV, o, size, v);
      if (o < PCM_VOICES * 0x20) {
        const vi = o >> 5, f = o & 0x1f;
        if (f <= 0x14 && f + size > 0x14) {
          const voice = this.voices[vi];
          if (this.apuRegs[(vi << 5) + 0x14] & 1) this.keyOn(vi); else voice.playing = false;
        }
      }
    }

    keyOn(vi) {
      const vc = this.voices[vi];
      vc.playing = true; vc.pos = 0; vc.frac = 0;
      vc.adIdx = 0; vc.adSig = 0; vc.adNext = 0; vc.adVal = 0;
    }

    // Sound engine, integer arithmetic (identical to rtl/mc_apu.v). Samples are read from the ROM only.
    romByte(a) { a >>>= 0; return a < this.romSize ? this.rom[a] : 0; }

    pcmSample(vi, vc) {             // 16-bit signed sample at the current position
      const base = vi << 5, dv = this.apuDV;
      const start = dv.getUint32(base, true);
      const fmt = this.apuRegs[base + 0x12];
      if (fmt === 0) return ((this.romByte(start + vc.pos) << 24) >> 24) * 256;
      if (fmt === 1) {
        const a = start + vc.pos * 2;
        const x = this.romByte(a) | (this.romByte(a + 1) << 8);
        return (x << 16) >> 16;
      }
      // OKI ADPCM (any format >= 2): decode up to the position, at most 16 nibbles per output sample
      for (let k = 0; k < 16 && vc.adNext <= vc.pos; k++) {
        const byte = this.romByte(start + Math.floor(vc.adNext / 2));
        const n = (vc.adNext & 1) ? (byte & 15) : (byte >> 4);
        const step = OKI_STEP[vc.adIdx];
        let diff = ((2 * (n & 7) + 1) * step) >> 3;
        if (n & 8) diff = -diff;
        vc.adSig = Math.max(-2048, Math.min(2047, vc.adSig + diff));
        vc.adIdx = Math.max(0, Math.min(48, vc.adIdx + OKI_ADJ[n & 7]));
        vc.adNext++;
      }
      return vc.adSig * 16;
    }

    mixSample() {
      let L = 0, R = 0;
      const dv = this.apuDV, regs = this.apuRegs;
      for (let vi = 0; vi < PCM_VOICES; vi++) {
        const vc = this.voices[vi];
        if (!vc.playing) continue;
        const base = vi << 5;
        const len = dv.getUint32(base + 4, true);
        if (vc.pos >= len) { vc.playing = false; continue; }
        const smp = this.pcmSample(vi, vc);
        L += (smp * regs[base + 0x10]) >> 8;
        R += (smp * regs[base + 0x11]) >> 8;
        const sum = vc.frac + dv.getUint32(base + 0x0C, true);
        vc.pos += Math.floor(sum / 65536);
        vc.frac = sum % 65536;
        if (vc.pos >= len) {
          // loop: one jump back by the loop length (PCM only); otherwise, or if still past the end, stop
          const loop = dv.getUint32(base + 8, true);
          if (regs[base + 0x12] < 2 && loop !== 0xFFFFFFFF && loop < len) {
            vc.pos -= len - loop;
            if (vc.pos >= len) vc.playing = false;
          } else vc.playing = false;
        }
      }
      for (let c = 0; c < 8; c++) {
        const base = 0x400 + c * 0x10;
        const vl = regs[base + 6], vr = regs[base + 7];
        if (!vl && !vr) continue;
        const freq = dv.getUint32(base, true) & 0xFFFFF;      // 1/16 Hz, 20 bits
        if (!freq) continue;
        const ch = this.psg[c];
        const inc = Math.floor(freq * 366503875 / 65536);   // 2^32 / (16 * 48000) in 16.16 fixed point
        const old = ch.phase;
        const wave = regs[base + 4];
        if (wave === 3 && Math.floor((old + inc) / 2147483648) !== Math.floor(old / 2147483648)) {
          // noise: new random value at each half period
          const bit = (ch.lfsr ^ (ch.lfsr >> 1)) & 1;
          ch.lfsr = (ch.lfsr >> 1) | (bit << 14);
          ch.noise = (ch.lfsr & 1) ? 1 : -1;
        }
        ch.phase = (old + inc) % 4294967296;
        const t = Math.floor(ch.phase / 65536);
        let smp = 0;
        if (wave === 0) smp = ch.phase < (regs[base + 5] || 128) * 16777216 ? 16384 : -16384;
        else if (wave === 1) smp = t < 32768 ? t - 16384 : 49152 - t;
        else if (wave === 2) smp = (t >> 1) - 16384;
        else if (wave === 3) smp = ch.noise * 16384;
        L += (smp * vl) >> 8;
        R += (smp * vr) >> 8;
      }
      const m = regs[0xF00];
      let oL = Math.floor(L * m * 77 / 65536), oR = Math.floor(R * m * 77 / 65536);
      oL = Math.max(-32768, Math.min(32767, oL));
      oR = Math.max(-32768, Math.min(32767, oR));
      const n = this.audioN++;
      if (n < this.audioL.length) { this.audioL[n] = oL / 32768; this.audioR[n] = oR / 32768; this.audioI[2 * n] = oL; this.audioI[2 * n + 1] = oR; }
      if (this.onSample) this.onSample(oL, oR);
    }

    // ---------------------------------------------------------------- GPU
    renderLine(y) {
      const g = this.gpuDV, W = this.scrW;
      const out = this.frameBuf, rowOff = y * W;
      const ctrl = g.getUint32(0, true);
      const pal = this.palRGBA;
      if (!(ctrl & 1)) { for (let x = 0; x < W; x++) out[rowOff + x] = 0xff000000; return; }
      const col = this.lineCol || (this.lineCol = new Uint16Array(384));
      const pri = this.linePri || (this.linePri = new Int8Array(384));
      col.fill(g.getUint32(0x0C, true) & 0xfff, 0, W);
      pri.fill(-1, 0, W);

      for (let l = 3; l >= 0; l--) {
        if (!(ctrl & (2 << l))) continue;
        const lb = 0x100 + l * 0x20;
        const lc = g.getUint32(lb, true);
        const ts = (lc & 1) ? 16 : 8, tsh = (lc & 1) ? 4 : 3;
        const b8 = (lc >> 1) & 1;
        const mw = 32 << Math.min(2, (lc >> 2) & 3), mh = 32 << Math.min(2, (lc >> 4) & 3);
        if (mw * mh > 4096) continue;
        const lprio = ((lc >> 8) & 3) * 2;
        const mem = this.resolve(g.getUint32(lb + 4, true));
        if (!mem) continue;
        const data = mem[0], dbase = mem[1];
        let sx = g.getInt16(lb + 8, true), sy = g.getInt16(lb + 12, true);
        if (lc & 0x40) { sx += this.ltDV.getInt16(l * 0x800 + y * 4, true); sy += this.ltDV.getInt16(l * 0x800 + y * 4 + 2, true); }
        const pw = mw * ts, ph = mh * ts;
        const py = (((y + sy) % ph) + ph) % ph;
        const trow = py >> tsh, tyy = py & (ts - 1);
        const tbytes = (ts * ts) >> (b8 ? 0 : 1);
        const tmBase = (l * 0x4000) >> 2;
        const tm = this.tm32;
        let px = (((sx) % pw) + pw) % pw;
        for (let x = 0; x < W; x++, px++) {
          if (px >= pw) px -= pw;
          const e = tm[tmBase + trow * mw + (px >> tsh)];
          let tx = px & (ts - 1), ty = tyy;
          if (e & 0x1000000) tx = ts - 1 - tx;
          if (e & 0x2000000) ty = ts - 1 - ty;
          const pa = dbase + (e & 0xffff) * tbytes;
          const pi = ty * ts + tx;
          let v;
          if (b8) v = data[pa + pi];
          else { v = data[pa + (pi >> 1)]; v = (pi & 1) ? (v >> 4) : (v & 15); }
          if (!v) continue;
          const p = lprio + ((e >> 26) & 1);
          if (p >= pri[x]) { pri[x] = p; col[x] = (((e >> 16) & 0xff) * 16 + v) & 0xfff; }
        }
      }

      if (ctrl & 0x20) this.renderSpritesLine(y, col, pri, W);

      for (let x = 0; x < W; x++) out[rowOff + x] = pal[col[x]];
    }

    renderSpritesLine(y, col, pri, W) {
      const s = this.sprDV, g = this.gpuDV;
      const count = Math.min(512, g.getUint32(0x24, true));
      const mem = this.resolve(g.getUint32(0x20, true));
      if (!mem) return;
      const data = mem[0], dbase = mem[1];
      const sel = this.sprSel || (this.sprSel = new Int16Array(SPR_LINE_MAX));
      let n = 0, pix = 0, words = 0;
      for (let i = 0; i < count; i++) {
        const o = i * 16;
        const attr = this.sprites[o + 7];
        if (!(attr & 0x80)) continue;
        const sy = s.getInt16(o + 2, true);
        const h = this.sprites[o + 9] || 1;
        const zy = s.getUint16(o + 14, true) || 256;
        const dh = Math.floor(h * 8 * zy / 256);
        if (y < sy || y >= sy + dh) continue;
        const w = this.sprites[o + 8] || 1;
        const zx = s.getUint16(o + 12, true) || 256;
        const dw = Math.floor(w * 8 * zx / 256);
        const wd = Math.min(dw, (attr & 0x20) ? w * 2 : w);
        if (n >= SPR_LINE_MAX || pix + dw > SPR_PIX_MAX || words + wd > SPR_WORD_MAX) { this.sprOverflowCur = true; break; }
        sel[n++] = i; pix += dw; words += wd;
      }
      if (n > this.sprPeakCur) this.sprPeakCur = n;
      for (let k = n - 1; k >= 0; k--) {
        const o = sel[k] * 16;
        const sx = s.getInt16(o, true), sy = s.getInt16(o + 2, true);
        const tile = s.getUint16(o + 4, true), palb = this.sprites[o + 6] * 16;
        const attr = this.sprites[o + 7];
        const w = this.sprites[o + 8] || 1, h = this.sprites[o + 9] || 1;
        const zx = s.getUint16(o + 12, true) || 256, zy = s.getUint16(o + 14, true) || 256;
        const dw = Math.floor(w * 8 * zx / 256);
        const prio = (attr >> 2) & 7, b8 = attr & 0x20;
        const cbytes = b8 ? 64 : 32;
        let srcY = Math.floor((y - sy) * 256 / zy);
        if (attr & 2) srcY = h * 8 - 1 - srcY;
        const crow = srcY >> 3, cy = srcY & 7;
        let x0 = Math.max(0, sx), x1 = Math.min(W, sx + dw);
        for (let x = x0; x < x1; x++) {
          let srcX = Math.floor((x - sx) * 256 / zx);
          if (attr & 1) srcX = w * 8 - 1 - srcX;
          const cell = tile + crow * w + (srcX >> 3);
          const pi = cy * 8 + (srcX & 7);
          const pa = dbase + cell * cbytes;
          let v;
          if (b8) v = data[pa + pi];
          else { v = data[pa + (pi >> 1)]; v = (pi & 1) ? (v >> 4) : (v & 15); }
          if (!v) continue;
          if (prio >= pri[x]) { pri[x] = prio; col[x] = (palb + v) & 0xfff; }
        }
      }
    }

    // ---------------------------------------------------------------- frame loop
    runFrame() {
      const w = this.gpuDV.getUint32(4, true), h = this.gpuDV.getUint32(8, true);
      this.scrW = Math.max(224, Math.min(384, w || 320));
      this.scrH = Math.max(192, Math.min(288, h || 240));
      const total = this.scrH + VBLANK_LINES;
      // a line lasts a whole number of 4-cycle pixels (12.5 MHz, see rtl/mc_vtiming.v)
      const cpl = 4 * Math.floor(CPU_HZ / (FPS * 4 * total));
      this.audioN = 0;
      const startCycles = this.cycles;
      this.stats.busy = 0;
      const startInstr = this.instret;
      for (let line = 0; line < total; line++) {
        this.vcount = line;
        if (line === 0) {
          this.inVblank = false;
          this.sprOverflowCur = false; this.sprPeakCur = 0;
        }
        if (line === this.scrH) {
          this.inVblank = true;
          this.sprOverflow = this.sprOverflowCur; this.sprPeak = this.sprPeakCur;
          this.frame = (this.frame + 1) >>> 0;
          this.irqStatus |= 1; this.brk = true;
        }
        if (line === this.gpuDV.getUint32(0x10, true)) { this.irqStatus |= 2; }
        if (line < this.scrH) this.renderLine(line);
        this.cycleAcc += cpl;
        const target = Math.round(startCycles + this.cycleAcc); // integer cycle counts
        this.runUntil(target);
      }
      this.cycleAcc -= cpl * total;
      const fc = cpl * total;
      this.stats.load = Math.min(1, this.stats.busy / fc);
      this.stats.instr = this.instret - startInstr;
      return this.audioN;
    }

    // run CPU (and timers) until absolute cycle target
    runUntil(target) {
      while (this.cycles < target && !this.halted) {
        // next timer event
        let stop = Math.min(target, this.nextSample);   // sound samples are mixed at their exact cycle
        for (const t of this.timers) if (t.on && t.reload > 0) stop = Math.min(stop, this.cycles + Math.max(1, Math.ceil(t.count)));
        const before = this.cycles;
        this.execUntil(stop);
        const spent = this.cycles - before;
        this.catchUpSamples();
        for (const t of this.timers) {
          if (!t.on || t.reload <= 0) continue;
          t.count -= spent;
          let n = 0;
          while (t.count <= 0) { t.count += t.reload; n++; }
          if (n) { this.irqStatus |= (t === this.timers[0]) ? 4 : 8; }
        }
      }
      if (this.halted && this.cycles < target) this.cycles = target;
      this.catchUpSamples();
    }

    // a sample is due at cycle ceil(k * 50e6 / 48000), like the 48 kHz accumulator of the RTL
    catchUpSamples() {
      while (this.cycles >= this.nextSample) {
        this.mixSample();
        this.sampleCount++;
        this.nextSample = Math.ceil((this.sampleCount + 1) * CPU_HZ / AUDIO_HZ);
      }
    }

    irqLine() { return (this.irqStatus & this.irqEnable) !== 0; }

    trap(cause, pc, tval) {
      if (!this.mtvec) { this.error(msg('noHandler', cause, (pc >>> 0).toString(16))); return; }
      this.mepc = pc >>> 0; this.mcause = cause >>> 0; this.mtval = tval >>> 0;
      this.mstatus = (this.mstatus & ~0x88) | ((this.mstatus & 8) << 4);
      this.pc = this.mtvec & ~3;
    }

    execUntil(stop) {
      const x = this.regs;
      while (this.cycles < stop && !this.halted) {
        const irq = this.irqLine();
        if (irq && (this.mie & 0x800)) {
          if (this.mstatus & 8) { this.wfi = false; this.trap(0x8000000B, this.pc, 0); }
          else this.wfi = false;
        }
        if (this.wfi) { this.cycles = Math.max(this.cycles, Math.ceil(stop)); return; }
        this.brk = false;
        let pc = this.pc, cyc = this.cycles;
        const busyStart = cyc;
        const rom32 = this.rom32, romSize = this.romSize, wram32 = this.wram32;
        let n = 0;
        while (cyc < stop) {
          let ins;
          if (pc < romSize) ins = rom32[pc >>> 2];
          else if (pc >= WRAM_BASE && pc < WRAM_BASE + WRAM_SIZE) ins = wram32[(pc - WRAM_BASE) >>> 2];
          else { this.pc = pc; this.cycles = cyc; this.error(msg('outOfMemory', (pc >>> 0).toString(16))); return; }
          if (pc & 3) { this.pc = pc; this.cycles = cyc; this.error(msg('misaligned', (pc >>> 0).toString(16))); return; }
          n++; cyc++;
          const op = ins & 0x7f, rd = (ins >> 7) & 31, f3 = (ins >> 12) & 7, rs1 = (ins >> 15) & 31, rs2 = (ins >> 20) & 31;
          let npc = (pc + 4) >>> 0;
          switch (op) {
            case 0x37: if (rd) x[rd] = ins & 0xfffff000; break;
            case 0x17: if (rd) x[rd] = (pc + (ins & 0xfffff000)) | 0; break;
            case 0x6f: {
              const imm = ((ins >> 31) << 20) | (((ins >> 12) & 0xff) << 12) | (((ins >> 20) & 1) << 11) | (((ins >> 21) & 0x3ff) << 1);
              if (rd) x[rd] = npc; npc = (pc + imm) >>> 0; cyc += 2; break;
            }
            case 0x67: {
              const t = ((x[rs1] + (ins >> 20)) & ~1) >>> 0;
              if (rd) x[rd] = npc; npc = t; cyc += 2; break;
            }
            case 0x63: {
              const a = x[rs1], b = x[rs2];
              let take;
              switch (f3) {
                case 0: take = a === b; break;
                case 1: take = a !== b; break;
                case 4: take = a < b; break;
                case 5: take = a >= b; break;
                case 6: take = (a >>> 0) < (b >>> 0); break;
                case 7: take = (a >>> 0) >= (b >>> 0); break;
                default: this.pc = pc; this.cycles = cyc; this.trap(2, pc, ins); return;
              }
              if (take) {
                const imm = ((ins >> 31) << 12) | (((ins >> 7) & 1) << 11) | (((ins >> 25) & 0x3f) << 5) | (((ins >> 8) & 0xf) << 1);
                npc = (pc + imm) >>> 0; cyc += 2;
              }
              break;
            }
            case 0x03: {
              const a = (x[rs1] + (ins >> 20)) >>> 0;
              let v;
              cyc++;
              if (f3 === 2 && a < romSize && !(a & 3)) v = rom32[a >>> 2];
              else if (f3 === 2 && a >= WRAM_BASE && a < WRAM_BASE + WRAM_SIZE && !(a & 3)) v = wram32[(a - WRAM_BASE) >>> 2];
              else {
                this.cycles = cyc;
                switch (f3) {
                  case 0: v = (this.read(a, 1) << 24) >> 24; break;
                  case 1: v = (this.read(a, 2) << 16) >> 16; break;
                  case 2: v = this.read(a, 4); break;
                  case 4: v = this.read(a, 1); break;
                  case 5: v = this.read(a, 2); break;
                  default: this.pc = pc; this.trap(2, pc, ins); return;
                }
                cyc = this.cycles;
              }
              if (rd) x[rd] = v;
              break;
            }
            case 0x23: {
              const a = (x[rs1] + (((ins >> 25) << 5) | ((ins >> 7) & 31))) >>> 0;
              const v = x[rs2];
              if (f3 === 2 && a >= WRAM_BASE && a < WRAM_BASE + WRAM_SIZE && !(a & 3)) wram32[(a - WRAM_BASE) >>> 2] = v;
              else {
                this.cycles = cyc;
                if (f3 === 0) this.write(a, 1, v); else if (f3 === 1) this.write(a, 2, v); else if (f3 === 2) this.write(a, 4, v);
                else { this.pc = pc; this.trap(2, pc, ins); return; }
                cyc = this.cycles;
                if (this.brk) { this.pc = npc; this.instret += n; this.stats.busy += cyc - busyStart; n = -1; break; }
              }
              break;
            }
            case 0x13: {
              const imm = ins >> 20;
              let v;
              switch (f3) {
                case 0: v = x[rs1] + imm; break;
                case 1: v = x[rs1] << rs2; break;
                case 2: v = x[rs1] < imm ? 1 : 0; break;
                case 3: v = (x[rs1] >>> 0) < (imm >>> 0) ? 1 : 0; break;
                case 4: v = x[rs1] ^ imm; break;
                case 5: v = (ins & 0x40000000) ? x[rs1] >> rs2 : x[rs1] >>> rs2; break;
                case 6: v = x[rs1] | imm; break;
                default: v = x[rs1] & imm; break;
              }
              if (rd) x[rd] = v;
              break;
            }
            case 0x33: {
              const a = x[rs1], b = x[rs2];
              let v;
              if ((ins >>> 25) === 1) {
                switch (f3) {
                  case 0: v = Math.imul(a, b); cyc += 2; break;
                  case 1: v = mulh(a, b); cyc += 2; break;
                  case 2: v = mulhsu(a, b); cyc += 2; break;
                  case 3: v = mulhu(a, b); cyc += 2; break;
                  case 4: v = b === 0 ? -1 : (a === -2147483648 && b === -1) ? a : Math.trunc(a / b); cyc += 16; break;
                  case 5: v = b === 0 ? -1 : Math.floor((a >>> 0) / (b >>> 0)); cyc += 16; break;
                  case 6: v = b === 0 ? a : (a === -2147483648 && b === -1) ? 0 : a % b; cyc += 16; break;
                  default: v = b === 0 ? a : (a >>> 0) % (b >>> 0); cyc += 16; break;
                }
              } else {
                switch (f3) {
                  case 0: v = (ins & 0x40000000) ? a - b : a + b; break;
                  case 1: v = a << (b & 31); break;
                  case 2: v = a < b ? 1 : 0; break;
                  case 3: v = (a >>> 0) < (b >>> 0) ? 1 : 0; break;
                  case 4: v = a ^ b; break;
                  case 5: v = (ins & 0x40000000) ? a >> (b & 31) : a >>> (b & 31); break;
                  case 6: v = a | b; break;
                  default: v = a & b; break;
                }
              }
              if (rd) x[rd] = v;
              break;
            }
            case 0x0f: break; // fence
            case 0x73: {
              this.cycles = cyc; this.pc = pc;
              if (f3 === 0) {
                if (ins === 0x30200073) { // mret
                  const mpie = (this.mstatus >> 7) & 1;
                  this.mstatus = (this.mstatus & ~8) | (mpie << 3) | 0x80;
                  npc = this.mepc >>> 0;
                } else if (ins === 0x10500073) { // wfi
                  this.wfi = true;
                } else if (ins === 0x00000073) { this.instret += n; this.stats.busy += cyc - busyStart; this.trap(11, pc, 0); return; }
                else if (ins === 0x00100073) { this.instret += n; this.stats.busy += cyc - busyStart; this.trap(3, pc, 0); return; }
                else { this.instret += n; this.stats.busy += cyc - busyStart; this.trap(2, pc, ins); return; }
              } else {
                const csr = ins >>> 20;
                const old = this.csrRead(csr);
                const src = (f3 & 4) ? rs1 : x[rs1];
                let nv = old, wr = true;
                switch (f3 & 3) {
                  case 1: nv = src; break;
                  case 2: nv = old | src; wr = rs1 !== 0; break;
                  case 3: nv = old & ~src; wr = rs1 !== 0; break;
                }
                if (wr) this.csrWrite(csr, nv);
                if (rd) x[rd] = old;
              }
              pc = npc;
              this.pc = pc; this.instret += n; this.stats.busy += cyc - busyStart; n = -1;
              break;
            }
            default:
              this.pc = pc; this.cycles = cyc; this.instret += n; this.stats.busy += cyc - busyStart;
              this.trap(2, pc, ins); return;
          }
          if (n < 0) break; // state already written back (system op or I/O write)
          pc = npc;
        }
        if (n >= 0) { this.pc = pc; this.instret += n; this.stats.busy += cyc - busyStart; this.cycles = cyc; }
        else this.cycles = cyc;
      }
    }

    csrRead(c) {
      switch (c) {
        case 0x300: return this.mstatus | 0;
        case 0x304: return this.mie | 0;
        case 0x344: return this.irqLine() ? 0x800 : 0;
        case 0x305: return this.mtvec | 0;
        case 0x340: return this.mscratch | 0;
        case 0x341: return this.mepc | 0;
        case 0x342: return this.mcause | 0;
        case 0x343: return this.mtval | 0;
        case 0xB00: case 0xC00: case 0xC01: return Math.floor(this.cycles) | 0;
        case 0xB80: case 0xC80: case 0xC81: return Math.floor(this.cycles / 4294967296) | 0;
        case 0xB02: case 0xC02: return this.instret | 0;
        case 0xB82: case 0xC82: return Math.floor(this.instret / 4294967296) | 0;
        case 0xF14: return 0;
        case 0x320: return 0;   // mcountinhibit (counters always run in the simulator)
        case 0x301: return 0x40001100; // RV32IM
      }
      return 0;
    }

    csrWrite(c, v) {
      switch (c) {
        case 0x300: this.mstatus = v & 0x88; break;
        case 0x304: this.mie = v & 0x800; break;
        case 0x305: this.mtvec = v >>> 0; break;
        case 0x340: this.mscratch = v; break;
        case 0x341: this.mepc = (v & ~3) >>> 0; break;
        case 0x342: this.mcause = v >>> 0; break;
        case 0x343: this.mtval = v >>> 0; break;
      }
    }
  }

  Monocore.CPU_HZ = CPU_HZ;
  Monocore.AUDIO_HZ = AUDIO_HZ;
  Monocore.FPS = FPS;
  Monocore.lang = 'fr';     // language of the messages: 'fr' or 'en'

  root.Monocore = Monocore;
  if (typeof module !== 'undefined' && module.exports) module.exports = Monocore;
})(typeof window !== 'undefined' ? window : globalThis);
