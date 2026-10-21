// Where the MONOCORE CPU spends its time: plays a scripted session in the simulator and samples
// the program counter after every line of every frame (264 samples per frame).
// usage: node tools/prof.js ROM.mnc "SCRIPT" FROM TO OUT.txt   (samples from frame FROM to TO)
// OUT.txt: "address count" per sampled address, for tools/prof.py
const fs = require('fs'), path = require('path');
const Monocore = require(path.join(__dirname, '../../../sim/core.js'));
const [rom, script, from, to, out] = process.argv.slice(2);
const m = new Monocore(new Uint8Array(fs.readFileSync(rom)));
const events = {};
for (const tok of script.split(/\s+/).filter(Boolean)) {
  const [f, a] = tok.split(':'); (events[+f] = events[+f] || []).push(a);
}
const samples = new Map(); let sampling = false;
const runUntil = m.runUntil.bind(m);
m.runUntil = (t) => { runUntil(t); if (sampling) samples.set(m.pc >>> 0, (samples.get(m.pc >>> 0) || 0) + 1); };
for (let f = 0; f <= +to; f++) {
  for (const a of events[f] || []) {
    const v = parseInt(a.slice(3), 16);
    if (a.startsWith('p1=')) m.inputs[0] = v; else if (a.startsWith('p2=')) m.inputs[1] = v;
    else if (a.startsWith('p3=')) m.inputs[3] = v; else if (a.startsWith('p4=')) m.inputs[4] = v;
  }
  sampling = f >= +from;
  m.runFrame();
}
fs.writeFileSync(out, [...samples].map(([a, n]) => a.toString(16) + ' ' + n).join('\n') + '\n');
