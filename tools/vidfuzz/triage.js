// Triage helper: for frame F of a ROM, find which layer (or the background) produced each pixel
// that differs between the simulator and the RTL screenshot, and print that layer's settings.
// usage: node triage.js rom.mnc F rtl.ppm
const fs = require('fs');
const path = require('path');
const Monocore = require(path.join(__dirname, '../../sim/core.js'));
const [romPath, F, rtlPath] = process.argv.slice(2);
const m = new Monocore(new Uint8Array(fs.readFileSync(romPath)));
for (let f = 0; f < +F; f++) m.runFrame();   // the state set during frame F-1's blanking renders frame F
const W = Math.max(224, Math.min(384, m.gpuDV.getUint32(4, true) || 320));
const H = Math.max(192, Math.min(288, m.gpuDV.getUint32(8, true) || 240));
m.scrW = W; m.scrH = H;
const ppm = fs.readFileSync(rtlPath);
let off = 0, nl = 0;
while (nl < 3) if (ppm[off++] === 10) nl++;
const g = m.gpuDV;
const ctrl = g.getUint32(0, true);
function renderWith(c) {
  g.setUint32(0, c, true);
  const out = new Uint32Array(W * H);
  for (let y = 0; y < H; y++) { m.renderLine(y); out.set(m.pic.subarray(y * 640, y * 640 + W), y * W); }
  g.setUint32(0, ctrl, true);
  return out;
}
const full = renderWith(ctrl);
const solo = [0, 1, 2, 3].map((l) => (ctrl & (2 << l)) ? renderWith((ctrl & ~0x3E) | (2 << l)) : null);
const bgOnly = renderWith(ctrl & ~0x3E);
const tally = {};
let ndiff = 0;
const examples = {};
for (let i = 0; i < W * H; i++) {
  const c = full[i];
  const r = ppm[off + i * 3], gg = ppm[off + i * 3 + 1], b = ppm[off + i * 3 + 2];
  const rc = (0xff000000 | (b << 16) | (gg << 8) | r) >>> 0;
  if (rc === c) continue;
  ndiff++;
  let who = 'fond';
  for (let l = 0; l < 4; l++) if (solo[l] && solo[l][i] === c && solo[l][i] !== bgOnly[i]) { who = 'plan' + l; break; }
  tally[who] = (tally[who] || 0) + 1;
  if (!examples[who]) examples[who] = { x: i % W, y: Math.floor(i / W), js: c.toString(16), rtl: rc.toString(16) };
}
console.log('frame', F, W + 'x' + H, 'DISP_CTRL', ctrl.toString(16), 'pixels differents', ndiff, tally);
for (let l = 0; l < 4; l++) {
  const lb = 0x100 + l * 0x20, lc = g.getUint32(lb, true);
  console.log('plan' + l, 'actif', !!(ctrl & (2 << l)), 'ctrl', lc.toString(16),
    'tuile', (lc & 1) ? 16 : 8, 'bpp', (lc & 2) ? 8 : 4, 'carte', (32 << ((lc >> 2) & 3)) + 'x' + (32 << ((lc >> 4) & 3)),
    'ligne', !!(lc & 0x40), 'prio', (lc >> 8) & 3, 'chr', g.getUint32(lb + 4, true).toString(16),
    'sx', g.getInt16(lb + 8, true), 'sy', g.getInt16(lb + 12, true));
}
console.log('exemples', JSON.stringify(examples));
