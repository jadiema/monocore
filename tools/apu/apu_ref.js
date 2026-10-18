#!/usr/bin/env node
// Replays a sound register log on the simulator's sound engine alone and writes the samples
// (interleaved int16 L, R) to a raw file. usage: node apu_ref.js rom.mnc log out.raw
const fs = require('fs');
const path = require('path');
const Monocore = require(path.join(__dirname, '../../sim/core.js'));
const [romPath, logPath, outPath] = process.argv.slice(2);
const m = new Monocore(new Uint8Array(fs.readFileSync(romPath)));
const writes = [];
let total = 0;
for (const line of fs.readFileSync(logPath, 'utf8').split('\n')) {
  if (!line) continue;
  if (line.startsWith('#samples')) { total = +line.split(' ')[1]; continue; }
  const [k, o, size, v] = line.split(' ').map(Number);
  writes.push([k, o, size, v]);
}
const out = Buffer.alloc(total * 4);
m.onSample = (l, r) => { out.writeInt16LE(l, n * 4); out.writeInt16LE(r, n * 4 + 2); };
let n = 0, w = 0;
for (; n < total; n++) {
  while (w < writes.length && writes[w][0] <= n) { const [, o, size, v] = writes[w++]; m.apuWrite(o, size, v | 0); }
  m.audioN = 0;
  m.mixSample();
}
fs.writeFileSync(outPath, out);
console.log(`${outPath}: ${total} échantillons`);
