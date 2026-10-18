#!/usr/bin/env node
// Headless MONOCORE-1 runner: run a ROM for N frames with scripted inputs, save screenshots and audio.
// usage: node run.js rom.mnc outdir "frame:action ..."
//   actions: shot  |  p1=<hex>  (sets P1 inputs from this frame)  |  sys=<hex>
const fs = require('fs');
const path = require('path');
const Monocore = require(path.join(__dirname, '../sim/core.js'));

const [romPath, outDir, script = ''] = process.argv.slice(2);
fs.mkdirSync(outDir, { recursive: true });
const m = new Monocore(new Uint8Array(fs.readFileSync(romPath)));
let dbg = '';
m.onDebug = (c) => { dbg += c; };
m.onError = (e) => { console.log('ERREUR:', e); };

const events = {};
let last = 0;
for (const tok of script.split(/\s+/).filter(Boolean)) {
  const [f, a] = tok.split(':');
  (events[+f] = events[+f] || []).push(a);
  last = Math.max(last, +f);
}
const audio = [];
let maxLoad = 0, sumLoad = 0, frames = 0;
const t0 = Date.now();
for (let f = 0; f <= last; f++) {
  for (const a of events[f] || []) {
    if (a.startsWith('p1=')) m.inputs[0] = parseInt(a.slice(3), 16);
    else if (a.startsWith('p2=')) m.inputs[1] = parseInt(a.slice(3), 16);
    else if (a.startsWith('sys=')) m.inputs[2] = parseInt(a.slice(4), 16);
  }
  const n = m.runFrame();
  for (let i = 0; i < 2 * n; i++) audio.push(m.audioI[i]);
  maxLoad = Math.max(maxLoad, m.stats.load); sumLoad += m.stats.load; frames++;
  for (const a of events[f] || []) {
    if (a.startsWith('dump=')) {
      // video and sound memories, in a fixed layout shared with the RTL testbench
      const parts = [m.palette, m.sprites, m.tilemaps, m.lineTab, m.chrram, m.apuRegs];
      fs.writeFileSync(path.join(outDir, a.slice(5) + '.mem'), Buffer.concat(parts.map((p) => Buffer.from(p.buffer, p.byteOffset, p.byteLength))));
      const g = m.gpuDV, regs = {};
      for (const [n, o] of [['disp_ctrl', 0], ['screen_w', 4], ['screen_h', 8], ['bg_color', 12], ['line_cmp', 16], ['spr_chr', 32], ['spr_count', 36]]) regs[n] = g.getUint32(o, true);
      for (let l = 0; l < 4; l++) for (const [n, o] of [['ctrl', 0], ['chr', 4], ['sx', 8], ['sy', 12]]) regs['l' + l + '_' + n] = g.getUint32(0x100 + l * 0x20 + o, true);
      regs.frame = m.frame;
      fs.writeFileSync(path.join(outDir, a.slice(5) + '.json'), JSON.stringify(regs));
    }
    if (a.startsWith('shot')) {
      const W = m.scrW, H = m.scrH;
      const buf = Buffer.alloc(W * H * 3);
      for (let i = 0; i < W * H; i++) {
        const c = m.frameBuf[i];
        buf[i * 3] = c & 255; buf[i * 3 + 1] = (c >> 8) & 255; buf[i * 3 + 2] = (c >> 16) & 255;
      }
      const name = a.split('=')[1] || ('f' + f);
      fs.writeFileSync(path.join(outDir, name + '.ppm'), Buffer.concat([Buffer.from(`P6\n${W} ${H}\n255\n`), buf]));
    }
  }
  if (m.halted) break;
}
// 16-bit stereo WAV
const pcm = Buffer.alloc(audio.length * 2);
for (let i = 0; i < audio.length; i++) pcm.writeInt16LE(audio[i], i * 2);
const hdr = Buffer.alloc(44);
hdr.write('RIFF', 0); hdr.writeUInt32LE(36 + pcm.length, 4); hdr.write('WAVE', 8); hdr.write('fmt ', 12);
hdr.writeUInt32LE(16, 16); hdr.writeUInt16LE(1, 20); hdr.writeUInt16LE(2, 22); hdr.writeUInt32LE(48000, 24);
hdr.writeUInt32LE(192000, 28); hdr.writeUInt16LE(4, 32); hdr.writeUInt16LE(16, 34); hdr.write('data', 36); hdr.writeUInt32LE(pcm.length, 40);
fs.writeFileSync(path.join(outDir, 'audio.wav'), Buffer.concat([hdr, pcm]));
let peak = 0; for (const s of audio) peak = Math.max(peak, Math.abs(s) / 32768);
console.log(`frames ${frames} en ${Date.now() - t0} ms, charge CPU moy ${(sumLoad / frames * 100).toFixed(1)}% max ${(maxLoad * 100).toFixed(1)}%, crete audio ${peak.toFixed(3)}, pc 0x${m.pc.toString(16)}`);
console.log('debug: ' + JSON.stringify(dbg));
