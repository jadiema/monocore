// MONOCORE-1 "msfx": sound effects of MONOSFX on the PSG (see msfx.h and MSFX.md).
// ms_voice_step is the whole sound: tools/msfx/msbank.py does exactly the same steps.
#include <string.h>
#include "mc.h"
#include "msfx.h"

int ms_open(ms_bank_t *b, const void *data, uint32_t max) {
  const ms_header_t *h = data;
  b->h = 0; b->fx = 0;
  if (max < sizeof *h || h->magic != MS_MAGIC || h->size > max || h->count > MS_EFFECTS ||
      h->size < sizeof *h + h->count * sizeof(ms_effect_t)) return -1;
  b->h = h; b->fx = (const ms_effect_t *)(h + 1);
  return 0;
}
int ms_check(const void *data, uint32_t max) { ms_bank_t b; return ms_open(&b, data, max); }
int ms_count(const ms_bank_t *b) { return b->h ? b->h->count : 0; }
const ms_effect_t *ms_effect(const ms_bank_t *b, int n) { return n >= 0 && n < ms_count(b) ? &b->fx[n] : 0; }
int ms_find(const ms_bank_t *b, const char *name) {
  for (int n = 0; n < ms_count(b); n++) {
    int i = 0;
    for (; i < 8; i++) {
      if (name[i] != b->fx[n].name[i]) break;
      if (!name[i]) return n;
    }
    if (i == 8 && !name[8]) return n;
  }
  return -1;
}

// ---------------------------------------------------------------- one effect, frame by frame
int ms_length(const ms_effect_t *e) { int n = e->attack + e->sustain + e->decay; return n ? n : 1; }
void ms_voice_start(ms_voice_t *v, const ms_effect_t *e) {
  v->e = e; v->t = 0; v->f = (int32_t)e->freq * 16; v->slide = e->slide; v->duty = e->duty;
}
// 2^(k/12) in 16.16, k = 0..11
static const uint32_t semitone[12] = { 65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715 };
static int32_t transpose(int32_t f, int k) {
  int oct = 0;
  while (k < 0) { k += 12; oct--; }
  while (k >= 12) { k -= 12; oct++; }
  int64_t r = (int64_t)f * semitone[k] >> 16;
  return (int32_t)(oct >= 0 ? r << oct : r >> -oct);
}
bool ms_voice_step(ms_voice_t *v, uint32_t *freq16, int *vol, int *duty) {
  const ms_effect_t *e = v->e;
  if (!e) return false;
  int len = ms_length(e), t = v->t;
  if (t >= len) return false;
  // volume: rises during the attack, stays, falls during the decay
  int a = e->attack, s = e->sustain, d = e->decay, vl;
  if (t < a) vl = e->volume * (t + 1) / (a + 1);
  else if (t < a + s) vl = e->volume;
  else vl = e->volume * (len - t) / (d + 1);
  // pitch: starts again, jumps, wobbles
  if (e->repeat && t && t % e->repeat == 0) { v->f = (int32_t)e->freq * 16; v->slide = e->slide; }
  if (e->arp_time && t == e->arp_time) v->f = transpose(v->f, e->arp);
  int32_t f = v->f;
  if (e->vib_speed && e->vib_depth) {        // a triangle of +- depth Hz
    int p = t % e->vib_speed, half = e->vib_speed;
    int tri = (p * 2 < half ? p * 4 : (half - p) * 4) - half;     // -half .. +half
    f += (int32_t)e->vib_depth * 16 * tri / (half ? half : 1);
  }
  if (f < 16 * 20) f = 16 * 20;
  if (f > 16 * 20000) f = 16 * 20000;
  *freq16 = (uint32_t)f;
  *vol = vl < 0 ? 0 : vl > 255 ? 255 : vl;
  *duty = v->duty < 8 ? 8 : v->duty > 248 ? 248 : v->duty;
  // next frame
  v->f += v->slide; v->slide += e->accel; v->duty += e->duty_sweep;
  if (v->f < 16 * 20) v->f = 16 * 20;
  v->t++;
  return true;
}

// ---------------------------------------------------------------- on the PSG
static ms_voice_t voice[8];
static uint32_t started[8], clock;
static uint8_t on[8];
static int ch_first = 6, ch_count = 2;
void ms_channels(int first, int count) { ch_first = first & 7; ch_count = count < 1 ? 1 : count > 8 - ch_first ? 8 - ch_first : count; }
void ms_stop(int ch) { ch &= 7; on[ch] = 0; mc_psg_volume(ch, 0, 0); }
void ms_stop_all(void) { for (int c = 0; c < 8; c++) if (on[c]) ms_stop(c); }
bool ms_playing(int ch) { return on[ch & 7]; }
void ms_play_on(const ms_effect_t *e, int ch) {
  if (!e) return;
  ch &= 7;
  ms_voice_start(&voice[ch], e);
  on[ch] = 1; started[ch] = ++clock;
  uint32_t f; int vl, d;
  if (ms_voice_step(&voice[ch], &f, &vl, &d)) {
    mc_psg(ch, e->wave & 3, 0, d, vl, vl);
    mc_psg_freq16(ch, f);
  } else ms_stop(ch);
}
int ms_play(const ms_bank_t *b, int n) {
  const ms_effect_t *e = ms_effect(b, n);
  if (!e) return -1;
  int best = ch_first;
  for (int c = ch_first; c < ch_first + ch_count; c++) {
    if (!on[c]) { best = c; break; }
    if (started[c] < started[best]) best = c;
  }
  ms_play_on(e, best);
  return best;
}
void ms_tick(void) {
  for (int c = 0; c < 8; c++) {
    if (!on[c]) continue;
    uint32_t f; int vl, d;
    if (!ms_voice_step(&voice[c], &f, &vl, &d)) { ms_stop(c); continue; }
    PSG[c].duty = (uint8_t)d;
    mc_psg_freq16(c, f);
    mc_psg_volume(c, vl, vl);
  }
}
