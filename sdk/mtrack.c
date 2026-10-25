// MONOCORE-1 "mtrack": music player of MONOTRACKER (see mtrack.h and MTRACK.md).
#include <string.h>
#include "mc.h"
#include "mtrack.h"

// 2^(i/768) in 16.16 fixed point: one octave in steps of 1/64 semitone
static const uint32_t pow768[768] = {
  65536, 65595, 65654, 65714, 65773, 65832, 65892, 65951, 66011, 66071, 66130, 66190,
  66250, 66309, 66369, 66429, 66489, 66549, 66609, 66670, 66730, 66790, 66850, 66911,
  66971, 67032, 67092, 67153, 67213, 67274, 67335, 67395, 67456, 67517, 67578, 67639,
  67700, 67761, 67823, 67884, 67945, 68007, 68068, 68129, 68191, 68252, 68314, 68376,
  68438, 68499, 68561, 68623, 68685, 68747, 68809, 68871, 68933, 68996, 69058, 69120,
  69183, 69245, 69308, 69370, 69433, 69496, 69558, 69621, 69684, 69747, 69810, 69873,
  69936, 69999, 70062, 70126, 70189, 70252, 70316, 70379, 70443, 70507, 70570, 70634,
  70698, 70762, 70825, 70889, 70953, 71017, 71082, 71146, 71210, 71274, 71339, 71403,
  71468, 71532, 71597, 71661, 71726, 71791, 71856, 71920, 71985, 72050, 72115, 72181,
  72246, 72311, 72376, 72442, 72507, 72573, 72638, 72704, 72769, 72835, 72901, 72967,
  73032, 73098, 73164, 73230, 73297, 73363, 73429, 73495, 73562, 73628, 73695, 73761,
  73828, 73894, 73961, 74028, 74095, 74162, 74229, 74296, 74363, 74430, 74497, 74564,
  74632, 74699, 74766, 74834, 74902, 74969, 75037, 75105, 75172, 75240, 75308, 75376,
  75444, 75512, 75581, 75649, 75717, 75786, 75854, 75922, 75991, 76060, 76128, 76197,
  76266, 76335, 76404, 76473, 76542, 76611, 76680, 76749, 76819, 76888, 76957, 77027,
  77096, 77166, 77236, 77305, 77375, 77445, 77515, 77585, 77655, 77725, 77795, 77866,
  77936, 78006, 78077, 78147, 78218, 78288, 78359, 78430, 78501, 78572, 78642, 78713,
  78785, 78856, 78927, 78998, 79069, 79141, 79212, 79284, 79355, 79427, 79499, 79571,
  79642, 79714, 79786, 79858, 79930, 80003, 80075, 80147, 80220, 80292, 80365, 80437,
  80510, 80582, 80655, 80728, 80801, 80874, 80947, 81020, 81093, 81166, 81240, 81313,
  81386, 81460, 81533, 81607, 81681, 81754, 81828, 81902, 81976, 82050, 82124, 82198,
  82273, 82347, 82421, 82496, 82570, 82645, 82719, 82794, 82869, 82944, 83019, 83093,
  83169, 83244, 83319, 83394, 83469, 83545, 83620, 83696, 83771, 83847, 83923, 83998,
  84074, 84150, 84226, 84302, 84378, 84454, 84531, 84607, 84683, 84760, 84836, 84913,
  84990, 85066, 85143, 85220, 85297, 85374, 85451, 85528, 85606, 85683, 85760, 85838,
  85915, 85993, 86070, 86148, 86226, 86304, 86382, 86460, 86538, 86616, 86694, 86772,
  86851, 86929, 87008, 87086, 87165, 87244, 87322, 87401, 87480, 87559, 87638, 87717,
  87796, 87876, 87955, 88034, 88114, 88194, 88273, 88353, 88433, 88513, 88592, 88672,
  88752, 88833, 88913, 88993, 89073, 89154, 89234, 89315, 89396, 89476, 89557, 89638,
  89719, 89800, 89881, 89962, 90043, 90125, 90206, 90288, 90369, 90451, 90532, 90614,
  90696, 90778, 90860, 90942, 91024, 91106, 91188, 91271, 91353, 91436, 91518, 91601,
  91684, 91766, 91849, 91932, 92015, 92098, 92181, 92265, 92348, 92431, 92515, 92598,
  92682, 92766, 92849, 92933, 93017, 93101, 93185, 93269, 93354, 93438, 93522, 93607,
  93691, 93776, 93860, 93945, 94030, 94115, 94200, 94285, 94370, 94455, 94541, 94626,
  94711, 94797, 94882, 94968, 95054, 95140, 95226, 95312, 95398, 95484, 95570, 95656,
  95743, 95829, 95916, 96002, 96089, 96176, 96263, 96350, 96436, 96524, 96611, 96698,
  96785, 96873, 96960, 97048, 97135, 97223, 97311, 97399, 97487, 97575, 97663, 97751,
  97839, 97928, 98016, 98104, 98193, 98282, 98370, 98459, 98548, 98637, 98726, 98815,
  98905, 98994, 99083, 99173, 99262, 99352, 99442, 99531, 99621, 99711, 99801, 99891,
  99982, 100072, 100162, 100253, 100343, 100434, 100524, 100615, 100706, 100797, 100888, 100979,
  101070, 101162, 101253, 101344, 101436, 101527, 101619, 101711, 101803, 101895, 101987, 102079,
  102171, 102263, 102356, 102448, 102540, 102633, 102726, 102818, 102911, 103004, 103097, 103190,
  103283, 103377, 103470, 103564, 103657, 103751, 103844, 103938, 104032, 104126, 104220, 104314,
  104408, 104502, 104597, 104691, 104786, 104880, 104975, 105070, 105165, 105260, 105355, 105450,
  105545, 105640, 105736, 105831, 105927, 106022, 106118, 106214, 106310, 106406, 106502, 106598,
  106694, 106791, 106887, 106984, 107080, 107177, 107274, 107371, 107468, 107565, 107662, 107759,
  107856, 107954, 108051, 108149, 108246, 108344, 108442, 108540, 108638, 108736, 108834, 108932,
  109031, 109129, 109228, 109326, 109425, 109524, 109623, 109722, 109821, 109920, 110019, 110119,
  110218, 110317, 110417, 110517, 110617, 110716, 110816, 110917, 111017, 111117, 111217, 111318,
  111418, 111519, 111619, 111720, 111821, 111922, 112023, 112124, 112226, 112327, 112428, 112530,
  112631, 112733, 112835, 112937, 113039, 113141, 113243, 113345, 113448, 113550, 113653, 113755,
  113858, 113961, 114064, 114167, 114270, 114373, 114476, 114580, 114683, 114787, 114890, 114994,
  115098, 115202, 115306, 115410, 115514, 115618, 115723, 115827, 115932, 116036, 116141, 116246,
  116351, 116456, 116561, 116667, 116772, 116877, 116983, 117088, 117194, 117300, 117406, 117512,
  117618, 117724, 117831, 117937, 118043, 118150, 118257, 118363, 118470, 118577, 118684, 118792,
  118899, 119006, 119114, 119221, 119329, 119437, 119544, 119652, 119760, 119869, 119977, 120085,
  120194, 120302, 120411, 120519, 120628, 120737, 120846, 120955, 121065, 121174, 121283, 121393,
  121502, 121612, 121722, 121832, 121942, 122052, 122162, 122272, 122383, 122493, 122604, 122715,
  122825, 122936, 123047, 123158, 123270, 123381, 123492, 123604, 123715, 123827, 123939, 124051,
  124163, 124275, 124387, 124500, 124612, 124725, 124837, 124950, 125063, 125176, 125289, 125402,
  125515, 125628, 125742, 125855, 125969, 126083, 126197, 126310, 126425, 126539, 126653, 126767,
  126882, 126996, 127111, 127226, 127341, 127456, 127571, 127686, 127801, 127917, 128032, 128148,
  128263, 128379, 128495, 128611, 128727, 128844, 128960, 129076, 129193, 129310, 129426, 129543,
  129660, 129777, 129894, 130012, 130129, 130247, 130364, 130482, 130600, 130718, 130836, 130954,
};

static const int8_t sine64[64] = {
  0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126,
  127, 126, 125, 122, 117, 112, 106, 98, 90, 81, 71, 60, 49, 37, 25, 12,
  0, -12, -25, -37, -49, -60, -71, -81, -90, -98, -106, -112, -117, -122, -125, -126,
  -127, -126, -125, -122, -117, -112, -106, -98, -90, -81, -71, -60, -49, -37, -25, -12 };

#define ENV_MAX (64 << 8)
enum { ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE, ENV_OFF };

typedef struct {
  const mt_instrument_t *ins;    // instrument of the note sounding
  const mt_instrument_t *sins;   // last instrument named by the song on this track
  int32_t pitch, target;         // 1/64 semitone from C-0
  int32_t env, env_step;
  uint8_t stage, svol, xvol, sfx, note;
  int16_t pan;                   // -1: the instrument's
  uint16_t ticks;
  uint8_t vib_pos, arp_pos, arp_timer;
  int16_t duty, dsweep;
  uint8_t fx, param;             // effect of the current row (fx = 1-16, 0 none)
  uint8_t porta_speed, vib_speed, vib_depth, slide_mem, volslide_mem;
  uint8_t muted, reserved, level;
  uint32_t delayed;              // cell of an EDx note delay
  uint32_t offset;               // 9xx sample offset of the next PCM note
} chan_t;

static chan_t ch[MT_TRACKS];
static const uint8_t *bank;
static mt_source_t cur;          // song being played
static volatile bool playing, paused, loop_pat;
static int order, row, tick, speed = 6, bpm = 125, master = 64;
static int next_order, next_row; // pattern break / jump, -1 if none
static int timer_id = MT_TIMER_DEFAULT;
static bool timer_on;

// compact song being played
static mt_instrument_t song_ins[MT_INSTRUMENTS];
static const uint8_t *song_data;
static uint32_t pat_buf[MT_ROWS * MT_TRACKS];
static int pat_cached = -1;

static inline uint32_t irq_save(void) { uint32_t s; __asm__ volatile("csrrci %0, mstatus, 8" : "=r"(s)); return s; }
static inline void irq_restore(uint32_t s) { if (s & 8) __asm__ volatile("csrsi mstatus, 8"); }
static inline uint32_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static inline uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

// ---------------------------------------------------------------- bank
int mt_bank_count(void) { return bank ? (int)rd32(bank + 4) : 0; }
const mt_sample_t *mt_bank_sample(int n) {
  if (n < 0 || n >= mt_bank_count()) return 0;
  return (const mt_sample_t *)(bank + 8 + 32 * n);
}
int mt_track_type(int t) { return t >= 8 ? MT_TYPE_PCM : MT_TYPE_PSG; }

// ---------------------------------------------------------------- pitch
static uint32_t psg_freq16(int32_t p) {          // 1/16 Hz
  if (p < 0) p = 0;
  int o = p / 768;
  uint64_t f = ((uint64_t)66976 * pow768[p % 768]) >> 16;   // C-0 in 1/16 Hz x 256
  f = (f << o) >> 8;
  return f > 0xFFFFF ? 0xFFFFF : (uint32_t)f;
}
static uint32_t pcm_step(int32_t p, const mt_sample_t *s) {   // 16.16
  int32_t d = p - (int32_t)(s->root - 1) * 64;
  int o = d >= 0 ? d / 768 : -((-d + 767) / 768);
  int fr = d - o * 768;
  uint64_t st = (((uint64_t)s->rate << 16) / 48000 * pow768[fr]) >> 16;
  if (o >= 0) st <<= o; else st >>= -o;
  return st > 0xFFFFF ? 0xFFFFF : (uint32_t)st;
}

// ---------------------------------------------------------------- notes
static void hw_silence(int t) {
  if (t < 8) { PSG[t].vol_l = 0; PSG[t].vol_r = 0; }
  else { PCM[t - 8].ctrl = 0; PCM[t - 8].vol_l = 0; PCM[t - 8].vol_r = 0; }
}

static void trigger(int t, int note, const mt_instrument_t *ins) {
  chan_t *c = &ch[t];
  if (!ins || ins->type != mt_track_type(t)) return;
  c->ins = ins; c->note = (uint8_t)note;
  if (ins->flags & MT_IF_FIXED) note = 49;
  c->pitch = c->target = (note - 1 + ins->transpose) * 64 + ins->finetune;
  if (ins->attack) { c->stage = ENV_ATTACK; c->env = 0; }
  else { c->stage = ENV_DECAY; c->env = ENV_MAX; }
  c->ticks = 0; c->vib_pos = 0; c->arp_pos = 0; c->arp_timer = 0;
  c->duty = ins->duty; c->dsweep = ins->duty_sweep;
  if (t < 8) {
    PSG[t].wave = ins->wave;
  } else {
    const mt_sample_t *s = mt_bank_sample(ins->wave);
    volatile pcm_voice_t *v = &PCM[t - 8];
    v->ctrl = 0;
    if (!s) { c->stage = ENV_OFF; return; }
    uint32_t off = c->offset < s->length ? c->offset : 0, loop = s->loop;
    if (ins->duty == 1 || loop >= s->length) loop = PCM_NOLOOP;
    else loop = loop > off ? loop - off : 0;
    v->start = (uint32_t)(bank + s->offset) + 2 * off;
    v->length = s->length - off;
    v->loop = loop;
    v->format = PCM_S16;
    v->step = pcm_step(c->pitch, s);
    v->vol_l = 0; v->vol_r = 0;
    v->ctrl = 1;
  }
  c->offset = 0;
}

static void release(chan_t *c) {
  if (c->stage >= ENV_RELEASE) return;
  if (!c->ins || !c->ins->release) { c->env = 0; c->stage = ENV_OFF; return; }
  c->stage = ENV_RELEASE;
  c->env_step = c->env / c->ins->release + 1;
}

static void update(int t) {
  chan_t *c = &ch[t];
  const mt_instrument_t *ins = c->ins;
  if (c->stage == ENV_OFF || !ins) {
    if (c->level || c->sfx) { hw_silence(t); c->level = 0; }
    c->sfx = 0;
    return;
  }
  // arpeggio: the 0xy effect, or the instrument's
  int arp = 0;
  if (c->fx == 1 && c->param) {
    int k = c->ticks % 3;
    arp = k == 1 ? c->param >> 4 : k == 2 ? c->param & 15 : 0;
  } else if (ins->arp_speed) {
    arp = c->arp_pos == 1 ? ins->arp1 : c->arp_pos == 2 ? ins->arp2 : 0;
    if (++c->arp_timer >= ins->arp_speed) {
      c->arp_timer = 0;
      c->arp_pos = (c->arp_pos + 1) % 3;
      if (c->arp_pos == 2 && !ins->arp2) c->arp_pos = 0;
    }
  }
  // vibrato: the 4xy effect, or the instrument's after its delay
  int vib = 0;
  if (c->fx == 5) {
    vib = sine64[c->vib_pos & 63] * c->vib_depth * 4 / 127;
    c->vib_pos += c->vib_speed;
  } else if (ins->vib_depth && c->ticks >= ins->vib_delay) {
    vib = sine64[c->vib_pos & 63] * ins->vib_depth * 4 / 127;
    c->vib_pos += ins->vib_speed;
  }
  int32_t p = c->pitch + arp * 64 + vib;
  // volume: envelope x instrument x channel x master
  int cv = c->sfx ? c->xvol : c->svol;
  int v = (int)(((uint32_t)(c->env >> 8) * ins->volume * cv * (c->sfx ? 64 : master)) >> 16);
  if (v > 255) v = 255;
  if (c->muted && !c->sfx) v = 0;
  int pan = c->pan >= 0 ? c->pan : ins->pan;
  int fl = 2 * (256 - pan), fr = 2 * (pan + 1);
  int vl = (v * (fl > 256 ? 256 : fl)) >> 8, vr = (v * (fr > 256 ? 256 : fr)) >> 8;
  c->level = (uint8_t)v;
  if (t < 8) {
    volatile psg_chan_t *h = &PSG[t];
    h->freq = psg_freq16(p);
    h->duty = (uint8_t)c->duty;
    h->vol_l = (uint8_t)vl; h->vol_r = (uint8_t)vr;
    if (c->dsweep) {
      c->duty += c->dsweep;
      if (ins->flags & MT_IF_BOUNCE) {
        if (c->duty < 1) { c->duty = 2 - c->duty; c->dsweep = -c->dsweep; }
        if (c->duty > 255) { c->duty = 510 - c->duty; c->dsweep = -c->dsweep; }
      } else c->duty &= 255;
    }
  } else {
    volatile pcm_voice_t *h = &PCM[t - 8];
    const mt_sample_t *s = mt_bank_sample(ins->wave);
    if (!(h->ctrl & 1)) { c->stage = ENV_OFF; c->level = 0; c->sfx = 0; return; }   // sample ended
    if (s) h->step = pcm_step(p, s);
    h->vol_l = (uint8_t)vl; h->vol_r = (uint8_t)vr;
  }
  // envelope and sweeps, for the next tick
  switch (c->stage) {
    case ENV_ATTACK:
      c->env += ENV_MAX / ins->attack;
      if (c->env >= ENV_MAX) { c->env = ENV_MAX; c->stage = ENV_DECAY; }
      break;
    case ENV_DECAY: {
      int32_t sus = (int32_t)ins->sustain << 8;
      if (!ins->decay) { c->env = sus; c->stage = ENV_SUSTAIN; break; }
      c->env -= (ENV_MAX - sus) / ins->decay + 1;
      if (c->env <= sus) { c->env = sus; c->stage = ENV_SUSTAIN; }
      break;
    }
    case ENV_RELEASE:
      c->env -= c->env_step;
      if (c->env <= 0) { c->env = 0; c->stage = ENV_OFF; }
      break;
  }
  if (c->stage == ENV_SUSTAIN && c->env == 0) c->stage = ENV_OFF;
  if (ins->pitch_sweep) { c->pitch += ins->pitch_sweep * 4; if (c->pitch < 0) c->pitch = 0; c->target = c->pitch; }
  c->ticks++;
}

// ---------------------------------------------------------------- song
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void set_timer(void) {           // tick rate = bpm x 2 / 5 Hz
  mc_on_timer(timer_id, 50, mt_tick);
  timer_on = true;
  uint32_t reload = 125000000u / (uint32_t)bpm;
  if (timer_id) T1_RELOAD = reload; else T0_RELOAD = reload;
}

static void row_cell(int t, uint32_t cell, bool delayed) {
  chan_t *c = &ch[t];
  int note = MT_C_NOTE(cell), inst = MT_C_INST(cell), vol = MT_C_VOL(cell);
  int fx = MT_C_FX(cell), param = MT_C_PARAM(cell);
  if (!delayed) {
    c->fx = fx; c->param = param;
    if (fx == 15 && (param >> 4) == 0xD && (param & 15)) { c->delayed = cell; return; }   // EDx
  }
  if (inst && cur.ins) {
    const mt_instrument_t *in = &cur.ins[inst];
    if (in->type == mt_track_type(t)) { c->sins = in; c->svol = 64; c->pan = -1; }
  }
  if (c->sfx && c->stage != ENV_OFF) return;     // a sound effect owns the track
  int fxn = fx ? fx - 1 : -1;
  if (fxn == 9 && t >= 8) c->offset = (uint32_t)param << 8;        // 9xx on PCM: sample offset
  if (note == MT_NOTE_OFF) release(c);
  else if (note) {
    if (fxn == 3 && c->stage != ENV_OFF && c->ins) {               // 3xx: slide to the note
      c->target = (note - 1 + c->ins->transpose) * 64 + c->ins->finetune;
    } else trigger(t, note, c->sins);
  }
  if (vol) c->svol = (uint8_t)(vol - 1);
  switch (fxn) {
    case 3: if (param) c->porta_speed = param; break;
    case 4: if (param >> 4) c->vib_speed = param >> 4; if (param & 15) c->vib_depth = param & 15; break;
    case 8: c->pan = param; break;
    case 9: if (t < 8) c->duty = param; break;
    case 10: if (param) c->volslide_mem = param; break;
    case 11: next_order = param; next_row = 0; break;
    case 12: c->svol = (uint8_t)(param > 64 ? 64 : param); break;
    case 13: if (next_order < 0) next_order = order + 1; next_row = param & 63; break;
    case 14:
      switch (param >> 4) {
        case 1: c->pitch += (param & 15) * 4; c->target = c->pitch; break;
        case 2: c->pitch -= (param & 15) * 4; c->target = c->pitch; break;
        case 0xC: if (!(param & 15)) { c->env = 0; c->stage = ENV_OFF; } break;
      }
      break;
    case 15:
      if (param == 0) break;
      if (param < 0x20) speed = param;
      else { bpm = param; set_timer(); }
      break;
  }
}

static void tick_effects(int t) {
  chan_t *c = &ch[t];
  if (c->sfx) return;
  int fxn = c->fx ? c->fx - 1 : -1, p = c->param;
  switch (fxn) {
    case 1: c->pitch += p * 4; c->target = c->pitch; break;
    case 2: c->pitch -= p * 4; if (c->pitch < 0) c->pitch = 0; c->target = c->pitch; break;
    case 3: {
      int s = c->porta_speed * 4;
      if (c->pitch < c->target) { c->pitch += s; if (c->pitch > c->target) c->pitch = c->target; }
      else if (c->pitch > c->target) { c->pitch -= s; if (c->pitch < c->target) c->pitch = c->target; }
      break;
    }
    case 10: {
      int q = c->volslide_mem;
      c->svol = (uint8_t)clampi(c->svol + (q >> 4) - (q & 15), 0, 64);
      break;
    }
    case 14:
      if ((p >> 4) == 9 && (p & 15) && tick % (p & 15) == 0 && c->stage != ENV_OFF) trigger(t, c->note, c->ins);
      if ((p >> 4) == 0xC && tick == (p & 15)) { c->env = 0; c->stage = ENV_OFF; }
      if ((p >> 4) == 0xD && tick == (p & 15) && c->delayed) { row_cell(t, c->delayed, true); c->delayed = 0; }
      break;
  }
}

void mt_tick(void) {
  if (playing && !paused) {
    if (tick == 0) {
      next_order = -1; next_row = 0;
      int pat = cur.orders[order];
      const uint32_t *cells = cur.pattern(cur.ctx, pat);
      for (int t = 0; t < MT_TRACKS; t++) {
        ch[t].delayed = 0;
        if (!ch[t].reserved && cells) row_cell(t, cells[row * MT_TRACKS + t], false);
      }
    } else {
      for (int t = 0; t < MT_TRACKS; t++) if (!ch[t].reserved) tick_effects(t);
    }
  }
  for (int t = 0; t < MT_TRACKS; t++) update(t);
  if (!playing || paused) return;
  if (++tick >= speed) {
    tick = 0;
    if (next_order >= 0 && !loop_pat) { order = next_order; row = next_row; }
    else if (next_order >= 0) row = next_row;
    else if (++row >= MT_ROWS) { row = 0; if (!loop_pat) order++; }
    if (order >= cur.n_orders) order = cur.restart < cur.n_orders ? cur.restart : 0;
  }
}

void mt_play_source(const mt_source_t *src, int ord, int r, bool lp) {
  uint32_t s = irq_save();
  cur = *src;
  if (cur.n_orders < 1) { irq_restore(s); return; }
  order = clampi(ord, 0, cur.n_orders - 1); row = clampi(r, 0, MT_ROWS - 1); tick = 0;
  speed = cur.speed ? cur.speed : 6; bpm = cur.bpm >= 32 ? cur.bpm : 125; master = cur.master;
  loop_pat = lp; paused = false;
  for (int t = 0; t < MT_TRACKS; t++) {
    chan_t *c = &ch[t];
    if (!c->sfx) { c->stage = ENV_OFF; c->ins = 0; }
    c->sins = 0; c->svol = 64; c->pan = -1; c->fx = 0; c->offset = 0;
  }
  playing = true;
  set_timer();
  irq_restore(s);
}

void mt_stop(void) {
  uint32_t s = irq_save();
  playing = false;
  for (int t = 0; t < MT_TRACKS; t++) { ch[t].stage = ENV_OFF; ch[t].sfx = 0; ch[t].level = 0; hw_silence(t); }
  irq_restore(s);
}
void mt_pause(bool on) { paused = on; if (on) for (int t = 0; t < MT_TRACKS; t++) if (!ch[t].sfx) { ch[t].stage = ENV_OFF; } }
bool mt_playing(void) { return playing; }
void mt_set_timer(int t) { timer_id = t & 1; }
uint32_t mt_tick_hz(void) { return (uint32_t)bpm * 2 / 5; }
void mt_set_tempo(int sp, int b) {
  uint32_t s = irq_save();
  if (sp > 0) speed = sp;
  if (b >= 32) { bpm = b; if (playing) set_timer(); }
  irq_restore(s);
}
int mt_order(void) { return order; }
int mt_row(void) { return row; }
void mt_master(int v) { master = clampi(v, 0, 64); }

void mt_mute(int t, bool on) { ch[t & 15].muted = on; }
bool mt_muted(int t) { return ch[t & 15].muted; }
void mt_reserve(int t, bool on) {
  uint32_t s = irq_save();
  ch[t & 15].reserved = on;
  if (on && !ch[t & 15].sfx) { ch[t & 15].stage = ENV_OFF; }
  irq_restore(s);
}
int mt_level(int t) { return ch[t & 15].level; }

void mt_note(int t, int note, const mt_instrument_t *ins, int vol) {
  uint32_t s = irq_save();
  t &= 15;
  if (ins && ins->type == mt_track_type(t) && note >= 1 && note <= 96) {
    ch[t].sfx = 1; ch[t].xvol = (uint8_t)clampi(vol, 0, 64); ch[t].fx = 0; ch[t].pan = -1;
    trigger(t, note, ins);
  }
  irq_restore(s);
  if (!timer_on) set_timer();            // the timer keeps the envelopes moving
}
void mt_note_off(int t) {
  uint32_t s = irq_save();
  if (ch[t & 15].sfx) release(&ch[t & 15]);
  irq_restore(s);
}
void mt_sfx(int t, int note, int inst, int vol) {
  if (inst > 0 && inst < MT_INSTRUMENTS && cur.ins) mt_note(t, note, &cur.ins[inst], vol);
}

void mt_init(const void *b) {
  bank = (const uint8_t *)b;
  memset(ch, 0, sizeof ch);
  for (int t = 0; t < MT_TRACKS; t++) { ch[t].stage = ENV_OFF; ch[t].pan = -1; ch[t].svol = 64; hw_silence(t); }
  playing = false;
}

// ---------------------------------------------------------------- compact songs
static const uint8_t *song_patterns(const uint8_t *s) {
  const mt_header_t *h = (const mt_header_t *)s;
  return s + sizeof(mt_header_t) + 32 * h->n_ins + h->n_orders;
}
const char *mt_song_name(const void *song) { return ((const mt_header_t *)song)->name; }

int mt_song_check(const void *song, uint32_t max) {
  const uint8_t *s = song;
  if (rd32(s) != MT_MAGIC) return -1;
  uint32_t size = rd32(s + 4);
  const mt_header_t *h = song;
  if (size > max || size < sizeof(mt_header_t)) return -2;
  if (h->n_ins > MT_INSTRUMENTS || h->n_orders == 0) return -3;
  const uint8_t *po = song_patterns(s);
  if ((uint32_t)(po - s) + 2u * h->n_patterns > size) return -4;
  for (int i = 0; i < h->n_orders; i++) if (s[sizeof(mt_header_t) + 32 * h->n_ins + i] >= h->n_patterns) return -5;
  for (int p = 0; p < h->n_patterns; p++) if (rd16(po + 2 * p) >= size) return -6;
  return 0;
}

int mt_song_instruments(const void *song, mt_instrument_t *out) {
  const mt_header_t *h = song;
  memset(out, 0, sizeof(mt_instrument_t) * MT_INSTRUMENTS);
  memcpy(out, (const uint8_t *)song + sizeof(mt_header_t), 32 * h->n_ins);
  return h->n_ins;
}

int mt_song_decode(const void *song, int pat, uint32_t *cells) {
  const uint8_t *s = song;
  const mt_header_t *h = song;
  memset(cells, 0, MT_ROWS * MT_TRACKS * 4);
  if (pat < 0 || pat >= h->n_patterns) return -1;
  const uint8_t *p = s + rd16(song_patterns(s) + 2 * pat);
  uint32_t tmask = rd16(p); p += 2;
  for (int t = 0; t < MT_TRACKS; t++) {
    if (!(tmask & (1u << t))) continue;
    uint32_t m[2] = { rd32(p), rd32(p + 4) }; p += 8;
    for (int r = 0; r < MT_ROWS; r++)
      if (m[r >> 5] & (1u << (r & 31))) { cells[r * MT_TRACKS + t] = rd32(p); p += 4; }
  }
  return 0;
}

uint32_t mt_pattern_encode(const uint32_t *cells, uint8_t *dst) {
  uint8_t *p = dst + 2;
  uint32_t tmask = 0;
  for (int t = 0; t < MT_TRACKS; t++) {
    uint32_t m[2] = {0, 0};
    for (int r = 0; r < MT_ROWS; r++) if (cells[r * MT_TRACKS + t]) m[r >> 5] |= 1u << (r & 31);
    if (!m[0] && !m[1]) continue;
    tmask |= 1u << t;
    for (int k = 0; k < 2; k++) for (int b = 0; b < 4; b++) *p++ = (uint8_t)(m[k] >> (8 * b));
    for (int r = 0; r < MT_ROWS; r++) {
      uint32_t c = cells[r * MT_TRACKS + t];
      if (c) for (int b = 0; b < 4; b++) *p++ = (uint8_t)(c >> (8 * b));
    }
  }
  dst[0] = (uint8_t)tmask; dst[1] = (uint8_t)(tmask >> 8);
  return (uint32_t)(p - dst);
}

static const uint32_t *song_pattern(void *ctx, int pat) {
  (void)ctx;
  if (pat != pat_cached) { mt_song_decode(song_data, pat, pat_buf); pat_cached = pat; }
  return pat_buf;
}

int mt_play(const void *song, int ord) {
  if (mt_song_check(song, 0x1000000) != 0) return -1;
  mt_stop();
  const mt_header_t *h = song;
  song_data = song; pat_cached = -1;
  mt_song_instruments(song, song_ins);
  mt_source_t src = {
    .ins = song_ins, .orders = (const uint8_t *)song + sizeof(mt_header_t) + 32 * h->n_ins,
    .n_orders = h->n_orders, .restart = h->restart, .pattern = song_pattern, .ctx = 0,
    .speed = h->speed, .bpm = h->bpm, .master = h->master,
  };
  mt_play_source(&src, ord, 0, false);
  return 0;
}
