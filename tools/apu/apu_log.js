#!/usr/bin/env node
// Sound register write logs for the bit-exact APU test.
//   node apu_log.js session rom.mnc "script" frames out.log   -> writes made by a ROM run in the simulator
//   node apu_log.js random  rom.mnc seed samples out.log      -> random writes over all registers
// Log line: "<sample index> <byte offset> <size> <value>": the write happens before that sample.
const fs = require('fs');
const path = require('path');
const Monocore = require(path.join(__dirname, '../../sim/core.js'));
const [mode, romPath, a1, a2, outPath] = process.argv.slice(2);
const rom = new Uint8Array(fs.readFileSync(romPath));
const lines = [];

if (mode === 'session') {
  const m = new Monocore(rom);
  const orig = m.apuWrite.bind(m);
  m.apuWrite = (o, size, v) => { lines.push(`${m.sampleCount} ${o} ${size} ${v >>> 0}`); orig(o, size, v); };
  const events = {};
  for (const tok of a1.split(/\s+/).filter(Boolean)) {
    const [f, act] = tok.split(':');
    (events[+f] = events[+f] || []).push(act);
  }
  for (let f = 0; f < +a2; f++) {
    for (const act of events[f] || []) if (act.startsWith('p1=')) m.inputs[0] = parseInt(act.slice(3), 16);
    m.runFrame();
  }
  lines.push(`#samples ${m.sampleCount}`);
} else {
  let s = (+a1 >>> 0) || 1;
  const rnd = () => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; return s; };
  const N = +a2;
  const romLen = rom.length;
  let k = 0;
  while (k < N) {
    k += rnd() % 40;
    const r = rnd() % 100;
    const w = (o, size, v) => lines.push(`${k} ${o} ${size} ${v >>> 0}`);
    if (r < 60) {
      const v = rnd() % 8, b = v * 32;
      const what = rnd() % 10;
      if (what === 0) {
        // set up a whole voice and start it
        const fmt = rnd() % 4;
        w(b + 0x00, 4, rnd() % 20 === 0 ? romLen + (rnd() % 4096) : rnd() % romLen);
        w(b + 0x04, 4, 1 + rnd() % 30000);
        w(b + 0x08, 4, rnd() % 3 === 0 ? 0xFFFFFFFF : rnd() % 30000);
        w(b + 0x0C, 4, rnd() % 10 === 0 ? rnd() : rnd() % 0x30000);
        w(b + 0x10, 4, (rnd() & 0xFFFF) | (fmt << 16));
        w(b + 0x14, 4, 1);
      } else if (what < 3) w(b + 0x14, (rnd() & 1) ? 4 : 1, rnd() & 1);
      else if (what < 5) w(b + 0x10 + (rnd() % 2), 1, rnd() & 0xFF);
      else if (what < 7) w(b + 0x0C, 4, rnd() % 0x30000);
      else if (what < 8) w(b + 0x12, 1, rnd() % 4);
      else if (what < 9) w(b + 0x08, 4, rnd() % 3 === 0 ? 0xFFFFFFFF : rnd() % 20000);
      else w(b + 0x0C, 2, rnd() & 0xFFFF);
    } else if (r < 95) {
      const c = rnd() % 8, b = 0x400 + c * 16;
      const what = rnd() % 6;
      if (what === 0) w(b, 4, rnd() % 6 === 0 ? rnd() : rnd() % 0x80000);
      else if (what === 1) w(b + 4, 4, rnd());
      else if (what === 2) w(b + 4, 1, rnd() % 5);
      else if (what === 3) w(b + 6, 2, rnd() & 0xFFFF);
      else if (what === 4) w(b + 5, 1, rnd() & 0xFF);
      else w(b, 2, rnd() & 0xFFFF);
    } else w(0xF00, (rnd() & 1) ? 4 : 1, rnd() & 0xFF);
  }
  lines.push(`#samples ${N}`);
}
fs.writeFileSync(outPath, lines.join('\n') + '\n');
console.log(`${outPath}: ${lines.length - 1} écritures`);
