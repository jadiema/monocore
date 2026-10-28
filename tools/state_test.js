// Save states of the simulator: run A frames, save, run B frames; then load the state into another machine
// (that already ran other frames) and check that the B frames give the same picture and sound.
// node tools/state_test.js rom.mnc A B
const M = require(require('path').join(__dirname, '../sim/core.js')), fs = require('fs'), crypto = require('crypto');
const [rom, A, B] = [process.argv[2], +process.argv[3], +process.argv[4]];
const bytes = new Uint8Array(fs.readFileSync(rom));
function step(m, inp) { if (inp) inp(m); m.runFrame(); const h = crypto.createHash('md5');
  h.update(Buffer.from(m.frameBuf.buffer, 0, m.scrW * m.scrH * 4)); h.update(Buffer.from(m.audioI.buffer, 0, m.audioN * 4)); m.audioN = 0; return h.digest('hex'); }
// some input activity so that the pads and keyboard paths are exercised
const inp = (f) => (m) => { m.inputs[0] = (f % 37 < 5) ? 0x10 : (f % 53 < 3 ? 0x01 : 0); };
const a = new M(bytes); a.onDebug = () => {};
for (let f = 0; f < A; f++) step(a, inp(f));
const st = a.saveState();
const ref = []; for (let f = A; f < A + B; f++) ref.push(step(a, inp(f)));
const b = new M(bytes); b.onDebug = () => {};
for (let f = 0; f < 7; f++) step(b, inp(f + 1000));   // a different machine state before loading
b.loadState(st);
let bad = -1; for (let i = 0; i < B; i++) if (step(b, inp(A + i)) !== ref[i]) { bad = i; break; }
const st2 = b.saveState();
console.log(rom.split('/').pop(), 'etat', (st.length / 1024).toFixed(0), 'Ko,', bad < 0 ? `${B} images identiques apres rechargement` : `DIFFERENCE a l'image ${A + bad}`);
