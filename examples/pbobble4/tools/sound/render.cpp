// Offline sound renderer: runs the game's own Z80 sound driver (m1.bin) with a YM2610 (ymfm) and
// writes what one sound command produces. Used to prepare the sounds of the port; nothing of this
// runs on the console. Build with tools/sound/build.sh (fetches ymfm, BSD-3, and a Z80 core, MIT).
//
// usage: render M1 V OUT SECONDS CMD...     one WAV per command: OUT_xx.wav (hex command)
//        render M1 V OUT SECONDS -writes FILE   the same, and the YM2610 writes per driver step (OUT.writes)
//        render M1 V - SECONDS -classify [P]  what every command 00-FF does, alone and over music 42
//                                             (with P: the pair P, command, one frame apart)
//        render M1 V OUT SECONDS -trace FILE  plays a trace "frame command" (60 frames per second)
// Neo Geo sound board: Z80 at 4 MHz, YM2610 at 8 MHz (output 8 MHz / 144 = 55555 Hz), 68000 to Z80
// through a latch and the NMI, Z80 memory 0000-7FFF fixed, 8000/C000/E000/F000 banked by reads of
// ports 0B/0A/09/08 with the bank number on the high byte of the address bus, RAM F800-FFFF.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include "ymfm_opn.h"
extern "C" {
#include "z80.h"
extern uint8_t z80_io_high;              // high byte of the I/O address (patch in build.sh)
}

static std::vector<uint8_t> m1, vrom;
static uint8_t ram[0x800];
static uint32_t bank[4] = {0x1E, 0x0E, 0x06, 0x02};      // F000 2K, E000 4K, C000 8K, 8000 16K
static uint8_t latch, reply; static bool nmi_on;
// what the driver does to the YM2610, for the analysis of the commands (-scan)
static uint8_t ym_addr[2];
static unsigned key_fm, key_adpcma, key_adpcmb, ssg_on;
static FILE *wlog;                    // -writes: YM2610 writes, one line per driver step
static uint32_t ticks, nsamples; static bool irq_prev;

struct intf : public ymfm::ymfm_interface {
  int32_t timer[2] = {-1, -1}; bool irq = false;
  uint8_t ymfm_external_read(ymfm::access_class t, uint32_t a) override {
    (void)t; return vrom.empty() ? 0 : vrom[a % vrom.size()];
  }
  void ymfm_set_timer(uint32_t n, int32_t d) override { timer[n] = d; }
  void ymfm_update_irq(bool a) override { irq = a; }
  void tick(int32_t clocks) {
    for (int n = 0; n < 2; n++)
      if (timer[n] >= 0 && (timer[n] -= clocks) <= 0) { timer[n] = -1; m_engine->engine_timer_expired(n); }
  }
};
static intf yi; static ymfm::ym2610 *ym;

static uint8_t rd(void *, uint16_t a) {
  if (a < 0x8000) return m1[a];
  if (a >= 0xF800) return ram[a & 0x7FF];
  if (a >= 0xF000) return m1[(bank[0] * 0x800 + (a & 0x7FF)) % m1.size()];
  if (a >= 0xE000) return m1[(bank[1] * 0x1000 + (a & 0xFFF)) % m1.size()];
  if (a >= 0xC000) return m1[(bank[2] * 0x2000 + (a & 0x1FFF)) % m1.size()];
  return m1[(bank[3] * 0x4000 + (a & 0x3FFF)) % m1.size()];
}
static void wr(void *, uint16_t a, uint8_t v) { if (a >= 0xF800) ram[a & 0x7FF] = v; }
static uint8_t in(z80 *, uint8_t p) {
  switch (p & 0x0F) {
  case 0x00: return latch;
  case 0x04: case 0x05: case 0x06: case 0x07: return ym->read(p & 3);
  case 0x08: case 0x09: case 0x0A: case 0x0B: bank[p & 3] = z80_io_high; return 0;
  }
  return 0;
}
static void out(z80 *, uint8_t p, uint8_t v) {
  if ((p & 0x1F) == 0x08) nmi_on = true;
  else if ((p & 0x1F) == 0x18) nmi_on = false;
  else if ((p & 0x0C) == 0x04) {
    int port = (p >> 1) & 1;
    if (!(p & 1)) ym_addr[port] = v;
    else {
      uint8_t a = ym_addr[port];
      if (port == 0 && a == 0x28 && (v & 0xF0)) key_fm++;                     // FM key on
      if (port == 1 && a == 0x00 && !(v & 0x80) && (v & 0x3F)) key_adpcma++;  // ADPCM-A key on
      if (port == 0 && a == 0x10 && (v & 0x80)) key_adpcmb++;                 // ADPCM-B start
      if (port == 0 && a >= 0x08 && a <= 0x0A && (v & 0x1F)) ssg_on++;        // SSG volume
    }
    ym->write(p & 3, v);
    if (wlog) fprintf(wlog, " %x%02x", p & 3, v);
  }
  else if ((p & 0x0F) == 0x0C) reply = v;
}

static z80 cpu;
static void reset() {
  ticks = nsamples = 0; irq_prev = false;
  memset(ram, 0, sizeof ram); bank[0] = 0x1E; bank[1] = 0x0E; bank[2] = 0x06; bank[3] = 0x02;
  latch = reply = 0; nmi_on = false;
  z80_init(&cpu); cpu.read_byte = rd; cpu.write_byte = wr; cpu.port_in = in; cpu.port_out = out;
  ym->reset(); yi.timer[0] = yi.timer[1] = -1; yi.irq = false;
}
static void command(uint8_t c) { latch = c; if (nmi_on) z80_gen_nmi(&cpu); }

// one output sample: 72 Z80 cycles, 144 YM2610 clocks
static void sample(int16_t *lr) {
  unsigned long target = cpu.cyc + 72;
  while (cpu.cyc < target) {
    cpu.int_pending = yi.irq; cpu.int_data = 0xFF;
    z80_step(&cpu);
  }
  yi.tick(144); nsamples++;
  if (yi.irq && !irq_prev && wlog) fprintf(wlog, "\n%u", nsamples);   // a timer interrupt: one driver step
  irq_prev = yi.irq;
  ymfm::ym2610::output_data o; ym->generate(&o, 1);
  for (int k = 0; k < 2; k++) {
    int32_t v = o.data[k] + o.data[2] / 2;       // FM + ADPCM, then SSG (mono) at half level
    lr[k] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
  }
}

static void wav(const std::string &path, const std::vector<int16_t> &s) {
  FILE *f = fopen(path.c_str(), "wb");
  uint32_t rate = 55555, n = s.size() * 2, v;
  fwrite("RIFF", 1, 4, f); v = 36 + n; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
  v = 16; fwrite(&v, 4, 1, f); uint16_t h[2] = {1, 2}; fwrite(h, 2, 2, f); fwrite(&rate, 4, 1, f);
  v = rate * 4; fwrite(&v, 4, 1, f); h[0] = 4; h[1] = 16; fwrite(h, 2, 2, f);
  fwrite("data", 1, 4, f); fwrite(&n, 4, 1, f); fwrite(s.data(), 2, s.size(), f); fclose(f);
}

static std::vector<uint8_t> load(const char *p) {
  FILE *f = fopen(p, "rb"); if (!f) { fprintf(stderr, "missing %s\n", p); exit(1); }
  std::vector<uint8_t> d; int c; while ((c = fgetc(f)) != EOF) d.push_back((uint8_t)c); fclose(f); return d;
}

// level per half second (peak of |L|,|R|) of SECONDS seconds after the driver start-up (03, 07),
// with the commands cmds[k] sent at times at[k] (seconds)
static std::vector<int> levels(double seconds, std::vector<std::pair<double, unsigned>> cmds) {
  const uint32_t rate = 55555; reset(); int16_t lr[2];
  command(0x03); for (uint32_t i = 0; i < rate / 10; i++) sample(lr);
  command(0x07); for (uint32_t i = 0; i < rate / 2; i++) sample(lr);
  std::vector<int> out; size_t k = 0; int peak = 0;
  for (uint32_t i = 0; i < (uint32_t)(seconds * rate); i++) {
    while (k < cmds.size() && cmds[k].first * rate <= i) command((uint8_t)cmds[k++].second);
    sample(lr); int p = std::max(abs(lr[0]), abs(lr[1])); if (p > peak) peak = p;
    if ((i + 1) % (rate / 2) == 0) { out.push_back(peak); peak = 0; }
  }
  return out;
}

static void classify(double seconds, int prefix) {
  std::vector<int> base = levels(seconds, {{0.0, 0x42}});
  for (unsigned c = 0; c < 256; c++) {
    std::vector<std::pair<double, unsigned>> alone = {{0.0, c}};
    if (prefix >= 0) alone = {{0.0, (unsigned)prefix}, {1.0 / 60, c}};    // the game sends a prefix, then the number
    std::vector<int> a = levels(seconds, alone);
    int first = -1, last = -1, peak = 0;
    for (size_t i = 0; i < a.size(); i++) if (a[i] > 300) { if (first < 0) first = (int)i; last = (int)i; peak = std::max(peak, a[i]); }
    std::vector<std::pair<double, unsigned>> over = {{0.0, 0x42}, {3.0, c}};
    if (prefix >= 0) over = {{0.0, 0x42}, {3.0, (unsigned)prefix}, {3.0 + 1.0 / 60, c}};
    std::vector<int> b = levels(seconds, over);
    long eb = 0, e0 = 0; for (size_t i = 8; i < b.size(); i++) { eb += b[i]; e0 += base[i]; }
    printf("%02X alone: %s", c, first < 0 ? "silent" : "");
    if (first >= 0) printf("%.1f-%.1f s peak %5d%s", first / 2.0, (last + 1) / 2.0, peak, last == (int)a.size() - 1 ? " (still on)" : "");
    printf(" | over music: level %3ld %%\n", e0 ? eb * 100 / e0 : 0);
    fflush(stdout);
  }
}

int main(int argc, char **argv) {
  if (argc < 6) { fprintf(stderr, "usage: render M1 V OUT SECONDS CMD... | -trace FILE\n"); return 1; }
  m1 = load(argv[1]); vrom = load(argv[2]);
  ym = new ymfm::ym2610(yi);
  std::string base = argv[3]; double seconds = atof(argv[4]);
  const uint32_t rate = 55555, boot = rate / 2;
  if (!strcmp(argv[5], "-classify")) { classify(seconds, argc > 6 ? (int)strtoul(argv[6], 0, 16) : -1); return 0; }
  if (!strcmp(argv[5], "-writes")) {         // like -trace, but logs the YM2610 writes (OUT.writes)
    wlog = fopen((base + ".writes").c_str(), "w"); argv[5] = (char *)"-trace";
  }
  if (!strcmp(argv[5], "-trace")) {           // the commands of a game session, at their frames
    FILE *f = fopen(argv[6], "r"); unsigned fr, c; std::vector<std::pair<unsigned, unsigned>> tr;
    while (f && fscanf(f, "%u %x", &fr, &c) == 2) tr.push_back({fr, c});
    reset(); std::vector<int16_t> s; size_t k = 0; int16_t lr[2];
    for (uint32_t i = 0; i < (uint32_t)(seconds * rate); i++) {
      while (k < tr.size() && tr[k].first * rate / 60 <= i) command((uint8_t)tr[k++].second);
      sample(lr); s.push_back(lr[0]); s.push_back(lr[1]);
    }
    wav(base + ".wav", s); return 0;
  }
  if (!strcmp(argv[5], "-probe") || !strcmp(argv[5], "-clip")) {
    // -probe FILE: one event per line (hex bytes sent after the start-up of the driver), prints
    //   how long it sounds and whether it still sounds at the end
    // -clip BYTES...: writes OUT.wav for one event (mono, 55555 Hz)
    bool clip = !strcmp(argv[5], "-clip");
    std::vector<std::vector<unsigned>> events;
    if (clip) { std::vector<unsigned> e; for (int a = 6; a < argc; a++) e.push_back(strtoul(argv[a], 0, 16)); events.push_back(e); }
    else {
      FILE *f = fopen(argv[6], "r"); char line[256];
      while (f && fgets(line, sizeof line, f)) {
        std::vector<unsigned> e; char *p = line, *q;
        for (;;) { unsigned long v = strtoul(p, &q, 16); if (q == p) break; e.push_back((unsigned)v); p = q; }
        if (!e.empty()) events.push_back(e);
      }
    }
    for (auto &e : events) {
      reset(); int16_t lr[2]; std::vector<int16_t> mono;
      uint32_t t = 0, start = rate * 3 / 10, len = (uint32_t)(seconds * rate), last = 0, peak = 0;
      double tail = 0;
      for (; t < start + len; t++) {
        if (t == 0) command(0x03);
        if (t == rate / 10) command(0x07);
        if (t >= start && (t - start) % (rate / 60) == 0 && (t - start) / (rate / 60) < e.size())
          command((uint8_t)e[(t - start) / (rate / 60)]);
        sample(lr);
        if (t < start) continue;
        int m = (lr[0] + lr[1]) / 2;
        if (clip) mono.push_back((int16_t)m);
        if (abs(m) > 200) last = t - start;
        if ((uint32_t)abs(m) > peak) peak = abs(m);
        if (t >= start + len - rate / 2) tail += (double)m * m;
      }
      if (clip) {
        std::vector<int16_t> st; for (int16_t v : mono) { st.push_back(v); st.push_back(v); }
        wav(base + ".wav", st);
      }
      for (unsigned v : e) printf("%02X ", v);
      printf("| sound %.2f s peak %u tail %.0f%s\n", last / (double)rate, peak, sqrt(tail / (rate / 2)),
             last + rate / 2 >= len ? " LOOPS?" : "");
      fflush(stdout);
    }
    return 0;
  }
  if (!strcmp(argv[5], "-scan")) {            // every command after the game's start-up, alone or over a music
    int over = argc > 6 ? (int)strtoul(argv[6], 0, 16) : -1;
    for (unsigned c = 0; c < 256; c++) {
      reset(); int16_t lr[2];
      const unsigned seq[][2] = {{0, 3}, {3, 2}, {465, 3}, {471, 7}};
      uint32_t t = 0, start = 600 * rate / 60, len = (uint32_t)(seconds * rate);
      size_t k = 0;
      double e_before = 0, e_after = 0; uint32_t last = 0;
      for (; t < start + len; t++) {
        while (k < 4 && seq[k][0] * rate / 60 <= t) command((uint8_t)seq[k++][1]);
        if (over >= 0 && t == start - rate * 2) command((uint8_t)over);
        if (t == start) { command((uint8_t)c); key_fm = key_adpcma = key_adpcmb = ssg_on = 0; }
        sample(lr);
        double e = (double)lr[0] * lr[0] + (double)lr[1] * lr[1];
        if (t >= start - rate && t < start) e_before += e;
        if (t >= start + len - rate) e_after += e;
        if (t >= start && (abs(lr[0]) > 300 || abs(lr[1]) > 300)) last = t - start;
      }
      printf("%02X fm %3u adpcma %3u adpcmb %2u ssg %3u sound %5.2f s  rms before %6.0f last second %6.0f\n", c,
             key_fm, key_adpcma, key_adpcmb, ssg_on, last / (double)rate,
             sqrt(e_before / rate / 2), sqrt(e_after / rate / 2));
      fflush(stdout);
    }
    return 0;
  }
  for (int a = 5; a < argc; a++) {
    unsigned c = strtoul(argv[a], 0, 16);
    reset(); int16_t lr[2]; std::vector<int16_t> s;
    for (uint32_t i = 0; i < boot; i++) sample(lr);                 // driver start-up
    command(0x03); for (uint32_t i = 0; i < rate / 10; i++) sample(lr);
    command((uint8_t)c);
    for (uint32_t i = 0; i < (uint32_t)(seconds * rate); i++) { sample(lr); s.push_back(lr[0]); s.push_back(lr[1]); }
    char name[16]; snprintf(name, sizeof name, "_%02X.wav", c); wav(base + name, s);
    int32_t peak = 0; for (int16_t v : s) peak = abs(v) > peak ? abs(v) : peak;
    printf("%02X: peak %d, reply %02X\n", c, peak, reply);
  }
  return 0;
}
